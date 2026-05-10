#include "thumbnail.hpp"

#include "http.hpp"
#include "utils.hpp"

#include <windows.h>
#include <objidl.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

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
