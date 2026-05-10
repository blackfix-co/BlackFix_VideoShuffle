#pragma once

#include "types.hpp"

#include <filesystem>
#include <vector>

std::filesystem::path AppDirectory();
std::vector<LinkEntry> LoadLinks();
void SaveLinks(const std::vector<LinkEntry>& entries);
WindowSettings LoadWindowSettings();
void SaveWindowSettings(const WindowSettings& settings);
