#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct AddResult {
    bool accepted{};
    std::wstring title;
    std::wstring url;
    std::wstring tag{L"전체"};
};

struct VideoDialogInput {
    bool editing{};
    std::wstring title;
    std::wstring url;
    std::wstring tag{L"전체"};
};

void RegisterAddDialogClass(HINSTANCE instance);
AddResult ShowVideoDialog(HINSTANCE instance, HWND parent, const std::vector<std::wstring>& tags, const VideoDialogInput& input = {});
