#pragma once

#include "types.hpp"

#include <windows.h>

#include <string>
#include <vector>

std::vector<size_t> ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::wstring& activeTag);
