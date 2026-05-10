#pragma once

#include <cstddef>
#include <string>
#include <vector>

std::vector<unsigned char> DownloadBytes(const std::wstring& url, size_t maxBytes = 0);
