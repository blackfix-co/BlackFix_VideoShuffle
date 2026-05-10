#include "link_title.hpp"

#include "http.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <vector>

namespace {

bool IsNameBoundary(wchar_t ch) {
    return !iswalnum(ch) && ch != L'-' && ch != L'_';
}

std::wstring CollapseSpaces(std::wstring value) {
    bool inSpace = false;
    std::wstring output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        bool space = ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == 0x00A0;
        if (space) {
            if (!inSpace) {
                output.push_back(L' ');
            }
            inSpace = true;
        } else {
            output.push_back(ch);
            inSpace = false;
        }
    }
    return Sanitized(output);
}

int HexValue(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return ch - L'a' + 10;
    }
    if (ch >= L'A' && ch <= L'F') {
        return ch - L'A' + 10;
    }
    return -1;
}

std::wstring HtmlDecoded(const std::wstring& value) {
    std::wstring output;
    output.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != L'&') {
            output.push_back(value[i]);
            continue;
        }

        size_t end = value.find(L';', i + 1);
        if (end == std::wstring::npos || end - i > 12) {
            output.push_back(value[i]);
            continue;
        }

        std::wstring entity = value.substr(i + 1, end - i - 1);
        std::wstring lower = LowerCopy(entity);
        if (lower == L"amp") {
            output.push_back(L'&');
        } else if (lower == L"quot") {
            output.push_back(L'"');
        } else if (lower == L"apos" || lower == L"#39") {
            output.push_back(L'\'');
        } else if (lower == L"lt") {
            output.push_back(L'<');
        } else if (lower == L"gt") {
            output.push_back(L'>');
        } else if (lower == L"nbsp") {
            output.push_back(L' ');
        } else if (!lower.empty() && lower[0] == L'#') {
            int base = 10;
            size_t start = 1;
            if (lower.size() > 2 && lower[1] == L'x') {
                base = 16;
                start = 2;
            }
            int code = 0;
            bool valid = start < lower.size();
            for (size_t j = start; j < lower.size(); ++j) {
                int digit = base == 16 ? HexValue(lower[j]) : (iswdigit(lower[j]) ? lower[j] - L'0' : -1);
                if (digit < 0 || digit >= base) {
                    valid = false;
                    break;
                }
                code = code * base + digit;
            }
            if (valid && code > 0 && code <= 0xFFFF) {
                output.push_back(static_cast<wchar_t>(code));
            } else {
                output.append(value, i, end - i + 1);
            }
        } else {
            output.append(value, i, end - i + 1);
        }
        i = end;
    }
    return CollapseSpaces(output);
}

std::wstring AttributeValue(const std::wstring& tag, const std::wstring& lowerTag, const std::wstring& name) {
    size_t pos = 0;
    while ((pos = lowerTag.find(name, pos)) != std::wstring::npos) {
        bool left = pos == 0 || IsNameBoundary(lowerTag[pos - 1]);
        size_t cursor = pos + name.size();
        bool right = cursor >= lowerTag.size() || IsNameBoundary(lowerTag[cursor]) || lowerTag[cursor] == L'=';
        if (!left || !right) {
            pos = cursor;
            continue;
        }

        while (cursor < lowerTag.size() && iswspace(lowerTag[cursor])) {
            ++cursor;
        }
        if (cursor >= lowerTag.size() || lowerTag[cursor] != L'=') {
            pos = cursor;
            continue;
        }
        ++cursor;
        while (cursor < lowerTag.size() && iswspace(lowerTag[cursor])) {
            ++cursor;
        }
        if (cursor >= tag.size()) {
            return {};
        }

        wchar_t quote = tag[cursor] == L'"' || tag[cursor] == L'\'' ? tag[cursor++] : 0;
        size_t start = cursor;
        if (quote) {
            size_t end = tag.find(quote, start);
            return end == std::wstring::npos ? tag.substr(start) : tag.substr(start, end - start);
        }

        while (cursor < tag.size() && !iswspace(tag[cursor]) && tag[cursor] != L'>') {
            ++cursor;
        }
        return tag.substr(start, cursor - start);
    }
    return {};
}

bool IsTitleMeta(const std::wstring& tag, const std::wstring& lowerTag) {
    std::wstring property = LowerCopy(AttributeValue(tag, lowerTag, L"property"));
    std::wstring name = LowerCopy(AttributeValue(tag, lowerTag, L"name"));
    return property == L"og:title" || name == L"twitter:title" || name == L"title";
}

std::wstring MetaTitle(const std::wstring& html, const std::wstring& lower) {
    size_t pos = 0;
    while ((pos = lower.find(L"<meta", pos)) != std::wstring::npos) {
        size_t end = lower.find(L'>', pos);
        if (end == std::wstring::npos) {
            break;
        }
        std::wstring tag = html.substr(pos, end - pos + 1);
        std::wstring lowerTag = lower.substr(pos, end - pos + 1);
        if (IsTitleMeta(tag, lowerTag)) {
            std::wstring content = AttributeValue(tag, lowerTag, L"content");
            if (!content.empty()) {
                return HtmlDecoded(content);
            }
        }
        pos = end + 1;
    }
    return {};
}

std::wstring PageTitle(const std::wstring& html, const std::wstring& lower) {
    size_t startTag = lower.find(L"<title");
    if (startTag == std::wstring::npos) {
        return {};
    }
    size_t start = lower.find(L'>', startTag);
    if (start == std::wstring::npos) {
        return {};
    }
    size_t end = lower.find(L"</title>", start + 1);
    if (end == std::wstring::npos) {
        return {};
    }
    return HtmlDecoded(html.substr(start + 1, end - start - 1));
}

}

std::wstring LoadLinkTitle(const std::wstring& url) {
    std::vector<unsigned char> bytes = DownloadBytes(url, 768 * 1024);
    if (bytes.empty()) {
        return {};
    }

    size_t length = bytes.size();
    std::string htmlBytes(reinterpret_cast<const char*>(bytes.data()), length);
    std::wstring html = FromUtf8(std::string_view(htmlBytes.data(), htmlBytes.size()));
    if (html.empty()) {
        return {};
    }

    std::wstring lower = LowerCopy(html);
    std::wstring title = MetaTitle(html, lower);
    if (title.empty()) {
        title = PageTitle(html, lower);
    }
    return Sanitized(title);
}
