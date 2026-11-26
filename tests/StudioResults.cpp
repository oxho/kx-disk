// SPDX-License-Identifier: GPL-3.0-or-later
#include "../windirstat/BannerConfig.h"
#include "../windirstat/SnapshotGrowth.h"
#include <cassert>

int main()
{
    const auto changes = snapshot::CompareFiles(
        {{L"样本/增长", 5}, {L"样本/移除", 10}, {L"不变", 2}, {L"缩小", 3}},
        {{L"样本/增长", 10}, {L"新增", UINT64_MAX}, {L"不变", 2}, {L"缩小", 2}});
    assert(changes.size() == 4);
    assert(changes[0].path == L"新增" && changes[0].Difference() == UINT64_MAX);
    assert(changes[1].path == L"样本/增长" && changes[1].Difference() == 5);
    assert(changes[2].path == L"缩小" && changes[3].path == L"样本/移除");
    assert(snapshot::CompareFiles({{L"中文", 4}}, {{L"中文", 4}}).empty());

    using studio::BannerConfigReader;
    std::wstring payload = LR"({"app_id":"2dbcd68cb497","placements":[{"kind":"banner","slot":"other"},{"slot":"scan-result","kind":"banner","asset":"","target":"https://example.org","title":"\u5f00枝"}]})";
    const auto placement = BannerConfigReader(payload).Read();
    assert(placement && placement->at(L"title") == L"开枝");
    assert(!BannerConfigReader(payload + L"X").Read());
    assert(!BannerConfigReader(LR"({"app_id":"2dbcd68cb497","placements":[]})").Read());
    auto wrongSlot = payload;
    wrongSlot.replace(wrongSlot.find(L"scan-result"), 11, L"splash");
    assert(!BannerConfigReader(wrongSlot).Read());
    payload.replace(payload.find(L"2dbcd68cb497"), 12, L"other");
    assert(!BannerConfigReader(payload).Read());
}
