#pragma once

#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <memory>
#include <string>

std::unique_ptr<Gdiplus::Bitmap> LoadThumbnail(const std::wstring& url);
