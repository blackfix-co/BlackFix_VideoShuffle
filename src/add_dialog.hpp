#pragma once

#include "types.hpp"

#include <windows.h>

#include <string>
#include <vector>

struct AddResult {
    bool accepted{};
    std::wstring title;
    std::wstring url;
    std::wstring tag{L"전체"};
    std::vector<size_t> tagAssignments;
};

struct VideoDialogInput {
    bool editing{};
    std::wstring title;
    std::wstring url;
    std::wstring tag{L"전체"};
};

void RegisterAddDialogClass(HINSTANCE instance);
AddResult ShowVideoDialog(HINSTANCE instance, HWND parent, const std::vector<std::wstring>& tags, const std::vector<LinkEntry>& entries, const VideoDialogInput& input = {});
