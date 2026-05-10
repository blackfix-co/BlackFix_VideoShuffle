#include "app.hpp"

#include "add_dialog.hpp"
#include "storage.hpp"
#include "thumbnail.hpp"
#include "utils.hpp"

#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>

using namespace Gdiplus;

namespace {

constexpr COLORREF kTransparentColor = RGB(255, 0, 255);
constexpr wchar_t kMainClassName[] = L"BlackFixVideoShuffleWindow";

struct AppState {
    HINSTANCE instance{};
    HWND window{};
    ULONG_PTR gdiplusToken{};
    std::vector<LinkEntry> entries;
    WindowSettings settings;
    int scrollIndex{};
};

AppState g_app;

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

void SaveCurrentWindowSettings(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return;
    }
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    WindowSettings settings;
    settings.x = rect.left;
    settings.y = rect.top;
    settings.width = rect.right - rect.left;
    settings.height = rect.bottom - rect.top;
    settings.valid = true;
    g_app.settings = settings;
    SaveWindowSettings(settings);
}

void LoadThumbnails() {
    for (auto& entry : g_app.entries) {
        entry.thumbnail = LoadThumbnail(entry.url);
    }
}

int PageLimit(int visibleSlots) {
    return std::max(0, static_cast<int>(g_app.entries.size()) - visibleSlots);
}

void ClampScroll(int visibleSlots) {
    g_app.scrollIndex = std::clamp(g_app.scrollIndex, 0, PageLimit(visibleSlots));
}

int MaxSlotsForWidth(int width, bool hasNavigation) {
    int margin = 24;
    int gap = 16;
    int navigationSpace = hasNavigation ? 76 : 0;
    int availableWidth = std::max(96, width - margin * 2 - navigationSpace);
    return std::clamp((availableWidth + gap) / (96 + gap), 1, 3);
}

Layout BuildLayout(int width, int height) {
    Layout layout;
    int buttonSize = 42;
    layout.addButton = {std::max(12, width - buttonSize - 16), 14, buttonSize, buttonSize};

    int count = static_cast<int>(g_app.entries.size());
    if (count == 0) {
        layout.visibleSlots = 0;
        return layout;
    }

    int slots = std::min(count, MaxSlotsForWidth(width, false));
    bool hasNavigation = count > slots;
    slots = std::min(count, MaxSlotsForWidth(width, hasNavigation));
    hasNavigation = count > slots;
    layout.visibleSlots = slots;
    ClampScroll(slots);

    int margin = 24;
    int gap = 16;
    int navSpace = hasNavigation ? 76 : 0;
    int availableWidth = std::max(96, width - margin * 2 - navSpace);
    int cardWidth = std::min(136, (availableWidth - gap * (slots - 1)) / slots);
    cardWidth = std::max(86, cardWidth);
    int availableHeight = std::max(92, height - 88);
    int cardHeight = std::clamp(static_cast<int>(cardWidth * 1.55), 110, availableHeight);
    int totalWidth = cardWidth * slots + gap * (slots - 1);
    int startX = (width - totalWidth) / 2;
    int startY = 64 + (availableHeight - cardHeight) / 2;

    for (int i = 0; i < slots; ++i) {
        size_t entryIndex = static_cast<size_t>(g_app.scrollIndex + i);
        if (entryIndex < g_app.entries.size()) {
            layout.cards.push_back({entryIndex, {startX + i * (cardWidth + gap), startY, cardWidth, cardHeight}});
        }
    }

    if (hasNavigation) {
        int arrowSize = 34;
        int arrowY = startY + (cardHeight - arrowSize) / 2;
        layout.leftButton = {std::max(8, startX - arrowSize - 18), arrowY, arrowSize, arrowSize};
        layout.rightButton = {std::min(width - arrowSize - 8, startX + totalWidth + 18), arrowY, arrowSize, arrowSize};
        layout.showLeft = g_app.scrollIndex > 0;
        layout.showRight = g_app.scrollIndex < PageLimit(slots);
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

void DrawCard(Graphics& graphics, const RectI& rect, const LinkEntry& entry) {
    SolidBrush fallback(Color(255, 128, 128, 128));
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
    SolidBrush titleBrush(Color(255, 92, 109, 132));
    graphics.FillRectangle(&titleBrush, titleBar.x, titleBar.y, titleBar.w, titleBar.h);
    std::wstring title = entry.title.empty() ? L"제목 없음" : entry.title;
    DrawText(graphics, title, RectF(static_cast<float>(rect.x + 10), static_cast<float>(rect.y + rect.h - 39), static_cast<float>(rect.w - 20), 34.0f), 15.0f, Color(255, 245, 248, 252), StringAlignmentCenter);
}

void DrawAddButton(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(255, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(255, 0, 0, 0), 4.0f);
    float cx = static_cast<float>(rect.x + rect.w / 2);
    float cy = static_cast<float>(rect.y + rect.h / 2);
    graphics.DrawLine(&pen, cx - 13.0f, cy, cx + 13.0f, cy);
    graphics.DrawLine(&pen, cx, cy - 13.0f, cx, cy + 13.0f);
}

void DrawArrowButton(Graphics& graphics, const RectI& rect, bool right) {
    SolidBrush brush(Color(255, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(255, 0, 0, 0), 4.0f);
    float centerX = static_cast<float>(rect.x + rect.w / 2);
    float centerY = static_cast<float>(rect.y + rect.h / 2);
    float direction = right ? 1.0f : -1.0f;
    graphics.DrawLine(&pen, centerX - 7.0f * direction, centerY - 11.0f, centerX + 7.0f * direction, centerY);
    graphics.DrawLine(&pen, centerX + 7.0f * direction, centerY, centerX - 7.0f * direction, centerY + 11.0f);
}

void DrawResizeGrip(Graphics& graphics, int width, int height) {
    Pen pen(Color(255, 70, 80, 96), 2.0f);
    graphics.DrawLine(&pen, width - 18, height - 6, width - 6, height - 18);
    graphics.DrawLine(&pen, width - 12, height - 6, width - 6, height - 12);
}

void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH clearBrush = CreateSolidBrush(kTransparentColor);
    FillRect(hdc, &client, clearBrush);
    DeleteObject(clearBrush);

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    int width = client.right - client.left;
    int height = client.bottom - client.top;
    Layout layout = BuildLayout(width, height);
    for (const auto& card : layout.cards) {
        DrawCard(graphics, card.rect, g_app.entries[card.entryIndex]);
    }
    if (layout.showLeft) {
        DrawArrowButton(graphics, layout.leftButton, false);
    }
    if (layout.showRight) {
        DrawArrowButton(graphics, layout.rightButton, true);
    }
    DrawAddButton(graphics, layout.addButton);
    DrawResizeGrip(graphics, width, height);

    EndPaint(hwnd, &ps);
}

bool HitCard(const Layout& layout, POINT point, size_t& entryIndex) {
    for (const auto& card : layout.cards) {
        if (card.rect.contains(point)) {
            entryIndex = card.entryIndex;
            return true;
        }
    }
    return false;
}

int HitResizeEdge(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    int edge = 8;
    bool left = point.x < edge;
    bool right = point.x >= width - edge;
    bool top = point.y < edge;
    bool bottom = point.y >= height - edge;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    return HTNOWHERE;
}

void AddEntry(const AddResult& result) {
    LinkEntry entry;
    entry.title = result.title.empty() ? L"제목 없음" : result.title;
    entry.url = result.url;
    entry.thumbnail = LoadThumbnail(entry.url);
    g_app.entries.push_back(std::move(entry));
    RECT client{};
    GetClientRect(g_app.window, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    if (layout.visibleSlots > 0) {
        g_app.scrollIndex = PageLimit(layout.visibleSlots);
    }
    SaveLinks(g_app.entries);
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void OpenUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ScrollBy(int delta) {
    RECT client{};
    GetClientRect(g_app.window, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    if (layout.visibleSlots <= 0) {
        return;
    }
    int before = g_app.scrollIndex;
    g_app.scrollIndex = std::clamp(g_app.scrollIndex + delta, 0, PageLimit(layout.visibleSlots));
    if (before != g_app.scrollIndex) {
        InvalidateRect(g_app.window, nullptr, FALSE);
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetLayeredWindowAttributes(hwnd, kTransparentColor, 255, LWA_COLORKEY);
        return 0;
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
        int resize = HitResizeEdge(hwnd, point);
        if (resize != HTNOWHERE) {
            return resize;
        }
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
        size_t entryIndex = 0;
        if (layout.addButton.contains(point) || layout.leftButton.contains(point) || layout.rightButton.contains(point) || HitCard(layout, point, entryIndex)) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
        if (layout.addButton.contains(point)) {
            AddResult result = ShowAddDialog(g_app.instance, hwnd);
            if (result.accepted) {
                AddEntry(result);
            }
            return 0;
        }
        if (layout.showLeft && layout.leftButton.contains(point)) {
            ScrollBy(-layout.visibleSlots);
            return 0;
        }
        if (layout.showRight && layout.rightButton.contains(point)) {
            ScrollBy(layout.visibleSlots);
            return 0;
        }
        size_t entryIndex = 0;
        if (HitCard(layout, point, entryIndex)) {
            OpenUrl(g_app.entries[entryIndex].url);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        ScrollBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
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
        SaveCurrentWindowSettings(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_PAINT:
        PaintWindow(hwnd);
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

    g_app.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kMainClassName, L"BlackFix VideoShuffle", WS_POPUP, g_app.settings.x, g_app.settings.y, g_app.settings.width, g_app.settings.height, nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
        g_app.entries.clear();
        GdiplusShutdown(g_app.gdiplusToken);
        return 1;
    }

    ShowWindow(g_app.window, showCommand);
    UpdateWindow(g_app.window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_app.entries.clear();
    GdiplusShutdown(g_app.gdiplusToken);
    return static_cast<int>(message.wParam);
}
