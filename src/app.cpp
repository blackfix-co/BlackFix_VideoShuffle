#include "app.hpp"

#include "add_dialog.hpp"
#include "delete_dialog.hpp"
#include "link_title.hpp"
#include "storage.hpp"
#include "thumbnail.hpp"
#include "updater.hpp"
#include "utils.hpp"

#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cwctype>

using namespace Gdiplus;

namespace {

constexpr wchar_t kMainClassName[] = L"BlackFixVideoShuffleWindow";
constexpr wchar_t kAllTag[] = L"전체";
constexpr UINT_PTR kCarouselTimerId = 11;
constexpr DWORD kCarouselDurationMs = 260;

struct AppState {
    HINSTANCE instance{};
    HWND window{};
    ULONG_PTR gdiplusToken{};
    std::vector<LinkEntry> entries;
    std::vector<std::wstring> tags;
    WindowSettings settings;
    std::wstring activeTag{kAllTag};
    int scrollIndex{};
    bool carouselAnimating{};
    int animationFromIndex{};
    int animationToIndex{};
    int animationDirection{};
    DWORD animationStarted{};
    double animationProgress{};
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
        Card,
        Move,
        Resize
    } pressTarget{PressTarget::None};
    PressTarget armedTarget{PressTarget::None};
};

AppState g_app;

Layout BuildLayout(int width, int height);
void ScrollBy(int delta);
void AddEntry(const AddResult& result);
void OpenUrl(const std::wstring& url);
void OpenEntry(const LinkEntry& entry);
void DeleteIndices(HWND hwnd, std::vector<size_t> indices);
void ApplyTagAssignments(const std::wstring& tag, const std::vector<size_t>& indices);
void RemoveKnownTags(const std::vector<std::wstring>& tags);
std::wstring ResolveTitle(const std::wstring& title, const std::wstring& url);
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

bool IsDevelopmentPath(const std::wstring& path) {
    std::wstring value = path;
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value.find(L"\\build\\") != std::wstring::npos;
}

void EnableStartup() {
    std::wstring exePath = CurrentExePath();
    if (IsDevelopmentPath(exePath)) {
        return;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    std::wstring command = L"\"" + exePath + L"\"";
    RegSetValueExW(key, L"BlackFix_VideoShuffle", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void CreateDesktopShortcut() {
    std::wstring exePath = CurrentExePath();
    if (IsDevelopmentPath(exePath)) {
        return;
    }

    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_CREATE, nullptr, &desktopPath))) {
        return;
    }

    std::wstring shortcutPath = desktopPath;
    CoTaskMemFree(desktopPath);
    if (!shortcutPath.empty() && shortcutPath.back() != L'\\') {
        shortcutPath += L'\\';
    }
    shortcutPath += L"BlackFix_VideoShuffle.lnk";

    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        return;
    }

    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        link->SetPath(exePath.c_str());
        size_t slash = exePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring workingDirectory = exePath.substr(0, slash);
            link->SetWorkingDirectory(workingDirectory.c_str());
        }
        link->SetDescription(L"BlackFix VideoShuffle");

        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            file->Save(shortcutPath.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
}

RECT VirtualScreenRect() {
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return {screenX, screenY, screenX + screenW, screenY + screenH};
}

POINT ClampedPosition(int x, int y, int width, int height) {
    RECT screen = VirtualScreenRect();
    int screenW = screen.right - screen.left;
    int screenH = screen.bottom - screen.top;
    POINT result{x, y};
    int left = static_cast<int>(screen.left);
    int top = static_cast<int>(screen.top);
    int right = static_cast<int>(screen.right);
    int bottom = static_cast<int>(screen.bottom);
    result.x = width >= screenW ? left : std::clamp(x, left, right - width);
    result.y = height >= screenH ? top : std::clamp(y, top, bottom - height);
    return result;
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

WindowSettings EnsureVisible(WindowSettings settings) {
    if (!settings.valid) {
        return DefaultWindowSettings();
    }

    RECT screen = VirtualScreenRect();
    int screenW = screen.right - screen.left;
    int screenH = screen.bottom - screen.top;
    settings.width = std::clamp(settings.width, 280, std::max(280, screenW));
    settings.height = std::clamp(settings.height, 180, std::max(180, screenH));
    POINT position = ClampedPosition(settings.x, settings.y, settings.width, settings.height);
    settings.x = position.x;
    settings.y = position.y;
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

bool AddKnownTag(const std::wstring& tag) {
    std::wstring normalized = NormalizedTag(tag);
    if (normalized == kAllTag) {
        return false;
    }
    if (std::find(g_app.tags.begin(), g_app.tags.end(), normalized) != g_app.tags.end()) {
        return false;
    }
    g_app.tags.push_back(normalized);
    return true;
}

void RemoveKnownTags(const std::vector<std::wstring>& tags) {
    for (const auto& tag : tags) {
        std::wstring normalized = NormalizedTag(tag);
        g_app.tags.erase(std::remove(g_app.tags.begin(), g_app.tags.end(), normalized), g_app.tags.end());
        if (g_app.activeTag == normalized) {
            g_app.activeTag = kAllTag;
        }
    }
}

std::vector<std::wstring> Tags() {
    std::vector<std::wstring> tags{kAllTag};
    for (const auto& knownTag : g_app.tags) {
        std::wstring tag = NormalizedTag(knownTag);
        if (tag != kAllTag && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
    }
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

std::wstring ExtractVideoIdAt(const std::wstring& url, size_t start) {
    std::wstring id;
    for (size_t i = start; i < url.size(); ++i) {
        wchar_t ch = url[i];
        bool allowed = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-';
        if (!allowed) {
            break;
        }
        id.push_back(ch);
    }
    if (id.size() >= 11) {
        id.resize(11);
        return id;
    }
    return {};
}

std::wstring YoutubeVideoId(const std::wstring& url) {
    std::wstring lower = LowerCopy(url);
    const std::vector<std::wstring> patterns = {
        L"youtu.be/",
        L"/shorts/",
        L"/embed/",
        L"/live/"
    };
    for (const auto& pattern : patterns) {
        size_t pos = lower.find(pattern);
        if (pos != std::wstring::npos) {
            return ExtractVideoIdAt(url, pos + pattern.size());
        }
    }
    size_t query = lower.find(L"v=");
    if (query != std::wstring::npos) {
        return ExtractVideoIdAt(url, query + 2);
    }
    return {};
}

std::wstring PreviewUrl(const std::wstring& url) {
    std::wstring id = YoutubeVideoId(url);
    if (id.empty()) {
        return url;
    }
    return L"https://www.youtube.com/embed/" + id + L"?autoplay=1&loop=1&playlist=" + id + L"&start=0&end=60";
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

int WrappedVisiblePosition(int index, int count) {
    return (index % count + count) % count;
}

RectI InterpolatedRect(const RectI& from, const RectI& to, double progress) {
    auto lerp = [progress](int a, int b) {
        return static_cast<int>(a + (b - a) * progress);
    };
    return {lerp(from.x, to.x), lerp(from.y, to.y), lerp(from.w, to.w), lerp(from.h, to.h)};
}

AppState::PressTarget TargetAt(const Layout& layout, POINT point, size_t& entryIndex) {
    if (g_app.armedTarget == AppState::PressTarget::Move || g_app.armedTarget == AppState::PressTarget::Resize) {
        return g_app.armedTarget;
    }
    if (ContainsExpanded(layout.tagButton, point, 8)) {
        return AppState::PressTarget::Tag;
    }
    if (ContainsExpanded(layout.addButton, point, 10)) {
        return AppState::PressTarget::Add;
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
    case AppState::PressTarget::Move:
    case AppState::PressTarget::Resize:
        return true;
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
    POINT position = ClampedPosition(g_app.pressWindow.left + dx, g_app.pressWindow.top + dy, width, height);
    SetWindowPos(hwnd, nullptr, position.x, position.y, width, height, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    double widthScale = static_cast<double>(std::max(minWidth, originalWidth + dx)) / static_cast<double>(originalWidth);
    double heightScale = static_cast<double>(std::max(minHeight, originalHeight + dy)) / static_cast<double>(originalHeight);
    double scale = std::abs(widthScale - 1.0) >= std::abs(heightScale - 1.0) ? widthScale : heightScale;

    RECT screen = VirtualScreenRect();
    POINT anchor = ClampedPosition(g_app.pressWindow.left, g_app.pressWindow.top, originalWidth, originalHeight);
    int anchorX = static_cast<int>(anchor.x);
    int anchorY = static_cast<int>(anchor.y);
    int maxWidth = std::max(minWidth, static_cast<int>(screen.right) - anchorX);
    int maxHeight = std::max(minHeight, static_cast<int>(screen.bottom) - anchorY);
    double minScale = std::max(static_cast<double>(minWidth) / originalWidth, static_cast<double>(minHeight) / originalHeight);
    double maxScale = std::min(static_cast<double>(maxWidth) / originalWidth, static_cast<double>(maxHeight) / originalHeight);
    scale = std::clamp(scale, minScale, std::max(minScale, maxScale));

    int newWidth = std::max(minWidth, static_cast<int>(originalWidth * scale));
    int newHeight = std::max(minHeight, static_cast<int>(originalHeight * scale));
    SetWindowPos(hwnd, nullptr, anchorX, anchorY, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
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
    if (g_app.entries.empty() && g_app.tags.empty()) {
        return;
    }

    DeleteResult result = ShowDeleteDialog(g_app.instance, hwnd, g_app.entries, Tags(), g_app.activeTag);
    if (result.indices.empty() && result.deletedTags.empty()) {
        return;
    }

    RemoveKnownTags(result.deletedTags);
    if (!result.indices.empty()) {
        DeleteIndices(hwnd, std::move(result.indices));
    } else {
        SaveTags(g_app.tags);
        RequestRender(hwnd);
    }
}

void DeleteIndices(HWND hwnd, std::vector<size_t> indices) {
    if (indices.empty()) {
        return;
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it < g_app.entries.size()) {
            g_app.entries.erase(g_app.entries.begin() + static_cast<std::ptrdiff_t>(*it));
        }
    }

    std::vector<size_t> visible = VisibleEntries();
    if (visible.empty()) {
        g_app.scrollIndex = 0;
    } else {
        g_app.scrollIndex = std::clamp(g_app.scrollIndex, 0, static_cast<int>(visible.size()) - 1);
    }
    KillTimer(hwnd, kCarouselTimerId);
    g_app.carouselAnimating = false;
    g_app.animationProgress = 0.0;
    SaveLinks(g_app.entries);
    SaveTags(g_app.tags);
    RequestRender(hwnd);
}

void ApplyTagAssignments(const std::wstring& tag, const std::vector<size_t>& indices) {
    std::wstring normalized = NormalizedTag(tag);
    AddKnownTag(normalized);
    for (size_t index : indices) {
        if (index < g_app.entries.size()) {
            g_app.entries[index].tag = normalized;
        }
    }
}

std::wstring CreatedTagName(const AddResult& result) {
    return result.createdTag.empty() ? result.tag : result.createdTag;
}

size_t CardAt(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    Layout layout = BuildLayout(client.right - client.left, client.bottom - client.top);
    for (const auto& card : layout.cards) {
        if (ContainsExpanded(card.rect, point, 8)) {
            return card.entryIndex;
        }
    }
    return static_cast<size_t>(-1);
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
    input.preview = g_app.entries[index].preview;
    AddResult result = ShowVideoDialog(g_app.instance, hwnd, Tags(), g_app.entries, input);
    if (result.tagCreated) {
        ApplyTagAssignments(CreatedTagName(result), result.tagAssignments);
        SaveLinks(g_app.entries);
        SaveTags(g_app.tags);
        RequestRender(hwnd);
    }
    if (!result.accepted) {
        return;
    }

    if (!result.tagCreated) {
        ApplyTagAssignments(result.tag, result.tagAssignments);
    }
    bool urlChanged = g_app.entries[index].url != result.url;
    if (!result.url.empty()) {
        g_app.entries[index].title = ResolveTitle(result.title, result.url);
        g_app.entries[index].url = result.url;
        g_app.entries[index].tag = NormalizedTag(result.tag);
        g_app.entries[index].preview = result.preview;
        if (urlChanged) {
            g_app.entries[index].thumbnail = LoadThumbnail(result.url);
        }
    }
    if (!EntryInActiveTag(g_app.entries[index])) {
        g_app.activeTag = NormalizedTag(g_app.entries[index].tag);
        g_app.scrollIndex = PositionInVisible(index);
    }
    SaveLinks(g_app.entries);
    SaveTags(g_app.tags);
    RequestRender(hwnd);
}

void ShowContextMenu(HWND hwnd, POINT clientPoint, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    constexpr UINT editId = 4101;
    constexpr UINT moveId = 4102;
    constexpr UINT resizeId = 4103;
    constexpr UINT deleteId = 4104;
    constexpr UINT closeId = 4105;

    size_t cardIndex = CardAt(hwnd, clientPoint);
    if (cardIndex < g_app.entries.size()) {
        AppendMenuW(menu, MF_STRING, editId, L"영상 수정");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, moveId, L"위치 이동");
    AppendMenuW(menu, MF_STRING, resizeId, L"크기 조절");
    AppendMenuW(menu, g_app.entries.empty() && g_app.tags.empty() ? MF_STRING | MF_GRAYED : MF_STRING, deleteId, L"지우기");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, closeId, L"끄기");

    SetForegroundWindow(hwnd);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y + 8, 0, hwnd, nullptr);
    DestroyMenu(menu);
    g_app.armedTarget = AppState::PressTarget::None;

    switch (command) {
    case editId:
        if (cardIndex < g_app.entries.size()) {
            EditEntry(hwnd, cardIndex);
        }
        break;
    case moveId:
        g_app.armedTarget = AppState::PressTarget::Move;
        ApplyCursorForTarget(g_app.armedTarget);
        break;
    case resizeId:
        g_app.armedTarget = AppState::PressTarget::Resize;
        ApplyCursorForTarget(g_app.armedTarget);
        break;
    case deleteId:
        DeleteFromDialog(hwnd);
        break;
    case closeId:
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
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
        AddResult result = ShowVideoDialog(g_app.instance, hwnd, Tags(), g_app.entries, input);
        if (result.tagCreated) {
            ApplyTagAssignments(CreatedTagName(result), result.tagAssignments);
            g_app.activeTag = NormalizedTag(CreatedTagName(result));
            g_app.scrollIndex = 0;
            SaveTags(g_app.tags);
            SaveLinks(g_app.entries);
            RequestRender(hwnd);
        }
        if (result.accepted) {
            if (!result.tagCreated) {
                ApplyTagAssignments(result.tag, result.tagAssignments);
            }
            if (!result.url.empty()) {
                AddEntry(result);
            } else {
                const std::wstring tag = result.tagCreated ? CreatedTagName(result) : result.tag;
                AddKnownTag(tag);
                g_app.activeTag = NormalizedTag(tag);
                g_app.scrollIndex = 0;
                SaveTags(g_app.tags);
                SaveLinks(g_app.entries);
                RequestRender(hwnd);
            }
        }
        break;
    }
    case AppState::PressTarget::Card:
        if (!g_app.mouseMoved && g_app.pressEntryIndex < g_app.entries.size() && PositionInVisible(g_app.pressEntryIndex) == g_app.scrollIndex) {
            OpenEntry(g_app.entries[g_app.pressEntryIndex]);
        } else if (!g_app.mouseMoved && g_app.pressEntryIndex < g_app.entries.size()) {
            int target = PositionInVisible(g_app.pressEntryIndex);
            int count = static_cast<int>(VisibleEntries().size());
            int forward = WrappedVisiblePosition(g_app.scrollIndex + 1, count);
            ScrollBy(target == forward ? 1 : -1);
        }
        break;
    default:
        break;
    }
    if (g_app.pressTarget == AppState::PressTarget::Move || g_app.pressTarget == AppState::PressTarget::Resize) {
        g_app.armedTarget = AppState::PressTarget::None;
        SaveCurrentWindowSettings(hwnd);
    }
    g_app.pressTarget = AppState::PressTarget::None;
    ReleaseCapture();
}

void LoadThumbnails() {
    for (auto& entry : g_app.entries) {
        entry.thumbnail = LoadThumbnail(entry.url);
    }
}

std::wstring ResolveTitle(const std::wstring& title, const std::wstring& url) {
    std::wstring cleanTitle = Sanitized(title);
    if (!cleanTitle.empty() && cleanTitle != L"제목 없음") {
        return cleanTitle;
    }
    std::wstring linkTitle = LoadLinkTitle(url);
    return linkTitle.empty() ? L"제목 없음" : linkTitle;
}

void ResolveMissingTitles() {
    bool changed = false;
    for (auto& entry : g_app.entries) {
        if (entry.title.empty() || entry.title == L"제목 없음") {
            std::wstring title = ResolveTitle(entry.title, entry.url);
            if (title != entry.title) {
                entry.title = title;
                changed = true;
            }
        }
    }
    if (changed) {
        SaveLinks(g_app.entries);
    }
}

Layout BuildLayout(int width, int height) {
    Layout layout;
    layout.tagButton = {12, 58, 96, 26};
    layout.addButton = {112, 58, 26, 26};

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

    int top = 90;
    int bottom = 12;
    int gap = std::clamp(width / 34, 10, 26);
    int availableWidth = std::max(120, width - 8);
    int availableHeight = std::max(96, height - top - bottom);
    double sideScale = 0.78;
    double cardAspect = 1.52;

    double widthRatio = 1.0;
    if (count == 2) {
        widthRatio += sideScale;
    } else if (count > 2) {
        widthRatio += sideScale * 2.0;
    }

    int gapCount = count == 1 ? 0 : (count == 2 ? 1 : 2);
    int widthLimit = static_cast<int>((availableWidth - gap * gapCount) / widthRatio);
    int heightLimit = static_cast<int>(availableHeight / cardAspect);
    int centerWidth = std::max(82, std::min(widthLimit, heightLimit));
    int sideWidth = std::max(64, static_cast<int>(centerWidth * sideScale));
    int centerHeight = std::clamp(static_cast<int>(centerWidth * cardAspect), 112, availableHeight);
    int sideHeight = std::clamp(static_cast<int>(centerHeight * 0.82), 92, availableHeight);

    int totalWidth = centerWidth;
    if (count == 2) {
        totalWidth += gap + sideWidth;
    } else if (count > 2) {
        totalWidth += (gap + sideWidth) * 2;
    }

    int groupX = (width - totalWidth) / 2;
    int centerX = (width - centerWidth) / 2;
    if (count > 2) {
        centerX = groupX + sideWidth + gap;
    }
    int centerY = top + (availableHeight - centerHeight) / 2;
    int sideY = centerY + (centerHeight - sideHeight) / 2;
    int leftX = centerX - gap - sideWidth;
    int rightX = centerX + centerWidth + gap;

    auto wrapIndex = [&visible, count](int index) {
        return visible[static_cast<size_t>(WrappedVisiblePosition(index, count))];
    };

    if (count == 1) {
        layout.cards.push_back({visible[0], {centerX, centerY, centerWidth, centerHeight}, true});
    } else if (g_app.carouselAnimating) {
        double progress = std::clamp(g_app.animationProgress, 0.0, 1.0);
        RectI centerRect{centerX, centerY, centerWidth, centerHeight};
        RectI leftRect{leftX, sideY, sideWidth, sideHeight};
        RectI rightRect{rightX, sideY, sideWidth, sideHeight};
        RectI offLeft{leftX - sideWidth - gap, sideY, sideWidth, sideHeight};
        RectI offRight{rightX + sideWidth + gap, sideY, sideWidth, sideHeight};

        int from = g_app.animationFromIndex;
        if (g_app.animationDirection > 0) {
            if (count > 2) {
                layout.cards.push_back({wrapIndex(from - 1), InterpolatedRect(leftRect, offLeft, progress), false});
                layout.cards.push_back({wrapIndex(from + 2), InterpolatedRect(offRight, rightRect, progress), false});
            }
            layout.cards.push_back({wrapIndex(from), InterpolatedRect(centerRect, leftRect, progress), progress < 0.5});
            layout.cards.push_back({wrapIndex(from + 1), InterpolatedRect(rightRect, centerRect, progress), progress >= 0.5});
        } else {
            if (count > 2) {
                layout.cards.push_back({wrapIndex(from + 1), InterpolatedRect(rightRect, offRight, progress), false});
                layout.cards.push_back({wrapIndex(from - 2), InterpolatedRect(offLeft, leftRect, progress), false});
            }
            layout.cards.push_back({wrapIndex(from), InterpolatedRect(centerRect, rightRect, progress), progress < 0.5});
            layout.cards.push_back({wrapIndex(from - 1), InterpolatedRect(leftRect, centerRect, progress), progress >= 0.5});
        }
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

bool TextFits(Graphics& graphics, const std::wstring& text, RectF bounds, float fontSize, StringFormat& format) {
    Font font(L"Malgun Gothic", fontSize, FontStyleRegular, UnitPixel);
    RectF measured;
    graphics.MeasureString(text.c_str(), -1, &font, bounds, &format, &measured);
    return measured.Width <= bounds.Width + 2.0f && measured.Height <= bounds.Height + 2.0f;
}

float FitTextSize(Graphics& graphics, const std::wstring& text, RectF bounds, float preferredSize, float minimumSize, StringFormat& format) {
    for (float size = preferredSize; size >= minimumSize; size -= 0.5f) {
        if (TextFits(graphics, text, bounds, size, format)) {
            return size;
        }
    }
    return minimumSize;
}

struct TitleLayout {
    RectI bar;
    RectF textBounds;
    float fontSize{};
};

TitleLayout BuildTitleLayout(Graphics& graphics, const RectI& rect, const std::wstring& title, bool active) {
    int baseHeight = std::clamp(rect.h / 6, 34, 72);
    int maxHeight = std::max(baseHeight, rect.h - 4);
    int horizontalPadding = std::clamp(rect.w / 14, 8, 22);
    float preferredSize = std::clamp(static_cast<float>(baseHeight) * (active ? 0.42f : 0.38f), 12.0f, 26.0f);
    float minimumSize = 5.0f;

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingNone);

    int probePadding = std::clamp(baseHeight / 9, 3, 10);
    RectF probeBounds(0.0f, 0.0f, static_cast<float>(std::max(1, rect.w - horizontalPadding * 2)), 4096.0f);
    Font preferredFont(L"Malgun Gothic", preferredSize, FontStyleRegular, UnitPixel);
    RectF measured;
    graphics.MeasureString(title.c_str(), -1, &preferredFont, probeBounds, &format, &measured);
    int measuredHeight = static_cast<int>(measured.Height + 0.999f) + probePadding * 2;
    int fittedHeight = std::clamp(measuredHeight, baseHeight, maxHeight);

    for (int height = fittedHeight; height <= maxHeight; height += 4) {
        int verticalPadding = std::clamp(height / 9, 3, 10);
        RectF bounds(static_cast<float>(rect.x + horizontalPadding), static_cast<float>(rect.y + rect.h - height + verticalPadding), static_cast<float>(std::max(1, rect.w - horizontalPadding * 2)), static_cast<float>(std::max(1, height - verticalPadding * 2)));
        if (TextFits(graphics, title, bounds, preferredSize, format)) {
            return {{rect.x, rect.y + rect.h - height, rect.w, height}, bounds, preferredSize};
        }
        fittedHeight = height;
    }

    int verticalPadding = std::clamp(fittedHeight / 9, 3, 10);
    RectF bounds(static_cast<float>(rect.x + horizontalPadding), static_cast<float>(rect.y + rect.h - fittedHeight + verticalPadding), static_cast<float>(std::max(1, rect.w - horizontalPadding * 2)), static_cast<float>(std::max(1, fittedHeight - verticalPadding * 2)));
    float fontSize = FitTextSize(graphics, title, bounds, preferredSize, minimumSize, format);
    return {{rect.x, rect.y + rect.h - fittedHeight, rect.w, fittedHeight}, bounds, fontSize};
}

void DrawTitleText(Graphics& graphics, const std::wstring& title, const TitleLayout& layout) {
    Font font(L"Malgun Gothic", layout.fontSize, FontStyleRegular, UnitPixel);
    SolidBrush brush(Color(255, 245, 248, 252));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingNone);
    graphics.DrawString(title.c_str(), -1, &font, layout.textBounds, &format, &brush);
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

    std::wstring title = entry.title.empty() ? L"제목 없음" : entry.title;
    TitleLayout titleLayout = BuildTitleLayout(graphics, rect, title, card.active);
    SolidBrush titleBrush(card.active ? Color(255, 92, 109, 132) : Color(255, 84, 95, 112));
    graphics.FillRectangle(&titleBrush, titleLayout.bar.x, titleLayout.bar.y, titleLayout.bar.w, titleLayout.bar.h);
    DrawTitleText(graphics, title, titleLayout);
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
    entry.title = ResolveTitle(result.title, result.url);
    entry.url = result.url;
    entry.tag = NormalizedTag(result.tag);
    entry.preview = result.preview;
    AddKnownTag(entry.tag);
    entry.thumbnail = LoadThumbnail(entry.url);
    g_app.entries.push_back(std::move(entry));
    g_app.activeTag = NormalizedTag(g_app.entries.back().tag);
    g_app.scrollIndex = PositionInVisible(g_app.entries.size() - 1);
    KillTimer(g_app.window, kCarouselTimerId);
    g_app.carouselAnimating = false;
    g_app.animationProgress = 0.0;
    SaveLinks(g_app.entries);
    SaveTags(g_app.tags);
    RequestRender(g_app.window);
}

void OpenUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenEntry(const LinkEntry& entry) {
    OpenUrl(entry.preview ? PreviewUrl(entry.url) : entry.url);
}

void ScrollBy(int delta) {
    int count = static_cast<int>(VisibleEntries().size());
    if (count <= 1) {
        return;
    }
    if (g_app.carouselAnimating) {
        return;
    }

    int direction = delta < 0 ? -1 : 1;
    int from = WrappedVisiblePosition(g_app.scrollIndex, count);
    int to = WrappedVisiblePosition(from + direction, count);
    if (from == to) {
        return;
    }

    g_app.animationFromIndex = from;
    g_app.animationToIndex = to;
    g_app.animationDirection = direction;
    g_app.animationStarted = GetTickCount();
    g_app.animationProgress = 0.0;
    g_app.carouselAnimating = true;
    SetTimer(g_app.window, kCarouselTimerId, 16, nullptr);
    RequestRender(g_app.window);
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
        POINT screenPoint = point;
        ClientToScreen(hwnd, &screenPoint);
        ShowContextMenu(hwnd, point, screenPoint);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_app.mouseDown && reinterpret_cast<HWND>(lParam) != hwnd) {
            g_app.mouseDown = false;
            g_app.pressTarget = AppState::PressTarget::None;
            g_app.armedTarget = AppState::PressTarget::None;
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
    case WM_TIMER:
        if (wParam == kCarouselTimerId && g_app.carouselAnimating) {
            DWORD elapsed = GetTickCount() - g_app.animationStarted;
            double t = std::min(1.0, static_cast<double>(elapsed) / static_cast<double>(kCarouselDurationMs));
            g_app.animationProgress = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
            if (t >= 1.0) {
                KillTimer(hwnd, kCarouselTimerId);
                g_app.carouselAnimating = false;
                g_app.animationProgress = 0.0;
                g_app.scrollIndex = g_app.animationToIndex;
            }
            RequestRender(hwnd);
            return 0;
        }
        break;
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
        SaveTags(g_app.tags);
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
    CreateDesktopShortcut();

    g_app.tags = LoadTags();
    g_app.entries = LoadLinks();
    ResolveMissingTitles();
    LoadThumbnails();

    g_app.settings = LoadWindowSettings();
    if (!g_app.settings.valid) {
        g_app.settings = DefaultWindowSettings();
    }
    g_app.settings = EnsureVisible(g_app.settings);

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
