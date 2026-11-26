// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CMainFrame;

class StudioBanner
{
public:
    void Refresh(CMainFrame& frame, bool eligible);
    int Layout(int width, int bottom) const;
    void Dismiss(CMainFrame& frame);
    void Open(HWND owner) const;

private:
    struct Placement
    {
        std::wstring title;
        std::wstring target;
        std::vector<BYTE> asset;
    };

    static Placement Fetch();
    void Hide(CMainFrame& frame);
    HWND m_title{}, m_close{}, m_image{};
    std::unique_ptr<std::remove_pointer_t<HBITMAP>, decltype(&DeleteObject)> m_bitmap{nullptr, DeleteObject};
    std::future<Placement> m_request;
    ULONGLONG m_nextCheck{}, m_nextNetworkCheck{};
    bool m_online = true;
    std::wstring m_target;
    bool m_dismissed{}, m_discard{};
};
