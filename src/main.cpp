#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr COLORREF kTransparentColor = RGB(255, 0, 255);
constexpr wchar_t kMainClassName[] = L"BlackFixVideoShuffleWindow";
constexpr wchar_t kDialogClassName[] = L"BlackFixVideoShuffleAddDialog";
constexpr int kTitleEditId = 101;
constexpr int kUrlEditId = 102;
constexpr int kAddButtonId = 103;
constexpr int kCancelButtonId = 104;

struct RectI {
    int x{};
    int y{};
    int w{};
    int h{};

    bool contains(POINT point) const {
        return point.x >= x && point.x < x + w && point.y >= y && point.y < y + h;
    }
};

struct Layout {
    RectI addButton;
    std::vector<RectI> cards;
};

struct LinkEntry {
    std::wstring title;
    std::wstring url;
    std::unique_ptr<Bitmap> thumbnail;
};

struct AppState {
    HINSTANCE instance{};
    HWND window{};
    ULONG_PTR gdiplusToken{};
    std::vector<LinkEntry> entries;
};

struct AddResult {
    bool accepted{};
    std::wstring title;
    std::wstring url;
};

struct AddDialogState {
    HWND parent{};
    HWND titleEdit{};
    HWND urlEdit{};
    HFONT font{};
    AddResult result;
};

struct HttpHandle {
    HINTERNET value{};

    explicit HttpHandle(HINTERNET handle = nullptr) : value(handle) {}

    ~HttpHandle() {
        if (value) {
            WinHttpCloseHandle(value);
        }
    }

    operator HINTERNET() const {
        return value;
    }
};

AppState g_app;

std::wstring currentExePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(size);
    return path;
}

std::filesystem::path appDirectory() {
    PWSTR rawPath = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &rawPath))) {
        path = rawPath;
        CoTaskMemFree(rawPath);
    } else {
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        path = buffer;
    }
    path /= L"BlackFix_VideoShuffle";
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path dataFilePath() {
    return appDirectory() / L"links.tsv";
}

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string output(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), length, nullptr, nullptr);
    return output;
}

std::wstring fromUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring output(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), length);
    return output;
}

std::wstring trimmed(std::wstring value) {
    auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!value.empty() && isSpace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(value.back())) {
        value.pop_back();
    }
    return value;
}

std::wstring sanitized(std::wstring value) {
    std::replace(value.begin(), value.end(), L'\t', L' ');
    std::replace(value.begin(), value.end(), L'\r', L' ');
    std::replace(value.begin(), value.end(), L'\n', L' ');
    return trimmed(value);
}

std::wstring lowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring extractVideoIdAt(const std::wstring& url, size_t start) {
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

std::wstring youtubeVideoId(const std::wstring& url) {
    std::wstring lower = lowerCopy(url);
    const std::vector<std::wstring> segmentPatterns = {
        L"youtu.be/",
        L"/shorts/",
        L"/embed/",
        L"/live/"
    };
    for (const auto& pattern : segmentPatterns) {
        size_t pos = lower.find(pattern);
        if (pos != std::wstring::npos) {
            return extractVideoIdAt(url, pos + pattern.size());
        }
    }
    size_t query = lower.find(L"v=");
    if (query != std::wstring::npos) {
        return extractVideoIdAt(url, query + 2);
    }
    return {};
}

std::wstring thumbnailUrlFor(const std::wstring& url) {
    std::wstring id = youtubeVideoId(url);
    if (id.empty()) {
        return {};
    }
    return L"https://img.youtube.com/vi/" + id + L"/hqdefault.jpg";
}

std::vector<unsigned char> downloadBytes(const std::wstring& url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        return {};
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    HttpHandle session(WinHttpOpen(L"BlackFix VideoShuffle/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return {};
    }

    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);
    HttpHandle connection(WinHttpConnect(session, host.c_str(), components.nPort, 0));
    if (!connection) {
        return {};
    }

    DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        return {};
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        return {};
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    if (status >= 400) {
        return {};
    }

    std::vector<unsigned char> bytes;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }
        size_t offset = bytes.size();
        bytes.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data() + offset, available, &read)) {
            return {};
        }
        bytes.resize(offset + read);
    }
    return bytes;
}

std::unique_ptr<Bitmap> bitmapFromBytes(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) {
        return {};
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) {
        return {};
    }

    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return {};
    }
    std::memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return {};
    }

    Bitmap image(stream);
    std::unique_ptr<Bitmap> result;
    if (image.GetLastStatus() == Ok && image.GetWidth() > 0 && image.GetHeight() > 0) {
        Bitmap* clone = image.Clone(0, 0, image.GetWidth(), image.GetHeight(), PixelFormat32bppARGB);
        if (clone && clone->GetLastStatus() == Ok) {
            result.reset(clone);
        } else {
            delete clone;
        }
    }

    stream->Release();
    return result;
}

std::unique_ptr<Bitmap> loadThumbnail(const std::wstring& url) {
    std::wstring imageUrl = thumbnailUrlFor(url);
    if (imageUrl.empty()) {
        return {};
    }
    return bitmapFromBytes(downloadBytes(imageUrl));
}

void enableStartup() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    std::wstring command = L"\"" + currentExePath() + L"\"";
    RegSetValueExW(key, L"BlackFix_VideoShuffle", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void saveLinks() {
    std::ofstream file(dataFilePath(), std::ios::binary | std::ios::trunc);
    for (const auto& entry : g_app.entries) {
        file << toUtf8(sanitized(entry.title)) << '\t' << toUtf8(sanitized(entry.url)) << '\n';
    }
}

void loadLinks() {
    std::ifstream file(dataFilePath(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        LinkEntry entry;
        entry.title = sanitized(fromUtf8(std::string_view(line.data(), tab)));
        entry.url = sanitized(fromUtf8(std::string_view(line.data() + tab + 1, line.size() - tab - 1)));
        if (!entry.url.empty()) {
            entry.thumbnail = loadThumbnail(entry.url);
            g_app.entries.push_back(std::move(entry));
        }
    }
}

Layout buildLayout(int width, int height) {
    Layout layout;
    int buttonSize = 42;
    layout.addButton = {std::max(16, width - buttonSize - 28), 22, buttonSize, buttonSize};

    int visibleSlots = std::max<int>(3, static_cast<int>(g_app.entries.size()));
    int margin = 24;
    int gap = 22;
    int targetWidth = 156;
    int availableWidth = std::max(120, width - margin * 2);
    int columns = std::max(1, std::min(visibleSlots, (availableWidth + gap) / (targetWidth + gap)));
    columns = std::min(columns, 5);
    int cardWidth = (availableWidth - gap * (columns - 1)) / columns;
    cardWidth = std::clamp(cardWidth, 96, targetWidth);
    int cardHeight = static_cast<int>(cardWidth * 1.22);
    int rows = (visibleSlots + columns - 1) / columns;
    int totalHeight = rows * cardHeight + (rows - 1) * gap;
    int startY = std::max(82, (height - totalHeight) / 2);

    layout.cards.reserve(visibleSlots);
    for (int row = 0; row < rows; ++row) {
        int remaining = visibleSlots - row * columns;
        int count = std::min(columns, remaining);
        int totalWidth = count * cardWidth + (count - 1) * gap;
        int startX = (width - totalWidth) / 2;
        for (int col = 0; col < count; ++col) {
            layout.cards.push_back({startX + col * (cardWidth + gap), startY + row * (cardHeight + gap), cardWidth, cardHeight});
        }
    }
    return layout;
}

void drawText(Graphics& graphics, const std::wstring& text, RectF bounds, float size, Color color, StringAlignment lineAlignment) {
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

void drawCard(Graphics& graphics, const RectI& rect, const LinkEntry* entry) {
    SolidBrush fallback(entry ? Color(255, 128, 128, 128) : Color(255, 132, 132, 132));
    graphics.FillRectangle(&fallback, rect.x, rect.y, rect.w, rect.h);

    if (entry && entry->thumbnail) {
        graphics.SetClip(Rect(rect.x, rect.y, rect.w, rect.h));
        float imageWidth = static_cast<float>(entry->thumbnail->GetWidth());
        float imageHeight = static_cast<float>(entry->thumbnail->GetHeight());
        float scale = std::max(static_cast<float>(rect.w) / imageWidth, static_cast<float>(rect.h) / imageHeight);
        float sourceWidth = static_cast<float>(rect.w) / scale;
        float sourceHeight = static_cast<float>(rect.h) / scale;
        float sourceX = (imageWidth - sourceWidth) * 0.5f;
        float sourceY = (imageHeight - sourceHeight) * 0.5f;
        graphics.DrawImage(entry->thumbnail.get(), RectF(static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w), static_cast<float>(rect.h)), sourceX, sourceY, sourceWidth, sourceHeight, UnitPixel);
        graphics.ResetClip();
        RectI titleBar{rect.x, rect.y + rect.h - 42, rect.w, 42};
        SolidBrush titleBrush(Color(255, 92, 109, 132));
        graphics.FillRectangle(&titleBrush, titleBar.x, titleBar.y, titleBar.w, titleBar.h);
        drawText(graphics, entry->title, RectF(static_cast<float>(rect.x + 10), static_cast<float>(rect.y + rect.h - 39), static_cast<float>(rect.w - 20), 34.0f), 15.0f, Color(255, 245, 248, 252), StringAlignmentCenter);
    } else if (entry) {
        drawText(graphics, entry->title.empty() ? L"제목 없음" : entry->title, RectF(static_cast<float>(rect.x + 12), static_cast<float>(rect.y + 12), static_cast<float>(rect.w - 24), static_cast<float>(rect.h - 24)), 16.0f, Color(255, 238, 238, 238), StringAlignmentCenter);
    }
}

void drawAddButton(Graphics& graphics, const RectI& rect) {
    SolidBrush brush(Color(255, 110, 126, 150));
    graphics.FillRectangle(&brush, rect.x, rect.y, rect.w, rect.h);
    Pen pen(Color(255, 0, 0, 0), 4.0f);
    float cx = static_cast<float>(rect.x + rect.w / 2);
    float cy = static_cast<float>(rect.y + rect.h / 2);
    graphics.DrawLine(&pen, cx - 13.0f, cy, cx + 13.0f, cy);
    graphics.DrawLine(&pen, cx, cy - 13.0f, cx, cy + 13.0f);
}

void paintWindow(HWND hwnd) {
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

    Layout layout = buildLayout(client.right - client.left, client.bottom - client.top);
    for (size_t i = 0; i < layout.cards.size(); ++i) {
        const LinkEntry* entry = i < g_app.entries.size() ? &g_app.entries[i] : nullptr;
        drawCard(graphics, layout.cards[i], entry);
    }
    drawAddButton(graphics, layout.addButton);

    EndPaint(hwnd, &ps);
}

void resizeToVirtualScreen(HWND hwnd) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring readWindowText(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(length);
    return text;
}

void applyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

LRESULT CALLBACK addDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AddDialogState* state = reinterpret_cast<AddDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AddDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        HWND titleLabel = CreateWindowW(L"STATIC", L"제목", WS_CHILD | WS_VISIBLE, 22, 22, 54, 24, hwnd, nullptr, g_app.instance, nullptr);
        state->titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 18, 260, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTitleEditId)), g_app.instance, nullptr);
        HWND urlLabel = CreateWindowW(L"STATIC", L"링크", WS_CHILD | WS_VISIBLE, 22, 64, 54, 24, hwnd, nullptr, g_app.instance, nullptr);
        state->urlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 60, 260, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUrlEditId)), g_app.instance, nullptr);
        HWND addButton = CreateWindowW(L"BUTTON", L"추가", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 176, 108, 78, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddButtonId)), g_app.instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 264, 108, 78, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelButtonId)), g_app.instance, nullptr);
        for (HWND control : {titleLabel, state->titleEdit, urlLabel, state->urlEdit, addButton, cancelButton}) {
            applyFont(control, state->font);
        }
        SetFocus(state->titleEdit);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kAddButtonId:
        case IDOK:
            state->result.title = sanitized(readWindowText(state->titleEdit));
            state->result.url = sanitized(readWindowText(state->urlEdit));
            if (state->result.url.empty()) {
                MessageBoxW(hwnd, L"링크를 입력해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            state->result.accepted = true;
            DestroyWindow(hwnd);
            return 0;
        case kCancelButtonId:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state && state->font) {
            DeleteObject(state->font);
            state->font = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

AddResult showAddDialog(HWND parent) {
    AddDialogState state;
    state.parent = parent;

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 370;
    int height = 178;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDialogClassName, L"링크 추가", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, g_app.instance, &state);
    if (!dialog) {
        return {};
    }

    EnableWindow(parent, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return state.result;
}

void addEntry(const AddResult& result) {
    LinkEntry entry;
    entry.title = result.title.empty() ? L"제목 없음" : result.title;
    entry.url = result.url;
    entry.thumbnail = loadThumbnail(entry.url);
    g_app.entries.push_back(std::move(entry));
    saveLinks();
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void openUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool pointHitsCard(const Layout& layout, POINT point, size_t& index) {
    for (size_t i = 0; i < layout.cards.size() && i < g_app.entries.size(); ++i) {
        if (layout.cards[i].contains(point)) {
            index = i;
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK mainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetLayeredWindowAttributes(hwnd, kTransparentColor, 255, LWA_COLORKEY);
        resizeToVirtualScreen(hwnd);
        return 0;
    case WM_DISPLAYCHANGE:
        resizeToVirtualScreen(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = buildLayout(client.right - client.left, client.bottom - client.top);
        size_t index = 0;
        if (layout.addButton.contains(point) || pointHitsCard(layout, point, index)) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout layout = buildLayout(client.right - client.left, client.bottom - client.top);
        if (layout.addButton.contains(point)) {
            AddResult result = showAddDialog(hwnd);
            if (result.accepted) {
                addEntry(result);
            }
            return 0;
        }
        size_t index = 0;
        if (pointHitsCard(layout, point, index)) {
            openUrl(g_app.entries[index].url);
            return 0;
        }
        return 0;
    }
    case WM_PAINT:
        paintWindow(hwnd);
        return 0;
    case WM_DESTROY:
        saveLinks();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void registerWindows(HINSTANCE instance) {
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.lpfnWndProc = mainProc;
    mainClass.hInstance = instance;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    mainClass.hbrBackground = CreateSolidBrush(kTransparentColor);
    mainClass.lpszClassName = kMainClassName;
    RegisterClassExW(&mainClass);

    WNDCLASSEXW dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.lpfnWndProc = addDialogProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dialogClass.lpszClassName = kDialogClassName;
    RegisterClassExW(&dialogClass);
}

} 

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_app.instance = instance;

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_app.gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        return 1;
    }

    registerWindows(instance);
    enableStartup();
    loadLinks();

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_app.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kMainClassName, L"BlackFix VideoShuffle", WS_POPUP, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
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

    GdiplusShutdown(g_app.gdiplusToken);
    return static_cast<int>(message.wParam);
}
