#pragma once

#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <memory>
#include <string>
#include <vector>

struct RectI {
    int x{};
    int y{};
    int w{};
    int h{};

    bool contains(POINT point) const {
        return point.x >= x && point.x < x + w && point.y >= y && point.y < y + h;
    }
};

struct WindowSettings {
    int x{};
    int y{};
    int width{460};
    int height{300};
    bool valid{};
};

struct LinkEntry {
    std::wstring title;
    std::wstring url;
    std::wstring tag{L"전체"};
    std::unique_ptr<Gdiplus::Bitmap> thumbnail;
};

struct CardLayout {
    size_t entryIndex{};
    RectI rect;
    bool active{};
};

struct Layout {
    RectI tagButton;
    RectI addButton;
    RectI moveHandle;
    RectI resizeHandle;
    RectI deleteButton;
    int visibleSlots{};
    std::vector<CardLayout> cards;
};
