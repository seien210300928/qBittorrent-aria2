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

#pragma once

#include "base/net/aria2/rangeset.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace Net::Aria2
{
    inline constexpr int64_t unbounded = std::numeric_limits<int64_t>::max();

    // Decides which byte range each connection downloads. It knows nothing about
    // sockets: the task reports bytes and outcomes, the planner answers with
    // assignments, extensions and cancellations.
    class SegmentPlanner
    {
    public:
        struct Worker
        {
            int64_t position = 0;   // next byte this worker will receive
            int64_t end = 0;        // assigned end, exclusive; may shrink
            int64_t requestEnd = 0; // end the request asked for, exclusive
            bool receiving = false; // has received payload bytes
            double speed = 0;       // bytes per second, smoothed
            double age = 0;         // seconds of speed samples since assignment
        };

        // `minSplit`: a range is split only if both halves get at least this much.
        // Once connections show their speed and start-up time, the limit drops to
        // what a new connection can still win (see minSplitFor).
        explicit SegmentPlanner(int64_t minSplit) : m_minSplit {minSplit} {}

        // Also clamps workers placed before the length was known.
        void setLength(int64_t length);
        int64_t length() const { return m_length; }
        const RangeSet &received() const { return m_received; }
        void restore(const std::vector<Range> &ranges);
        void reset();

        // Range for a new connection, or nothing when no work is worth starting.
        // The returned range is registered as worker `id`.
        std::optional<Range> assign(int id);
        // Registers a worker for an explicit request (first request, resume).
        void place(int id, int64_t begin, int64_t requestEnd);

        // Records received bytes and returns how many of them belong to the worker's
        // assignment; the rest must be discarded.
        int64_t receive(int id, int64_t bytes);
        // Called periodically with the rate over the last `seconds`.
        void updateSpeed(int id, double bytesPerSecond, double seconds);
        // The average rate of a finished connection, which may have been too short
        // for periodic samples.
        void noteSpeed(double bytesPerSecond)
        {
            m_peakSpeed = std::max(m_peakSpeed, bytesPerSecond);
        }
        // Seconds from a request to its first payload byte.
        void noteStartup(double seconds)
        {
            m_startup = (m_startup > 0) ? ((m_startup * 0.7) + (seconds * 0.3)) : seconds;
        }

        // After a worker reached its end: if its request continues into a range of a
        // worker that has not received anything, it takes that range over and the
        // returned id must be cancelled. Returns false when the worker is finished.
        bool extend(int id, std::optional<int> &cancelled);
        // Workers whose remaining range is already covered by others (race losers).
        std::vector<int> redundant() const;

        void release(int id) { m_workers.erase(id); }
        // Received bytes that turned out bad and are needed again.
        void forget(int64_t begin, int64_t end) { m_received.remove(begin, end); }
        const Worker *worker(int id) const;
        const std::map<int, Worker> &workers() const { return m_workers; }
        bool complete() const;

    private:
        std::optional<Range> largestGap() const;
        std::optional<Range> split(int id);
        std::optional<Range> race(int id);
        double typicalSpeed(int except = -1) const;
        int64_t minSplitFor(double speed) const;

        int64_t m_minSplit;
        int64_t m_length = -1;
        RangeSet m_received;
        std::map<int, Worker> m_workers;
        double m_peakSpeed = 0; // fastest smoothed connection speed seen
        double m_startup = 0;   // smoothed request-to-first-byte seconds
    };
}
