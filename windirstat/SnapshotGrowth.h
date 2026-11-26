// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace snapshot
{
    struct Change
    {
        std::wstring path;
        std::uint64_t before, after;
        bool Growing() const { return after > before; }
        std::uint64_t Difference() const { return Growing() ? after - before : before - after; }
    };

    using Sizes = std::unordered_map<std::wstring, std::uint64_t>;
    inline std::vector<Change> CompareFiles(Sizes before, const Sizes& current)
    {
        std::vector<Change> rows;
        for (const auto& [path, size] : current)
        {
            auto old = before.extract(path);
            const auto previous = old.empty() ? 0ULL : old.mapped();
            if (previous != size) rows.push_back({ path, previous, size });
        }
        for (const auto& [path, size] : before)
            if (size) rows.push_back({ path, size, 0 });
        std::ranges::sort(rows, [](const Change& a, const Change& b)
        {
            if (a.Growing() != b.Growing()) return a.Growing();
            if (a.Difference() == b.Difference()) return a.path < b.path;
            return a.Growing() ? a.Difference() > b.Difference() : a.Difference() < b.Difference();
        });
        return rows;
    }
}
