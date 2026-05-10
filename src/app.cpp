#include "app.hpp"

#include "add_dialog.hpp"
#include "delete_dialog.hpp"
#include "storage.hpp"
#include "thumbnail.hpp"
#include "updater.hpp"
#include "utils.hpp"

#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>

using namespace Gdiplus;

namespace {

constexpr wchar_t kMainClassName[] = L"BlackFixVideoShuffleWindow";
constexpr wchar_t kAllTag[] = L"전체";

struct AppState {
    HINSTANCE instance{};
    HWND window{};
    ULONG_PTR gdiplusToken{};
    std::vector<LinkEntry> entries;
    WindowSettings settings;
    std::wstring activeTag{kAllTag};
    int scrollIndex{};
    bool mouseDown{};
    bool mouseMoved{};
    bool rendering{};
    POINT pressClient{};
    POINT pressScreen{};
    RECT pressWindow{};
    size_t pressEntryIndex{};
    int wheelAccum{};
    enum class PressTarget {
        None,
        Tag,
        Add,
        Delete,
        Card,
        Move,
        Resize
    } pressTarget{PressTarget::None};
};

AppState g_app;

Layout BuildLayout(int width, int height);
void ScrollBy(int delta);
void AddEntry(const AddResult& result);
void OpenUrl(const std::wstring& url);
void RenderWindow(HWND hwnd);
void RequestRender(HWND hwnd);

std::wstring CurrentExePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(size);
    return path;
}

void EnableStartup() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    std::wstring command = L"\"" + CurrentExePath() + L"\"";
    RegSetValueExW(key, L"BlackFix_VideoShuffle", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

WindowSettings DefaultWindowSettings() {
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    WindowSettings settings;
    settings.width = 460;
    settings.height = 300;
    settings.x = screenX + (screenW - settings.width) / 2;
    settings.y = screenY + (screenH - settings.height) / 2;
    settings.valid = true;
    return settings;
}

WindowSettings CurrentWindowSettings(HWND hwnd) {
    WindowSettings settings;
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return settings;
    }
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    settings.x = rect.left;
    settings.y = rect.top;
    settings.width = rect.right - rect.left;
    settings.height = rect.bottom - rect.top;
    settings.valid = true;
    return settings;
}

void RememberCurrentWindowSettings(HWND hwnd) {
    WindowSettings settings = CurrentWindowSettings(hwnd);
    if (settings.valid) {
        g_app.settings = settings;
    }
}

void SaveCurrentWindowSettings(HWND hwnd) {
    RememberCurrentWindowSettings(hwnd);
    if (g_app.settings.valid) {
        SaveWindowSettings(g_app.settings);
    }
}

RectI Expanded(RectI rect, int amount) {
    rect.x -= amount;
    rect.y -= amount;
    rect.w += amount * 2;
    rect.h += amount * 2;
    return rect;
}

bool ContainsExpanded(const RectI& rect, POINT point, int amount) {
    return Expanded(rect, amount).contains(point);
}

std::wstring NormalizedTag(std::wstring tag) {
    tag = Sanitized(tag);
    return tag.empty() ? kAllTag : tag;
}

std::vector<std::wstring> Tags() {
    std::vector<std::wstring> tags{kAllTag};
    for (const auto& entry : g_app.entries) {
        std::wstring tag = NormalizedTag(entry.tag);
        if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
    }
    return tags;
}

bool EntryInActiveTag(const LinkEntry& entry) {
    return g_app.activeTag == kAllTag || NormalizedTag(entry.tag) == g_app.activeTag;
}

std::vector<size_t> VisibleEntries() {
    std::vector<size_t> visible;
    for (size_t i = 0; i < g_app.entries.size(); ++i) {
        if (EntryInActiveTag(g_app.entries[i])) {
            visible.push_back(i);
        }
    }
    return visible;
}

int PositionInVisible(size_t entryIndex) {
    std::vector<size_t> visible = VisibleEntries();
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i] == entryIndex) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

AppState::PressTarget TargetAt(const Layout& layout, POINT point, size_t& entryIndex) {
    if (ContainsExpanded(layout.tagButton, point, 8)) {
        return AppState::PressTarget::Tag;
    }
    if (ContainsExpanded(layout.addButton, point, 10)) {
        return AppState::PressTarget::Add;
    }
    if (ContainsExpanded(layout.deleteButton, point, 10)) {
        return AppState::PressTarget::Delete;
    }
    if (ContainsExpanded(layout.moveHandle, point, 14)) {
        return AppState::PressTarget::Move;
    }
    if (ContainsExpanded(layout.resizeHandle, point, 16)) {
        return AppState::PressTarget::Resize;
    }
    for (const auto& card : layout.cards) {
        if (ContainsExpanded(card.rect, point, 8)) {
            entryIndex = card.entryIndex;
            return AppState::PressTarget::Card;
        }
    }
    return AppState::PressTarget::None;
}

bool TargetStillActive(const Layout& layout, POINT point, AppState::PressTarget target, size_t entryIndex) {
    switch (target) {
    case AppState::PressTarget::Tag:
        return ContainsExpanded(layout.tagButton, point, 8);
    case AppState::PressTarget::Add:
        return ContainsExpanded(layout.addButton, point, 10);
    case AppState::PressTarget::Delete:
        return ContainsExpanded(layout.deleteButton, point, 10);
    case AppState::PressTarget::Move:
        return ContainsExpanded(layout.moveHandle, point, 14);
    case AppState::PressTarget::Resize:
        return ContainsExpanded(layout.resizeHandle, point, 16);
    case AppState::PressTarget::Card:
        for (const auto& card : layout.cards) {
            if (card.entryIndex == entryIndex && ContainsExpanded(card.rect, point, 8)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

void ApplyCursorForTarget(AppState::PressTarget target) {
    if (target == AppState::PressTarget::Move) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
    } else if (target == AppState::PressTarget::Resize) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENESW));
    } else if (target == AppState::PressTarget::None) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    } else {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
    }
}

void MoveWindowFromDrag(HWND hwnd, POINT screenPoint) {
    int dx = screenPoint.x - g_app.pressScreen.x;
    int dy = screenPoint.y - g_app.pressScreen.y;
    if (std::abs(dx) > 2 || std::abs(dy) > 2) {
        g_app.mouseMoved = true;
    }
    int width = g_app.pressWindow.right - g_app.pressWindow.left;
    int height = g_app.pressWindow.bottom - g_app.pressWindow.top;
    SetWindowPos(hwnd, nullptr, g_app.pressWindow.left + dx, g_app.pressWindow.top + dy, width, height, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RequestRender(hwnd);
}

void ResizeWindowFromDrag(HWND hwnd, POINT screenPoint) {
    int dx = screenPoint.x - g_app.pressScreen.x;
    int dy = screenPoint.y - g_app.pressScreen.y;
    if (std::abs(dx) > 2 || std::abs(dy) > 2) {
        g_app.mouseMoved = true;
    }

    int originalWidth = g_app.pressWindow.right - g_app.pressWindow.left;
    int originalHeight = g_app.pressWindow.bottom - g_app.pressWindow.top;
    int minWidth = 280;
    int minHeight = 180;
    int newWidth = std::max(minWidth, originalWidth + dx);
    int newHeight = std::max(minHeight, originalHeight + dy);
    SetWindowPos(hwnd, nullptr, g_app.pressWindow.left, g_app.pressWindow.top, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    RequestRender(hwnd);
}

void DragScrollCards(POINT point) {
    int dx = point.x - g_app.pressClient.x;
    int dy = point.y - g_app.pressClient.y;
    if (std::abs(dx) < 42 || std::abs(dx) < std::abs(dy)) {
        return;
    }
    ScrollBy(dx < 0 ? 1 : -1);
    g_app.mouseMoved = true;
    g_app.pressClient = point;
}

void UpdateCursor(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    size_t entryIndex = 0;
    ApplyCursorForTarget(TargetAt(layout, point, entryIndex));
}

void StartPress(HWND hwnd, POINT point, POINT screenPoint) {
    RECT client{};
    GetClientRect(hwnd, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    g_app.mouseDown = true;
    g_app.mouseMoved = false;
    g_app.pressClient = point;
    g_app.pressScreen = screenPoint;
    g_app.pressEntryIndex = 0;
    g_app.pressTarget = TargetAt(layout, point, g_app.pressEntryIndex);
    GetWindowRect(hwnd, &g_app.pressWindow);
    SetCapture(hwnd);
    ApplyCursorForTarget(g_app.pressTarget);
}

void ShowTagMenu(HWND hwnd) {
    std::vector<std::wstring> tags = Tags();
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    constexpr UINT baseId = 3000;
    for (size_t i = 0; i < tags.size(); ++i) {
        UINT flags = MF_STRING;
        if (tags[i] == g_app.activeTag) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(menu, flags, baseId + static_cast<UINT>(i), tags[i].c_str());
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    POINT menuPoint{layout.tagButton.x, layout.tagButton.y + layout.tagButton.h + 4};
    ClientToScreen(hwnd, &menuPoint);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, menuPoint.x, menuPoint.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (command >= baseId && command < baseId + tags.size()) {
        g_app.activeTag = tags[command - baseId];
        g_app.scrollIndex = 0;
        RequestRender(hwnd);
    }
}

void DeleteFromDialog(HWND hwnd) {
    if (g_app.entries.empty()) {
        return;
    }

    size_t index = ShowDeleteDialog(g_app.instance, hwnd, g_app.entries, g_app.activeTag);
    if (index >= g_app.entries.size()) {
        return;
    }

    g_app.entries.erase(g_app.entries.begin() + static_cast<std::ptrdiff_t>(index));
    std::vector<size_t> visible = VisibleEntries();
    if (visible.empty()) {
        g_app.scrollIndex = 0;
    } else {
        g_app.scrollIndex = std::clamp(g_app.scrollIndex, 0, static_cast<int>(visible.size()) - 1);
    }
    SaveLinks(g_app.entries);
    RequestRender(hwnd);
}

void EditEntry(HWND hwnd, size_t index) {
    if (index >= g_app.entries.size()) {
        return;
    }

    VideoDialogInput input;
    input.editing = true;
    input.title = g_app.entries[index].title;
    input.url = g_app.entries[index].url;
    input.tag = NormalizedTag(g_app.entries[index].tag);
    AddResult result = ShowVideoDialog(g_app.instance, hwnd, Tags(), input);
    if (!result.accepted) {
        return;
    }

    bool urlChanged = g_app.entries[index].url != result.url;
    g_app.entries[index].title = result.title.empty() ? L"제목 없음" : result.title;
    g_app.entries[index].url = result.url;
    g_app.entries[index].tag = NormalizedTag(result.tag);
    if (urlChanged) {
        g_app.entries[index].thumbnail = LoadThumbnail(result.url);
    }
    if (!EntryInActiveTag(g_app.entries[index])) {
        g_app.activeTag = NormalizedTag(g_app.entries[index].tag);
        g_app.scrollIndex = PositionInVisible(index);
    }
    SaveLinks(g_app.entries);
    RequestRender(hwnd);
}

void FinishPress(HWND hwnd, POINT point) {
    if (!g_app.mouseDown) {
        return;
    }
    g_app.mouseDown = false;

    RECT client{};
    GetClientRect(hwnd, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    if (!TargetStillActive(layout, point, g_app.pressTarget, g_app.pressEntryIndex)) {
        g_app.pressTarget = AppState::PressTarget::None;
        ReleaseCapture();
        return;
    }

    switch (g_app.pressTarget) {
    case AppState::PressTarget::Tag:
        ShowTagMenu(hwnd);
        break;
    case AppState::PressTarget::Add: {
        VideoDialogInput input;
        input.tag = g_app.activeTag;
        AddResult result = ShowVideoDialog(g_app.instance, hwnd, Tags(), input);
        if (result.accepted) {
            AddEntry(result);
        }
        break;
    }
    case AppState::PressTarget::Delete:
        DeleteFromDialog(hwnd);
        break;
    case AppState::PressTarget::Card:
        if (!g_app.mouseMoved && g_app.pressEntryIndex < g_app.entries.size() && PositionInVisible(g_app.pressEntryIndex) == g_app.scrollIndex) {
            OpenUrl(g_app.entries[g_app.pressEntryIndex].url);
        } else if (!g_app.mouseMoved && g_app.pressEntryIndex < g_app.entries.size()) {
            g_app.scrollIndex = PositionInVisible(g_app.pressEntryIndex);
            RequestRender(hwnd);
        }
        break;
    default:
        break;
    }
    g_app.pressTarget = AppState::PressTarget::None;
    ReleaseCapture();
}

void LoadThumbnails() {
    for (auto& entry : g_app.entries) {
        entry.thumbnail = LoadThumbnail(entry.url);
    }
}

Layout BuildLayout(int width, int height) {
    Layout layout;
    layout.tagButton = {12, 14, 100, 28};
    layout.addButton = {std::max(12, width - 44), 14, 28, 28};
    layout.deleteButton = {14, std::max(12, height - 38), 28, 28};
    layout.moveHandle = {std::max(12, width / 2 - 32), std::max(12, height - 36), 64, 24};
    layout.resizeHandle = {std::max(12, width - 42), std::max(12, height - 38), 30, 28};

    std::vector<size_t> visible = VisibleEntries();
    int count = static_cast<int>(visible.size());
    if (count == 0) {
        layout.visibleSlots = 0;
        return layout;
    }

    layout.visibleSlots = std::min(count, 3);
    if (count > 0) {
        g_app.scrollIndex = (g_app.scrollIndex % count + count) % count;
    }

    int top = 58;
    int bottom = 58;
    int gap = 18;
    int availableHeight = std::max(96, height - top - bottom);
    int centerWidth = std::clamp((width - 76) / 3, 92, 150);
    int sideWidth = std::max(72, static_cast<int>(centerWidth * 0.78));
    int totalWidth = sideWidth * 2 + centerWidth + gap * 2;
    if (totalWidth > width - 24) {
        double scale = static_cast<double>(std::max(180, width - 24)) / static_cast<double>(totalWidth);
        centerWidth = std::max(82, static_cast<int>(centerWidth * scale));
        sideWidth = std::max(64, static_cast<int>(sideWidth * scale));
    }

    int centerHeight = std::clamp(static_cast<int>(centerWidth * 1.52), 112, availableHeight);
    int sideHeight = std::clamp(static_cast<int>(centerHeight * 0.82), 92, availableHeight);
    int centerX = (width - centerWidth) / 2;
    int centerY = top + (availableHeight - centerHeight) / 2;
    int sideY = centerY + (centerHeight - sideHeight) / 2;
    int leftX = centerX - gap - sideWidth;
    int rightX = centerX + centerWidth + gap;

    auto wrapIndex = [&visible, count](int index) {
        return visible[static_cast<size_t>((index % count + count) % count)];
    };

    if (count == 1) {
        layout.cards.push_back({visible[0], {centerX, centerY, centerWidth, centerHeight}, true});
    } else if (count == 2) {
        layout.cards.push_back({wrapIndex(g_app.scrollIndex + 1), {rightX, sideY, sideWidth, sideHeight}, false});
        layout.cards.push_back({wrapIndex(g_app.scrollIndex), {centerX, centerY, centerWidth, centerHeight}, true});
    } else {
        layout.cards.push_back({wrapIndex(g_app.scrollIndex - 1), {leftX, sideY, sideWidth, sideHeight}, false});
        layout.cards.push_back({wrapIndex(g_app.scrollIndex + 1), {rightX, sideY, sideWidth, sideHeight}, false});
        layout.cards.push_back({wrapIndex(g_app.scrollIndex), {centerX, centerY, centerWidth, centerHeight}, true});
    }

    return layout;
}

void DrawText(Graphics& graphics, const std::wstring& text, RectF bounds, float size, Color color, StringAlignment lineAlignment) {
    if (text.empty()) {
        return;
    }
    Font font(L"Malgun Gothic", size, FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(lineAlignment);
    format.SetTrimming(StringTrimmingEllipsisWord);
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    graphics.DrawString(text.c_str(), -1, &font, bounds, &format, &brush);
}

void DrawCard(Graphics& graphics, const CardLayout& card, const LinkEntry& entry) {
    const RectI& rect = card.rect;
    SolidBrush fallback(card.active ? Color(255, 128, 128, 128) : Color(255, 112, 112, 112));
    graphics.FillRectangle(&fallback, rect.x, rect.y, rect.w, rect.h);

    if (entry.thumbnail) {
        graphics.SetClip(Rect(rect.x, rect.y, rect.w, rect.h));
        float imageWidth = static_cast<float>(entry.thumbnail->GetWidth());
        float imageHeight = static_cast<float>(entry.thumbnail->GetHeight());
        float scale = std::max(static_cast<float>(rect.w) / imageWidth, static_cast<float>(rect.h) / imageHeight);
        float sourceWidth = static_cast<float>(rect.w) / scale;
        float sourceHeight = static_cast<float>(rect.h) / scale;
        float sourceX = (imageWidth - sourceWidth) * 0.5f;
        float sourceY = (imageHeight - sourceHeight) * 0.5f;
        graphics.DrawImage(entry.thumbnail.get(), RectF(static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w), static_cast<float>(rect.h)), sourceX, sourceY, sourceWidth, sourceHeight, UnitPixel);
        graphics.ResetClip();
    }

    RectI titleBar{rect.x, rect.y + rect.h - 42, rect.w, 42};
    SolidBrush titleBrush(card.active ? Color(255, 92, 109, 132) : Color(255, 84, 95, 112));
    graphics.FillRectangle(&titleBrush, titleBar.x, titleBar.y, titleBar.w, titleBar.h);
    std::wstring title = entry.title.empty() ? L"제목 없음" : entry.title;
    float fontSize = card.active ? 15.0f : 13.0f;
    DrawText(graphics, title, RectF(static_cast<float>(rect.x + 10), static_cast<float>(rect.y + rect.h - 39), static_cast<float>(rect.w - 20), 34.0f), fontSize, Color(255, 245, 248, 252), StringAlignmentCenter);
}

void DrawAddButton(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(190, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(220, 0, 0, 0), 3.0f);
    float cx = static_cast<float>(rect.x + rect.w / 2);
    float cy = static_cast<float>(rect.y + rect.h / 2);
    graphics.DrawLine(&pen, cx - 8.0f, cy, cx + 8.0f, cy);
    graphics.DrawLine(&pen, cx, cy - 8.0f, cx, cy + 8.0f);
}

void DrawTagButton(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(190, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    DrawText(graphics, g_app.activeTag, RectF(static_cast<float>(rect.x + 8), static_cast<float>(rect.y + 4), static_cast<float>(rect.w - 16), static_cast<float>(rect.h - 8)), 13.0f, Color(230, 0, 0, 0), StringAlignmentCenter);
}

void DrawDeleteButton(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(190, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(220, 0, 0, 0), 3.0f);
    graphics.DrawLine(&pen, rect.x + 8, rect.y + 8, rect.x + rect.w - 8, rect.y + rect.h - 8);
    graphics.DrawLine(&pen, rect.x + rect.w - 8, rect.y + 8, rect.x + 8, rect.y + rect.h - 8);
}

void DrawMoveHandle(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(190, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(220, 0, 0, 0), 3.0f);
    float centerX = static_cast<float>(rect.x + rect.w / 2);
    float centerY = static_cast<float>(rect.y + rect.h / 2);
    graphics.DrawLine(&pen, centerX - 14.0f, centerY, centerX + 14.0f, centerY);
    graphics.DrawLine(&pen, centerX, centerY - 7.0f, centerX, centerY + 7.0f);
    graphics.DrawLine(&pen, centerX - 14.0f, centerY, centerX - 8.0f, centerY - 5.0f);
    graphics.DrawLine(&pen, centerX - 14.0f, centerY, centerX - 8.0f, centerY + 5.0f);
    graphics.DrawLine(&pen, centerX + 14.0f, centerY, centerX + 8.0f, centerY - 5.0f);
    graphics.DrawLine(&pen, centerX + 14.0f, centerY, centerX + 8.0f, centerY + 5.0f);
}

void DrawResizeHandle(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(190, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(220, 0, 0, 0), 3.0f);
    int right = rect.x + rect.w - 7;
    int bottom = rect.y + rect.h - 7;
    graphics.DrawLine(&pen, right - 18, bottom, right, bottom - 18);
    graphics.DrawLine(&pen, right - 9, bottom, right, bottom - 9);
}

void RenderWindow(HWND hwnd) {
    if (g_app.rendering) {
        return;
    }

    struct RenderScope {
        bool& value;
        explicit RenderScope(bool& target) : value(target) {
            value = true;
        }
        ~RenderScope() {
            value = false;
        }
    } scope(g_app.rendering);

    RECT window{};
    GetWindowRect(hwnd, &window);
    int width = window.right - window.left;
    int height = window.bottom - window.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return;
    }
    HDC memoryDc = CreateCompatibleDC(screenDc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memoryDc || !bitmap || !bits) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (memoryDc) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(nullptr, screenDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

    {
        Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Graphics graphics(&surface);
        graphics.SetCompositingMode(CompositingModeSourceCopy);
        graphics.Clear(Color(0, 0, 0, 0));
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        Layout layout = BuildLayout(width, height);
        for (const auto& card : layout.cards) {
            DrawCard(graphics, card, g_app.entries[card.entryIndex]);
        }
        DrawTagButton(graphics, layout.tagButton);
        DrawAddButton(graphics, layout.addButton);
        DrawDeleteButton(graphics, layout.deleteButton);
        DrawMoveHandle(graphics, layout.moveHandle);
        DrawResizeHandle(graphics, layout.resizeHandle);
        graphics.Flush(FlushIntentionFlush);
    }

    POINT destination{window.left, window.top};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, screenDc, &destination, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    ValidateRect(hwnd, nullptr);
}

void RequestRender(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        RenderWindow(hwnd);
    }
}

void AddEntry(const AddResult& result) {
    LinkEntry entry;
    entry.title = result.title.empty() ? L"제목 없음" : result.title;
    entry.url = result.url;
    entry.tag = NormalizedTag(result.tag);
    entry.thumbnail = LoadThumbnail(entry.url);
    g_app.entries.push_back(std::move(entry));
    g_app.activeTag = NormalizedTag(g_app.entries.back().tag);
    g_app.scrollIndex = PositionInVisible(g_app.entries.size() - 1);
    SaveLinks(g_app.entries);
    RequestRender(g_app.window);
}

void OpenUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ScrollBy(int delta) {
    int count = static_cast<int>(VisibleEntries().size());
    if (count <= 1) {
        return;
    }
    int before = g_app.scrollIndex;
    g_app.scrollIndex = (g_app.scrollIndex + delta) % count;
    if (g_app.scrollIndex < 0) {
        g_app.scrollIndex += count;
    }
    if (before != g_app.scrollIndex) {
        RequestRender(g_app.window);
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCAPTION) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 280;
        info->ptMinTrackSize.y = 180;
        return 0;
    }
    case WM_NCHITTEST: {
        POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT point = screenPoint;
        ScreenToClient(hwnd, &point);
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
        size_t entryIndex = 0;
        return TargetAt(layout, point, entryIndex) == AppState::PressTarget::None ? HTTRANSPARENT : HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT screenPoint = point;
        ClientToScreen(hwnd, &screenPoint);
        StartPress(hwnd, point, screenPoint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_app.mouseDown) {
            POINT screenPoint = point;
            ClientToScreen(hwnd, &screenPoint);
            if (g_app.pressTarget == AppState::PressTarget::Move) {
                MoveWindowFromDrag(hwnd, screenPoint);
            } else if (g_app.pressTarget == AppState::PressTarget::Resize) {
                ResizeWindowFromDrag(hwnd, screenPoint);
            } else if (g_app.pressTarget == AppState::PressTarget::Card) {
                DragScrollCards(point);
            }
        } else {
            UpdateCursor(hwnd, point);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        FinishPress(hwnd, point);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
        for (const auto& card : layout.cards) {
            if (ContainsExpanded(card.rect, point, 8)) {
                EditEntry(hwnd, card.entryIndex);
                return 0;
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_app.mouseDown && reinterpret_cast<HWND>(lParam) != hwnd) {
            g_app.mouseDown = false;
            g_app.pressTarget = AppState::PressTarget::None;
        }
        return 0;
    case WM_MOUSEWHEEL:
        g_app.wheelAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        while (g_app.wheelAccum >= WHEEL_DELTA) {
            ScrollBy(-1);
            g_app.wheelAccum -= WHEEL_DELTA;
        }
        while (g_app.wheelAccum <= -WHEEL_DELTA) {
            ScrollBy(1);
            g_app.wheelAccum += WHEEL_DELTA;
        }
        return 0;
    case WM_MOUSEHWHEEL:
        g_app.wheelAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        while (g_app.wheelAccum >= WHEEL_DELTA) {
            ScrollBy(1);
            g_app.wheelAccum -= WHEEL_DELTA;
        }
        while (g_app.wheelAccum <= -WHEEL_DELTA) {
            ScrollBy(-1);
            g_app.wheelAccum += WHEEL_DELTA;
        }
        return 0;
    case WM_SIZE:
        RequestRender(hwnd);
        return 0;
    case WM_EXITSIZEMOVE:
        SaveCurrentWindowSettings(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_LEFT) {
            ScrollBy(-1);
            return 0;
        }
        if (wParam == VK_RIGHT) {
            ScrollBy(1);
            return 0;
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        RememberCurrentWindowSettings(hwnd);
        if ((reinterpret_cast<WINDOWPOS*>(lParam)->flags & SWP_NOSIZE) == 0) {
            RequestRender(hwnd);
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        RenderWindow(hwnd);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveLinks(g_app.entries);
        SaveCurrentWindowSettings(hwnd);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterMainClass(HINSTANCE instance) {
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.lpfnWndProc = MainProc;
    mainClass.hInstance = instance;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    mainClass.lpszClassName = kMainClassName;
    RegisterClassExW(&mainClass);
}

}

int RunApp(HINSTANCE instance, int showCommand) {
    if (CheckForUpdateAndRestart()) {
        return 0;
    }

    g_app.instance = instance;

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_app.gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        return 1;
    }

    RegisterMainClass(instance);
    RegisterAddDialogClass(instance);
    EnableStartup();

    g_app.entries = LoadLinks();
    LoadThumbnails();

    g_app.settings = LoadWindowSettings();
    if (!g_app.settings.valid) {
        g_app.settings = DefaultWindowSettings();
    }

    g_app.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, kMainClassName, L"BlackFix VideoShuffle", WS_POPUP, g_app.settings.x, g_app.settings.y, g_app.settings.width, g_app.settings.height, nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
        g_app.entries.clear();
        GdiplusShutdown(g_app.gdiplusToken);
        return 1;
    }

    RenderWindow(g_app.window);
    ShowWindow(g_app.window, showCommand);
    RenderWindow(g_app.window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_app.entries.clear();
    GdiplusShutdown(g_app.gdiplusToken);
    return static_cast<int>(message.wParam);
}
