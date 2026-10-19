/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Adapted from aria2-next (https://github.com/AnInsomniacy/aria2-next),
 * Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later.
 */

#include "base/net/aria2/segmentplanner.h"

#include <algorithm>
#include <map>

namespace Net::Aria2
{
    namespace
    {
        // Splits land on page boundaries so writes stay aligned.
        constexpr int64_t alignment = 4096;
        // A new connection gets this long to show its speed before it is raced.
        constexpr double raceSeconds = 2.0;
        // A copy is started only for a range that needs at least this long.
        constexpr double minRaceSeconds = 0.5;
        // Ranges are never split below this, however slow the connection.
        constexpr int64_t minSplitFloor = 64 << 10;
        // Copies of one range that are delivering or still starting, including the
        // original. Copies that stay silent do not count; at most 2 * maxCopies of
        // those are tried.
        constexpr int maxCopies = 3;
    }

    void SegmentPlanner::setLength(const int64_t length)
    {
        m_length = length;
        for (auto &[id, worker] : m_workers)
        {
            worker.end = std::min(worker.end, length);
            worker.requestEnd = std::min(worker.requestEnd, length);
        }
    }

    void SegmentPlanner::restore(const std::vector<Range> &ranges)
    {
        for (const auto &[begin, end] : ranges)
            m_received.add(begin, end);
    }

    void SegmentPlanner::reset()
    {
        m_received.clear();
        m_workers.clear();
        m_length = -1;
        m_peakSpeed = 0;
    }

    void SegmentPlanner::place(const int id, const int64_t begin, const int64_t requestEnd)
    {
        const int64_t end = (m_length >= 0) ? std::min(requestEnd, m_length) : requestEnd;
        m_workers[id] = Worker {begin, end, requestEnd, false, 0};
    }

    const SegmentPlanner::Worker *SegmentPlanner::worker(const int id) const
    {
        const auto found = m_workers.find(id);
        return (found == m_workers.end()) ? nullptr : &found->second;
    }

    bool SegmentPlanner::complete() const
    {
        return (m_length >= 0) && m_received.contains(0, m_length);
    }

    double SegmentPlanner::typicalSpeed(const int except) const
    {
        double sum = 0;
        int count = 0;
        for (const auto &[id, worker] : m_workers)
        {
            if ((id != except) && worker.receiving && (worker.speed > 0))
            {
                sum += worker.speed;
                ++count;
            }
        }
        return count ? (sum / count) : 0;
    }

    // Halving a range of R bytes at speed v saves R / 2v and costs a new
    // connection's start-up, so it pays off from R = 2 * v * startup. On fast
    // links that is more than min-split-size; on slow ones (0.1 MB/s overseas)
    // it lets the last megabytes spread over fresh connections instead of
    // trailing behind one.
    int64_t SegmentPlanner::minSplitFor(const double speed) const
    {
        if ((speed <= 0) || (m_startup <= 0))
            return m_minSplit;

        const auto worth = static_cast<int64_t>(2 * speed * m_startup);
        return std::clamp(worth, std::min(minSplitFloor, m_minSplit), m_minSplit);
    }

    std::optional<Range> SegmentPlanner::largestGap() const
    {
        if (m_length < 0)
            return std::nullopt;

        auto owned = m_received;
        for (const auto &[id, worker] : m_workers)
            owned.add(worker.position, std::min(worker.end, m_length));

        std::optional<Range> best;
        for (auto gap = owned.firstGap(0, m_length); gap;
                gap = owned.firstGap(gap->second, m_length))
        {
            if (!best || (gap->second - gap->first) > (best->second - best->first))
                best = gap;
        }
        return best;
    }

    std::optional<Range> SegmentPlanner::assign(const int id)
    {
        if (m_length < 0)
            return std::nullopt;

        if (const auto gap = largestGap())
        {
            m_workers[id] = Worker {gap->first, gap->second, gap->second, false, 0};
            return gap;
        }

        if (auto range = split(id))
            return range;

        return race(id);
    }

    std::optional<Range> SegmentPlanner::split(const int id)
    {
        const double typical = typicalSpeed();
        Worker *victim = nullptr;
        double longest = 0;

        for (auto &[other, worker] : m_workers)
        {
            const int64_t remaining = worker.end - worker.position;
            const double speed = (worker.speed > 0) ? worker.speed : typical;
            if ((worker.end == unbounded) || (remaining < (2 * minSplitFor(speed))))
                continue;

            // A connection far behind the others is raced, not split: splitting
            // would leave it holding the start of its range.
            if (worker.receiving && (typicalSpeed(other) > (2 * worker.speed)))
                continue;

            // Unknown speeds compare by size; any measured speed beats none.
            const double time = (speed > 0) ? (remaining / speed) : (remaining * 1e-3);
            if (!victim || (time > longest))
            {
                victim = &worker;
                longest = time;
            }
        }

        if (!victim)
            return std::nullopt;

        auto &target = *victim;
        const int64_t remaining = target.end - target.position;
        const double own = (target.speed > 0) ? target.speed : typical;
        const double fresh = (typical > 0) ? typical : own;
        const double share = ((own > 0) && (fresh > 0)) ? (own / (own + fresh)) : 0.5;
        const int64_t least = minSplitFor(own);

        auto cut = target.position + static_cast<int64_t>(remaining * share);
        cut = std::clamp(cut, target.position + least, target.end - least);
        if ((cut - (cut % alignment)) > (target.position + least))
            cut -= cut % alignment;

        const Range range {cut, target.end};
        target.end = cut;
        m_workers[id] = Worker {range.first, range.second, range.second, false, 0};
        return range;
    }

    // The endgame: a range that would hold the download back gets another copy;
    // the first copy to finish wins. A copy that turns out slow as well (another
    // bad route) may be raced again, up to maxCopies.
    std::optional<Range> SegmentPlanner::race(const int id)
    {
        struct Group
        {
            int leader = -1; // the copy with the earliest expected finish
            double time = 0;
            int copies = 0;
            int silent = 0;
            bool young = false;
        };

        std::map<int64_t, Group> groups; // by end

        // A copy without data after several usual start-ups is silent.
        const double silentAfter =
                (m_startup > 0) ? std::clamp((3 * m_startup), minRaceSeconds, raceSeconds)
                                : raceSeconds;

        for (const auto &[other, worker] : m_workers)
        {
            const int64_t remaining = worker.end - worker.position;
            if ((remaining <= 0) || (worker.end == unbounded))
                continue;

            auto &group = groups[worker.end];
            // A copy that never delivered (a server that accepted the request and
            // sends nothing) must not use up the copies that could replace it.
            if (!worker.receiving && (worker.age >= silentAfter))
            {
                ++group.silent;
            }
            else
            {
                ++group.copies;
            }

            // A new connection gets time to connect and show its speed.
            group.young = group.young
                    || (worker.age < (worker.receiving ? raceSeconds : silentAfter));

            const double speed = worker.receiving ? worker.speed : 0;
            const double time = (speed > 0) ? (static_cast<double>(remaining) / speed) : 1e9;
            if ((group.leader < 0) || (time < group.time))
            {
                group.leader = other;
                group.time = time;
            }
        }

        const Worker *slowest = nullptr;
        double longest = 0;
        for (const auto &[end, group] : groups)
        {
            if (group.young || (group.copies >= maxCopies)
                    || (group.silent >= (2 * maxCopies)) || (group.time <= longest))
                continue;

            const auto &leader = m_workers.at(group.leader);
            const double speed = leader.receiving ? leader.speed : 0;
            const double reference = std::max(typicalSpeed(group.leader), m_peakSpeed);

            // A copy needs a start-up and then the rest at a normal connection's
            // speed; it is worth starting only if it would finish well first.
            const double remaining = static_cast<double>(leader.end - leader.position);
            const double copy = ((m_startup > 0) ? m_startup : raceSeconds)
                    + ((reference > 0) ? (remaining / reference) : 0);
            if (group.time <= std::max(minRaceSeconds, (1.5 * copy)))
                continue;

            // Raced only when it is markedly slower than its siblings or than other
            // connections of this download managed before.
            if ((reference <= 0) || (speed < (reference / 2)))
            {
                slowest = &leader;
                longest = group.time;
            }
        }

        if (!slowest)
            return std::nullopt;

        const Range range {slowest->position, slowest->end};
        m_workers[id] = Worker {range.first, range.second, range.second, false, 0};
        return range;
    }

    int64_t SegmentPlanner::receive(const int id, const int64_t bytes)
    {
        auto &worker = m_workers.at(id);
        const int64_t usable = std::clamp<int64_t>(worker.end - worker.position, 0, bytes);
        m_received.add(worker.position, (worker.position + usable));
        worker.position += usable;
        worker.receiving = true;
        return usable;
    }

    void SegmentPlanner::updateSpeed(const int id, const double bytesPerSecond, const double seconds)
    {
        if (const auto found = m_workers.find(id); found != m_workers.end())
        {
            found->second.age += seconds;
            auto &speed = found->second.speed;
            speed = (speed > 0) ? ((speed * 0.6) + (bytesPerSecond * 0.4)) : bytesPerSecond;
            m_peakSpeed = std::max(m_peakSpeed, speed);
        }
    }

    bool SegmentPlanner::extend(const int id, std::optional<int> &cancelled)
    {
        auto &worker = m_workers.at(id);
        if ((worker.requestEnd <= worker.end)
                || ((m_length >= 0) && (worker.end >= m_length)))
            return false;

        // Take over the next range if its connection has not produced data yet.
        for (auto &[other, next] : m_workers)
        {
            if ((other != id) && (next.position == worker.end) && !next.receiving)
            {
                worker.end = std::min(worker.requestEnd, next.end);
                cancelled = other;
                m_workers.erase(other);
                return true;
            }
        }

        // Or continue into unowned bytes directly after it.
        auto owned = m_received;
        for (const auto &[other, next] : m_workers)
        {
            if (other != id)
                owned.add(next.position, next.end);
        }

        const int64_t limit =
                (m_length >= 0) ? std::min(worker.requestEnd, m_length) : worker.requestEnd;
        if (const auto gap = owned.firstGap(worker.end, limit);
                gap && (gap->first == worker.end))
        {
            worker.end = gap->second;
            return true;
        }
        return false;
    }

    std::vector<int> SegmentPlanner::redundant() const
    {
        std::vector<int> result;
        for (const auto &[id, worker] : m_workers)
        {
            if ((worker.position < worker.end)
                    && m_received.contains(worker.position, worker.end))
                result.push_back(id);
        }
        return result;
    }
}
