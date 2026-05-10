#include "storage.hpp"

#include "utils.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path LinksPath() {
    return AppDirectory() / L"links.tsv";
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

std::vector<LinkEntry> LoadLinks() {
    std::vector<LinkEntry> entries;
    std::ifstream file(LinksPath(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        LinkEntry entry;
        entry.title = Sanitized(FromUtf8(std::string_view(line.data(), tab)));
        entry.url = Sanitized(FromUtf8(std::string_view(line.data() + tab + 1, line.size() - tab - 1)));
        if (!entry.url.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

void SaveLinks(const std::vector<LinkEntry>& entries) {
    std::ofstream file(LinksPath(), std::ios::binary | std::ios::trunc);
    for (const auto& entry : entries) {
        file << ToUtf8(Sanitized(entry.title)) << '\t' << ToUtf8(Sanitized(entry.url)) << '\n';
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
