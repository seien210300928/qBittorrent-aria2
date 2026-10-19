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

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace Net::Aria2
{
    using Range = std::pair<int64_t, int64_t>; // [begin, end)

    // Disjoint, coalesced byte ranges.
    class RangeSet
    {
    public:
        void add(int64_t begin, int64_t end);
        void remove(int64_t begin, int64_t end);
        void clear() { m_ranges.clear(); }
        bool contains(int64_t begin, int64_t end) const;
        int64_t total() const;
        // Bytes of [begin, end) that are present.
        int64_t covered(int64_t begin, int64_t end) const;
        // First missing range inside [from, limit), if any.
        std::optional<Range> firstGap(int64_t from, int64_t limit) const;
        std::vector<Range> ranges() const { return {m_ranges.begin(), m_ranges.end()}; }
        bool empty() const { return m_ranges.empty(); }

    private:
        std::map<int64_t, int64_t> m_ranges; // begin -> end
    };
}
