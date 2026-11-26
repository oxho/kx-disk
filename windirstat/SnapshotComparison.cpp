// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "SnapshotComparison.h"
#include "SnapshotGrowth.h"
#include "CsvLoader.h"
#include "ProgressDlg.h"

namespace
{
    using namespace snapshot;

    struct Report
    {
        std::vector<Change> rows;
        std::wstring summary;
    };

    Sizes ReadFiles(CItem* root)
    {
        Sizes sizes;
        for (const auto* file : CItem::GetItemsRecursive({root}))
            if (file->HasSizePhysical()) sizes.emplace(file->GetPath(), file->GetSizePhysicalRaw());
        return sizes;
    }

    std::set<std::wstring> Roots(CItem* root)
    {
        if (!root->IsTypeOrFlag(IT_MYCOMPUTER)) return { root->GetPath() };
        std::set<std::wstring> paths;
        for (const auto* child : root->GetChildren()) paths.insert(child->GetPath());
        return paths;
    }

    INT_PTR CALLBACK GrowthDialog(HWND window, UINT message, WPARAM command, LPARAM param)
    {
        if (message == WM_INITDIALOG)
        {
            const auto* report = reinterpret_cast<const Report*>(param);
            SetWindowLongPtrW(window, DWLP_USER, param);
            SetDlgItemTextW(window, IDC_GROWTH_SUMMARY, report->summary.c_str());
            const HWND list = GetDlgItem(window, IDC_GROWTH_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            RECT bounds{};
            GetClientRect(list, &bounds);
            int column = 0;
            for (auto title : { L"文件路径", L"占用变化", L"基准占用", L"当前占用" })
            {
                LVCOLUMNW item{ .mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT };
                item.cx = (bounds.right - GetSystemMetrics(SM_CXVSCROLL)) * (column == 0 ? 49 : 17) / 100;
                item.fmt = column == 0 ? LVCFMT_LEFT : LVCFMT_RIGHT;
                item.pszText = const_cast<LPWSTR>(title);
                ListView_InsertColumn(list, column, &item);
                ++column;
            }
            ListView_SetItemCount(list, static_cast<int>(report->rows.size()));
            DarkMode::AdjustControls(window);
            return TRUE;
        }
        if (message == WM_NOTIFY)
        {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(param);
            if (info->hdr.idFrom != IDC_GROWTH_LIST || info->hdr.code != LVN_GETDISPINFOW) return FALSE;
            const auto* report = reinterpret_cast<const Report*>(GetWindowLongPtrW(window, DWLP_USER));
            if (!(info->item.mask & LVIF_TEXT) || static_cast<size_t>(info->item.iItem) >= report->rows.size()) return FALSE;
            const auto& row = report->rows[info->item.iItem];
            std::wstring value;
            switch (info->item.iSubItem)
            {
            case 0: value = row.path; break;
            case 1: value = (row.Growing() ? L"+" : L"−") + FormatBytes(row.Difference()); break;
            case 2: value = FormatBytes(row.before); break;
            case 3: value = FormatBytes(row.after); break;
            }
            lstrcpynW(info->item.pszText, value.c_str(), info->item.cchTextMax);
            return TRUE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && (LOWORD(command) == IDOK || LOWORD(command) == IDCANCEL)))
        {
            EndDialog(window, IDOK);
            return TRUE;
        }
        return FALSE;
    }
}

void CompareSnapshot(CItem* current, const std::wstring& baselinePath)
{
    using namespace snapshot;
    Report report;
    std::wstring error;
    CProgressDlg(0, CProgressDlg::Flags::NoCancel, GetMainWindow(), [&](CProgressDlg*)
    {
        const std::unique_ptr<CItem> baseline(LoadResults(baselinePath));
        if (!baseline) { error = L"无法读取基准快照。请选择已保存的扫描报告。"; return; }
        if (Roots(current) != Roots(baseline.get())) { error = L"两次扫描的根路径不同，请选择相同扫描范围的快照。"; return; }
        if (!current->HasSizePhysical()) { error = L"此扫描范围没有完整的磁盘占用数据。"; return; }
        report.rows = CompareFiles(ReadFiles(baseline.get()), ReadFiles(current));
    }).ShowModal();
    if (!error.empty()) { DisplayError(error); return; }
    report.summary = std::format(L"{} 个文件占用发生变化  ·  基准快照  {}", report.rows.size(), baselinePath);
    DialogBoxParamW(GetAppInstance(), MAKEINTRESOURCEW(IDD_GROWTH), GetMainWindowHandle(),
        GrowthDialog, reinterpret_cast<LPARAM>(&report));
}
