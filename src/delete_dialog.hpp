#pragma once

#include "types.hpp"

#include <windows.h>

#include <string>
#include <vector>

struct DeleteResult {
    std::vector<size_t> indices;
    std::vector<std::wstring> deletedTags;
};

DeleteResult ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::vector<std::wstring>& tags, const std::wstring& activeTag);
