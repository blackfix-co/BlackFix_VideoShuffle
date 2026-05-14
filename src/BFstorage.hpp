#pragma once

#include "BFtypes.hpp"

#include <filesystem>
#include <vector>

std::filesystem::path AppDirectory();
std::vector<std::wstring> LoadTags();
void SaveTags(const std::vector<std::wstring>& tags);
std::vector<LinkEntry> LoadLinks();
void SaveLinks(const std::vector<LinkEntry>& entries);
WindowSettings LoadWindowSettings();
void SaveWindowSettings(const WindowSettings& settings);
