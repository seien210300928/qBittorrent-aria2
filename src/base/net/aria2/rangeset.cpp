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

#include "base/net/aria2/rangeset.h"

#include <algorithm>

namespace Net::Aria2
{
    void RangeSet::add(int64_t begin, int64_t end)
    {
        if (begin >= end)
            return;

        auto it = m_ranges.upper_bound(begin);
        if ((it != m_ranges.begin()) && (std::prev(it)->second >= begin))
        {
            --it;
            begin = it->first;
        }

        while ((it != m_ranges.end()) && (it->first <= end))
        {
            end = std::max(end, it->second);
            it = m_ranges.erase(it);
        }

        m_ranges.emplace(begin, end);
    }

    void RangeSet::remove(int64_t begin, int64_t end)
    {
        if (begin >= end)
            return;

        auto it = m_ranges.upper_bound(begin);
        if (it != m_ranges.begin())
            --it;

        while ((it != m_ranges.end()) && (it->first < end))
        {
            const auto [first, last] = *it;
            if (last <= begin)
            {
                ++it;
                continue;
            }

            it = m_ranges.erase(it);
            if (first < begin)
                m_ranges.emplace(first, begin);
            if (last > end)
            {
                m_ranges.emplace(end, last);
                break;
            }
        }
    }

    bool RangeSet::contains(const int64_t begin, const int64_t end) const
    {
        if (begin >= end)
            return true;

        const auto it = m_ranges.upper_bound(begin);
        return (it != m_ranges.begin()) && (std::prev(it)->second >= end);
    }

    int64_t RangeSet::total() const
    {
        int64_t sum = 0;
        for (const auto &[begin, end] : m_ranges)
            sum += end - begin;
        return sum;
    }

    int64_t RangeSet::covered(const int64_t begin, const int64_t end) const
    {
        int64_t sum = 0;
        auto it = m_ranges.upper_bound(begin);
        if (it != m_ranges.begin())
            --it;

        for (; (it != m_ranges.end()) && (it->first < end); ++it)
            sum += std::max<int64_t>(0, (std::min(end, it->second) - std::max(begin, it->first)));

        return sum;
    }

    std::optional<Range> RangeSet::firstGap(const int64_t from, const int64_t limit) const
    {
        auto cursor = from;
        const auto it = m_ranges.upper_bound(from);
        if ((it != m_ranges.begin()) && (std::prev(it)->second > from))
            cursor = std::prev(it)->second;

        if (cursor >= limit)
            return std::nullopt;

        const int64_t next = (it == m_ranges.end()) ? limit : std::min(limit, it->first);
        return Range {cursor, next};
    }
}
