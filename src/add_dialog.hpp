#pragma once

#include <windows.h>

#include <string>

struct AddResult {
    bool accepted{};
    std::wstring title;
    std::wstring url;
};

void RegisterAddDialogClass(HINSTANCE instance);
AddResult ShowAddDialog(HINSTANCE instance, HWND parent);
