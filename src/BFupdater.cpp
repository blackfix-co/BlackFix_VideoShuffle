#include "BFupdater.hpp"

#include "BFhttp.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cwctype>

namespace {

constexpr wchar_t kReleaseExeUrl[] = L"https://github.com/blackfix-co/BlackFix_VideoShuffle/releases/download/BlackFix_VideoShuffle/BlackFix_VideoShuffle.exe";

std::filesystem::path CurrentExePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(size);
    return path;
}

bool IsDevelopmentPath(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value.find(L"\\build\\") != std::wstring::npos;
}

std::vector<unsigned char> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file) {
        return {};
    }
    return bytes;
}

bool WriteFileBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::wstring PowerShellQuote(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'') {
            quoted += L"''";
        } else {
            quoted += ch;
        }
    }
    quoted += L"'";
    return quoted;
}

bool LaunchUpdater(const std::filesystem::path& source, const std::filesystem::path& destination) {
    DWORD pid = GetCurrentProcessId();
    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command ";
    std::wstring script = L"Wait-Process -Id " + std::to_wstring(pid) + L" -ErrorAction SilentlyContinue;";
    script += L"Copy-Item -LiteralPath " + PowerShellQuote(source.wstring()) + L" -Destination " + PowerShellQuote(destination.wstring()) + L" -Force;";
    script += L"Start-Process -FilePath " + PowerShellQuote(destination.wstring()) + L";";
    script += L"Remove-Item -LiteralPath " + PowerShellQuote(source.wstring()) + L" -Force -ErrorAction SilentlyContinue";
    command += L"\"" + script + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (ok) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return ok == TRUE;
}

}

bool CheckForUpdateAndRestart() {
    try {
        std::filesystem::path exePath = CurrentExePath();
        if (IsDevelopmentPath(exePath)) {
            return false;
        }

        std::vector<unsigned char> remote = DownloadBytes(kReleaseExeUrl);
        if (remote.empty()) {
            return false;
        }

        std::vector<unsigned char> local = ReadFileBytes(exePath);
        if (!local.empty() && local == remote) {
            return false;
        }

        std::filesystem::path updatePath = std::filesystem::temp_directory_path() / L"BlackFix_VideoShuffle.update.exe";
        if (!WriteFileBytes(updatePath, remote)) {
            return false;
        }

        return LaunchUpdater(updatePath, exePath);
    } catch (...) {
        return false;
    }
}
