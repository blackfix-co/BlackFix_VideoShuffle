#pragma once

#include <string>
#include <string_view>

std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);
std::wstring Trimmed(std::wstring value);
std::wstring Sanitized(std::wstring value);
std::wstring LowerCopy(std::wstring value);
