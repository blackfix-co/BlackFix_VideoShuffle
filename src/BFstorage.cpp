#include "BFstorage.hpp"

#include "BFutils.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path LinksPath() {
    return AppDirectory() / L"links.tsv";
}

std::filesystem::path TagsPath() {
    return AppDirectory() / L"tags.tsv";
}

std::filesystem::path WindowPath() {
    return AppDirectory() / L"window.tsv";
}

}

std::filesystem::path AppDirectory() {
    PWSTR rawPath = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &rawPath))) {
        path = rawPath;
        CoTaskMemFree(rawPath);
    } else {
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        path = buffer;
    }
    path /= L"BlackFix_VideoShuffle";
    std::filesystem::create_directories(path);
    return path;
}

std::vector<std::wstring> LoadTags() {
    std::vector<std::wstring> tags;
    std::ifstream file(TagsPath(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        std::wstring tag = Sanitized(FromUtf8(line));
        if (!tag.empty() && tag != L"전체" && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
    }
    return tags;
}

void SaveTags(const std::vector<std::wstring>& tags) {
    std::ofstream file(TagsPath(), std::ios::binary | std::ios::trunc);
    for (const auto& tag : tags) {
        std::wstring clean = Sanitized(tag);
        if (!clean.empty() && clean != L"전체") {
            file << ToUtf8(clean) << '\n';
        }
    }
}

std::vector<LinkEntry> LoadLinks() {
    std::vector<LinkEntry> entries;
    std::ifstream file(LinksPath(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find('\t');
        if (first == std::string::npos) {
            continue;
        }
        size_t second = line.find('\t', first + 1);
        LinkEntry entry;
        entry.title = Sanitized(FromUtf8(std::string_view(line.data(), first)));
        if (second == std::string::npos) {
            entry.url = Sanitized(FromUtf8(std::string_view(line.data() + first + 1, line.size() - first - 1)));
            entry.tag = L"전체";
        } else {
            entry.url = Sanitized(FromUtf8(std::string_view(line.data() + first + 1, second - first - 1)));
            size_t third = line.find('\t', second + 1);
            size_t tagLength = third == std::string::npos ? line.size() - second - 1 : third - second - 1;
            entry.tag = Sanitized(FromUtf8(std::string_view(line.data() + second + 1, tagLength)));
            if (entry.tag.empty()) {
                entry.tag = L"전체";
            }
        }
        if (!entry.url.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

void SaveLinks(const std::vector<LinkEntry>& entries) {
    std::ofstream file(LinksPath(), std::ios::binary | std::ios::trunc);
    for (const auto& entry : entries) {
        file << ToUtf8(Sanitized(entry.title)) << '\t' << ToUtf8(Sanitized(entry.url)) << '\t' << ToUtf8(Sanitized(entry.tag.empty() ? L"전체" : entry.tag)) << '\n';
    }
}

WindowSettings LoadWindowSettings() {
    std::ifstream file(WindowPath(), std::ios::binary);
    WindowSettings settings;
    if (!(file >> settings.x >> settings.y >> settings.width >> settings.height)) {
        return {};
    }
    settings.width = std::clamp(settings.width, 280, 1200);
    settings.height = std::clamp(settings.height, 180, 900);
    settings.valid = true;
    return settings;
}

void SaveWindowSettings(const WindowSettings& settings) {
    std::ofstream file(WindowPath(), std::ios::binary | std::ios::trunc);
    file << settings.x << '\t' << settings.y << '\t' << settings.width << '\t' << settings.height << '\n';
}
