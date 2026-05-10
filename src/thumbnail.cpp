#include "thumbnail.hpp"

#include "utils.hpp"

#include <windows.h>
#include <winhttp.h>
#include <objidl.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

struct HttpHandle {
    HINTERNET value{};

    explicit HttpHandle(HINTERNET handle = nullptr) : value(handle) {}

    ~HttpHandle() {
        if (value) {
            WinHttpCloseHandle(value);
        }
    }

    operator HINTERNET() const {
        return value;
    }
};

std::wstring ExtractVideoIdAt(const std::wstring& url, size_t start) {
    std::wstring id;
    for (size_t i = start; i < url.size(); ++i) {
        wchar_t ch = url[i];
        bool allowed = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-';
        if (!allowed) {
            break;
        }
        id.push_back(ch);
    }
    if (id.size() >= 11) {
        id.resize(11);
        return id;
    }
    return {};
}

std::wstring YoutubeVideoId(const std::wstring& url) {
    std::wstring lower = LowerCopy(url);
    const std::vector<std::wstring> segmentPatterns = {
        L"youtu.be/",
        L"/shorts/",
        L"/embed/",
        L"/live/"
    };
    for (const auto& pattern : segmentPatterns) {
        size_t pos = lower.find(pattern);
        if (pos != std::wstring::npos) {
            return ExtractVideoIdAt(url, pos + pattern.size());
        }
    }
    size_t query = lower.find(L"v=");
    if (query != std::wstring::npos) {
        return ExtractVideoIdAt(url, query + 2);
    }
    return {};
}

std::wstring ThumbnailUrlFor(const std::wstring& url) {
    std::wstring id = YoutubeVideoId(url);
    if (id.empty()) {
        return {};
    }
    return L"https://img.youtube.com/vi/" + id + L"/hqdefault.jpg";
}

std::vector<unsigned char> DownloadBytes(const std::wstring& url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        return {};
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    HttpHandle session(WinHttpOpen(L"BlackFix VideoShuffle/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return {};
    }

    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);
    HttpHandle connection(WinHttpConnect(session, host.c_str(), components.nPort, 0));
    if (!connection) {
        return {};
    }

    DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        return {};
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        return {};
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    if (status >= 400) {
        return {};
    }

    std::vector<unsigned char> bytes;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }
        size_t offset = bytes.size();
        bytes.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data() + offset, available, &read)) {
            return {};
        }
        bytes.resize(offset + read);
    }
    return bytes;
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromBytes(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) {
        return {};
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) {
        return {};
    }

    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return {};
    }
    std::memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return {};
    }

    Gdiplus::Bitmap image(stream);
    std::unique_ptr<Gdiplus::Bitmap> result;
    if (image.GetLastStatus() == Gdiplus::Ok && image.GetWidth() > 0 && image.GetHeight() > 0) {
        Gdiplus::Bitmap* clone = image.Clone(0, 0, image.GetWidth(), image.GetHeight(), PixelFormat32bppARGB);
        if (clone && clone->GetLastStatus() == Gdiplus::Ok) {
            result.reset(clone);
        } else {
            delete clone;
        }
    }

    stream->Release();
    return result;
}

}

std::unique_ptr<Gdiplus::Bitmap> LoadThumbnail(const std::wstring& url) {
    std::wstring imageUrl = ThumbnailUrlFor(url);
    if (imageUrl.empty()) {
        return {};
    }
    return BitmapFromBytes(DownloadBytes(imageUrl));
}
