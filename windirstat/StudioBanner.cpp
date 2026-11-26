// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <sensapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include "Version.h"
#pragma comment(lib, "windowsapp.lib")
using namespace winrt::Windows::Data::Json;

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "sensapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace
{
    bool SplitHttps(const std::wstring& url, URL_COMPONENTS& parts)
    {
        if (!url.starts_with(L"https://") || url.find_first_of(L"\r\n\t \\") != url.npos) return false;
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
        return WinHttpCrackUrl(url.c_str(), 0, 0, &parts) && parts.nScheme == INTERNET_SCHEME_HTTPS &&
            parts.dwHostNameLength && !parts.dwUserNameLength && !parts.dwPasswordLength;
    }

    std::vector<BYTE> Download(HINTERNET session, const std::wstring& url, size_t limit, DWORD* statusOut = nullptr)
    {
        URL_COMPONENTS parts{};
        if (!SplitHttps(url, parts)) return {};
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        SmartPointer connection(WinHttpCloseHandle, WinHttpConnect(session, host.c_str(), parts.nPort, 0));
        if (!connection) return {};
        SmartPointer request(WinHttpCloseHandle, WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        if (!request || !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) return {};
        DWORD status{}, length = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
            &status, &length, nullptr)) return {};
        if (statusOut) *statusOut = status;
        if (status != 200) return {};
        std::vector<BYTE> bytes;
        std::array<BYTE, 8192> block;
        DWORD count{};
        const auto deadline = GetTickCount64() + 8000;
        while (GetTickCount64() < deadline && WinHttpReadData(request, block.data(), static_cast<DWORD>(block.size()), &count))
        {
            if (!count) return bytes;
            if (bytes.size() + count > limit) return {};
            bytes.insert(bytes.end(), block.begin(), block.begin() + count);
        }
        return {};
    }

    HBITMAP Thumbnail(const std::vector<BYTE>& data, int edge)
    {
        CComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(data.data(), static_cast<UINT>(data.size())));
        if (!stream) return nullptr;
        Gdiplus::Bitmap original(stream);
        const UINT w = original.GetWidth(), h = original.GetHeight();
        if (!w || !h || uint64_t(w) * h > 16000000) return nullptr;
        Gdiplus::Bitmap canvas(edge, edge);
        {
            Gdiplus::Graphics graphics(&canvas);
            const double scale = double(edge) / std::max(w, h);
            const int width = std::max(1, int(w * scale)), height = std::max(1, int(h * scale));
            if (graphics.DrawImage(&original, (edge-width)/2, (edge-height)/2, width, height) != Gdiplus::Ok) return nullptr;
        }
        HBITMAP bitmap{};
        canvas.GetHBITMAP(Gdiplus::Color(0, 63, 65), &bitmap);
        return bitmap;
    }
}

StudioBanner::Placement StudioBanner::Fetch()
{
    SmartPointer session(WinHttpCloseHandle, WinHttpOpen(L"KxDisk/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session, 2500, 2500, 2500, 3000)) return {};
    DWORD status{};
    wchar_t test[2]{};
    const bool testing = GetEnvironmentVariableW(L"KX_AD_TEST", test, 2) == 1 && test[0] == L'1';
    auto endpoint = std::format(L"https://8.137.20.181/v2/apps/2dbcd68cb497/ads?app_version={}.{}.{}&platform=Windows{}", PRD_MAJVER, PRD_MINVER, PRD_PATCH, testing ? L"&test=1" : L"");
    auto data = Download(session, endpoint, 65536, &status);
    if (status == 404 && !testing) data = Download(session, L"https://8.137.20.181/v1/apps/2dbcd68cb497/ads", 65536);
    if (data.empty()) return {};
    const auto input = reinterpret_cast<const char*>(data.data());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, static_cast<int>(data.size()), nullptr, 0);
    if (!length) return {};
    std::wstring json(length, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, static_cast<int>(data.size()), json.data(), length)) return {};
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct ApartmentGuard { ~ApartmentGuard(){ winrt::uninit_apartment(); } } guard;
    const auto config = JsonObject::Parse(json);
    if (config.GetNamedString(L"app_id") != L"2dbcd68cb497") return {};
    Placement result;
    std::wstring asset;
    for (const auto& row : config.GetNamedArray(L"placements"))
    {
        const auto ad = row.GetObject();
        if (ad.GetNamedString(L"slot") != L"scan-result" || ad.GetNamedString(L"kind") != L"banner") continue;
        result.title = ad.GetNamedString(L"title");
        result.target = ad.GetNamedString(L"target");
        asset = ad.GetNamedString(L"asset", L"");
        result.creative = ad.GetNamedString(L"creative_id", L"");
        result.campaign = ad.GetNamedString(L"campaign_id", L"");
        if (ad.HasKey(L"creative_version")) {
            const auto value = ad.GetNamedValue(L"creative_version");
            result.version = value.ValueType() == JsonValueType::String ? value.GetString() : value.Stringify();
        }
        result.test = ad.GetNamedBoolean(L"is_test", false);
        break;
    }
    if (result.title.empty()) return {};
    URL_COMPONENTS target{};
    if (!SplitHttps(result.target, target)) return {};
    if (!asset.empty())
    {
        result.asset = Download(session, asset, 2 * 1024 * 1024);
        if (result.asset.empty()) return {};
    }
    return result;
}

namespace {
    std::wstring NewEventId() {
        GUID id{};
        if (FAILED(CoCreateGuid(&id))) return {};
        wchar_t text[39]{};
        StringFromGUID2(id, text, 39);
        return std::wstring(text + 1, 36);
    }
}
void StudioBanner::Report(const wchar_t* type) const
{
    if (m_creative.empty() || m_campaign.empty() || m_version.empty() || m_exposure.empty()) return;
    const auto send = [creative=m_creative, campaign=m_campaign, version=m_version, exposure=m_exposure, test=m_test, type] {
        try {
            winrt::init_apartment();
            struct Guard { ~Guard(){winrt::uninit_apartment();} } guard;
            JsonObject e;
            auto put = [&](const wchar_t* key, const std::wstring& value){ e.Insert(key, JsonValue::CreateStringValue(value)); };
            put(L"event_id", NewEventId()); put(L"exposure_id",exposure); put(L"event_type",type);
            put(L"slot",L"scan-result"); put(L"creative_id",creative); put(L"creative_version",version);
            put(L"campaign_id",campaign); put(L"platform",L"Windows");
            put(L"app_version",std::format(L"{}.{}.{}",PRD_MAJVER,PRD_MINVER,PRD_PATCH));
            e.Insert(L"is_test",JsonValue::CreateBooleanValue(test));
            auto bytes=winrt::to_string(e.Stringify());
            SmartPointer session(WinHttpCloseHandle, WinHttpOpen(L"KxDisk", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,nullptr,nullptr,0));
            if (!session || !WinHttpSetTimeouts(session,2500,2500,2500,3000)) return;
            SmartPointer con(WinHttpCloseHandle, WinHttpConnect(session,L"8.137.20.181",443,0));
            if (!con) return;
            for (int attempt=0; attempt < 2; ++attempt) {
            SmartPointer req(WinHttpCloseHandle, WinHttpOpenRequest(con,L"POST",L"/v2/apps/2dbcd68cb497/events",nullptr,nullptr,nullptr,WINHTTP_FLAG_SECURE));
            DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
            if (req && WinHttpSetOption(req,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) && WinHttpSendRequest(req,L"Content-Type: application/json",DWORD(-1),bytes.data(),static_cast<DWORD>(bytes.size()),static_cast<DWORD>(bytes.size()),0))
                if (WinHttpReceiveResponse(req,nullptr)) {
                    DWORD code{}, len=sizeof(code);
                    if (WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&len,nullptr) && code < 500 && code != 429) break;
                }
            if (attempt==0) Sleep(750);
            }
        } catch (...) {}
    };
    try { std::thread(send).detach(); } catch (...) {}
}

void StudioBanner::Hide(CMainFrame& frame)
{
    if (m_target.empty()) return;
    m_target.clear();
    m_exposure.clear();
    m_visibleSince = 0;
    for (const HWND control : {m_title, m_close, m_image}) ::ShowWindow(control, SW_HIDE);
    frame.UpdateLayout();
}

void StudioBanner::Dismiss(CMainFrame& frame)
{
    Report(L"dismiss");
    m_dismissed = true;
    Hide(frame);
}

void StudioBanner::Open(HWND owner) const
{
    if (!m_target.empty()) Report(L"click");
    if (!m_target.empty()) ShellExecuteW(owner, L"open", m_target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int StudioBanner::Layout(int width, int bottom) const
{
    if (m_target.empty()) return bottom;
    const int height = ScaleForDpi(36), closeWidth = std::min(width, ScaleForDpi(64));
    const int imageWidth = m_bitmap ? height : 0;
    const int top = std::max(0, bottom - height);
    MoveWindow(m_image, 0, top, imageWidth, height, TRUE);
    MoveWindow(m_title, imageWidth, top, std::max(0, width-imageWidth-closeWidth), height, TRUE);
    MoveWindow(m_close, width-closeWidth, top, closeWidth, height, TRUE);
    return top;
}

void StudioBanner::Refresh(CMainFrame& frame, bool eligible)
{
    const auto now = GetTickCount64();
    if (eligible && !m_dismissed && now >= m_nextNetworkCheck)
    {
        DWORD flags{};
        m_online = IsNetworkAlive(&flags) || GetLastError() != ERROR_SUCCESS;
        m_nextNetworkCheck = now + 1000;
    }
    if (!eligible || !m_online || m_dismissed)
    {
        m_discard = true;
        m_nextCheck = 0;
        Hide(frame);
        return;
    }
    if (!m_impressed && !m_exposure.empty()) {
        RECT banner{}, window{}, clipped{};
        GetWindowRect(m_title,&banner); GetWindowRect(frame.Handle(),&window);
        const auto area = [](RECT r){return int64_t(r.right-r.left)*(r.bottom-r.top);};
        if (GetForegroundWindow() != frame.Handle() || !IsWindowVisible(m_title) || IsIconic(frame.Handle()) || area(banner)<=0 ||
            !IntersectRect(&clipped,&banner,&window) || area(clipped)*2 < area(banner)) m_visibleSince=0;
        else if (!m_visibleSince) m_visibleSince=now;
        else if (now-m_visibleSince>=1000) { m_impressed=true; Report(L"impression"); }
    }
    if (m_request.valid())
    {
        if (m_request.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        Placement placement;
        try { placement = m_request.get(); } catch (...) {}
        if (m_discard || placement.target.empty()) { Hide(frame); return; }
        if (!m_title)
        {
            auto create = [&](LPCWSTR type, LPCWSTR label, DWORD style, UINT_PTR id)
            {
                return CreateWindowExW(0, type, label, WS_CHILD | style, 0, 0, 0, 0,
                    frame.Handle(), reinterpret_cast<HMENU>(id), GetAppInstance(), nullptr);
            };
            m_title = create(WC_BUTTON, nullptr, WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT, ID_STUDIO_OPEN);
            m_close = create(WC_BUTTON, L"关闭", WS_TABSTOP | BS_PUSHBUTTON, ID_STUDIO_DISMISS);
            m_image = create(WC_STATIC, nullptr, SS_BITMAP | SS_CENTERIMAGE, 0);
            DarkMode::AdjustControls(frame.Handle());
            for (const HWND button : {m_title, m_close}) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetAppFont(frame.Handle())), TRUE);
        }
        if (!m_title || !m_close || !m_image) return;
        HBITMAP next = placement.asset.empty() ? nullptr : Thumbnail(placement.asset, ScaleForDpi(36));
        if (!placement.asset.empty() && !next) { Hide(frame); return; }
        const HBITMAP old = reinterpret_cast<HBITMAP>(SendMessageW(m_image, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(next)));
        if (old && old != m_bitmap.get()) DeleteObject(old);
        m_bitmap.reset(next);
        if (m_exposure.empty() || m_creative != placement.creative || m_version != placement.version || m_campaign != placement.campaign || m_test != placement.test) {
            m_exposure=NewEventId(); m_impressed=false; m_visibleSince=0;
        }
        m_creative=placement.creative; m_campaign=placement.campaign; m_version=placement.version; m_test=placement.test;
        m_target = std::move(placement.target);
        SetWindowTextW(m_title, (L"开枝工作室广告  ·  " + placement.title).c_str());
        for (const HWND button : {m_title, m_close}) ShowWindow(button, SW_SHOWNA);
        ShowWindow(m_image, next ? SW_SHOWNA : SW_HIDE);
        frame.UpdateLayout();
    }
    if (GetTickCount64() >= m_nextCheck)
    {
        m_discard = false;
        m_nextCheck = GetTickCount64() + 30000;
        m_request = std::async(std::launch::async, Fetch);
    }
}
