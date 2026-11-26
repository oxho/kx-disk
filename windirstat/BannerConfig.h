// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The ad service returns two top-level fields and an array of string-valued placements.
namespace studio
{
    using PlacementFields = std::map<std::wstring, std::wstring>;

    class BannerConfigReader
    {
    public:
        explicit BannerConfigReader(std::wstring_view input) : m_input(input) {}

        std::optional<PlacementFields> Read()
        {
            std::wstring app;
            std::vector<PlacementFields> placements;
            bool seenApp = false, seenPlacements = false;
            if (!Take(L'{')) return {};
            do
            {
                std::wstring key;
                if (!String(key) || !Take(L':')) return {};
                if (key == L"app_id" && !seenApp)
                {
                    seenApp = true;
                    if (!String(app)) return {};
                }
                else if (key == L"placements" && !seenPlacements)
                {
                    seenPlacements = true;
                    if (!Array(placements)) return {};
                }
                else return {};
            } while (Take(L','));
            if (!Take(L'}') || !AtEnd() || app != L"2dbcd68cb497") return {};
            for (auto& fields : placements)
                if (fields[L"slot"] == L"scan-result" && fields[L"kind"] == L"banner" &&
                    !fields[L"title"].empty() && !fields[L"target"].empty() && fields.contains(L"asset"))
                    return std::move(fields);
            return {};
        }

    private:
        std::wstring_view m_input;

        bool AtEnd()
        {
            const auto next = m_input.find_first_not_of(L" \t\r\n");
            m_input.remove_prefix(next == m_input.npos ? m_input.size() : next);
            return m_input.empty();
        }

        bool Take(wchar_t c)
        {
            if (AtEnd() || m_input.front() != c) return false;
            m_input.remove_prefix(1);
            return true;
        }

        bool String(std::wstring& text)
        {
            if (!Take(L'"')) return false;
            while (!m_input.empty())
            {
                wchar_t c = m_input.front();
                m_input.remove_prefix(1);
                if (c == L'"') return true;
                if (c < 0x20) return false;
                if (c == L'\\')
                {
                    if (m_input.empty()) return false;
                    c = m_input.front();
                    m_input.remove_prefix(1);
                    constexpr std::wstring_view escapes = L"\"\\/bfnrt";
                    const auto index = escapes.find(c);
                    if (index != escapes.npos) c = L"\"\\/\b\f\n\r\t"[index];
                    else if (c == L'u' && m_input.size() >= 4)
                    {
                        unsigned code = 0;
                        for (const wchar_t digit : m_input.substr(0, 4))
                        {
                            const auto n = std::wstring_view(L"0123456789abcdef").find(
                                digit >= L'A' && digit <= L'F' ? wchar_t(digit + 32) : digit);
                            if (n == m_input.npos) return false;
                            code = code * 16 + static_cast<unsigned>(n);
                        }
                        if (!code) return false;
                        c = static_cast<wchar_t>(code);
                        m_input.remove_prefix(4);
                    }
                    else return false;
                }
                text += c;
            }
            return false;
        }

        bool Array(std::vector<PlacementFields>& rows)
        {
            if (!Take(L'[')) return false;
            if (Take(L']')) return true;
            do
            {
                if (!Take(L'{')) return false;
                auto& row = rows.emplace_back();
                if (Take(L'}')) continue;
                do
                {
                    std::wstring key, value;
                    if (!String(key) || !Take(L':') || !String(value) ||
                        !row.emplace(std::move(key), std::move(value)).second) return false;
                } while (Take(L','));
                if (!Take(L'}')) return false;
            } while (Take(L','));
            return Take(L']');
        }
    };
}
