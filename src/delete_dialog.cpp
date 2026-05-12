#include "delete_dialog.hpp"

#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

namespace {

constexpr wchar_t kDeleteDialogClassName[] = L"BlackFixVideoShuffleDeleteDialog";
constexpr int kOkId = 203;
constexpr int kCancelId = 204;
constexpr int kRootCheckId = 3000;
constexpr int kTagCheckBaseId = 3100;
constexpr int kContentTop = 16;
constexpr int kContentBottom = 386;
constexpr int kContentLeft = 16;
constexpr COLORREF kDarkBackground = RGB(24, 26, 32);
constexpr COLORREF kDarkPanel = RGB(34, 37, 45);
constexpr COLORREF kDarkText = RGB(255, 255, 255);
constexpr DWORD kDarkTitleBarAttribute = 20;

struct DeleteChoice {
    size_t index{};
    std::wstring tag;
    std::wstring label;
};

struct ItemControl {
    size_t index{};
    std::wstring tag;
    HWND check{};
    int x{};
    int y{};
};

struct TagControl {
    std::wstring tag;
    HWND check{};
    int y{};
};

struct DeleteDialogState {
    HINSTANCE instance{};
    HWND rootCheck{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    HBRUSH panelBrush{};
    std::vector<DeleteChoice> choices;
    std::vector<std::wstring> tagOrder;
    std::vector<TagControl> tags;
    std::vector<ItemControl> items;
    DeleteResult result;
    int contentHeight{};
    int scrollOffset{};
};

HMENU MenuId(int value) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
}

void ApplyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void ApplyDarkWindow(HWND hwnd) {
    BOOL enabled = TRUE;
    DwmSetWindowAttribute(hwnd, kDarkTitleBarAttribute, &enabled, sizeof(enabled));
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

HBRUSH ApplyControlColors(WPARAM wParam, HBRUSH brush, COLORREF background) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, kDarkText);
    SetBkColor(dc, background);
    SetBkMode(dc, TRANSPARENT);
    return brush;
}

void DrawDarkButton(const DRAWITEMSTRUCT* item) {
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    COLORREF fillColor = pressed ? RGB(48, 52, 63) : RGB(34, 37, 45);
    COLORREF borderColor = RGB(80, 86, 102);
    COLORREF textColor = disabled ? RGB(130, 135, 146) : kDarkText;

    HBRUSH fill = CreateSolidBrush(fillColor);
    FillRect(item->hDC, &item->rcItem, fill);
    DeleteObject(fill);

    HBRUSH border = CreateSolidBrush(borderColor);
    FrameRect(item->hDC, &item->rcItem, border);
    DeleteObject(border);

    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(sizeof(text) / sizeof(text[0])));
    SetTextColor(item->hDC, textColor);
    SetBkMode(item->hDC, TRANSPARENT);
    RECT textRect = item->rcItem;
    DrawTextW(item->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

std::wstring NormalizedTagName(const LinkEntry& entry) {
    return entry.tag.empty() ? L"전체" : entry.tag;
}

std::wstring EntryLabel(const LinkEntry& entry) {
    return entry.title.empty() ? L"제목 없음" : entry.title;
}

int PageHeight() {
    return kContentBottom - kContentTop;
}

void MoveScrolled(HWND hwnd, int x, int baseY, int w, int h, int offset) {
    int y = kContentTop + baseY - offset;
    MoveWindow(hwnd, x, y, w, h, TRUE);
    ShowWindow(hwnd, y + h >= kContentTop && y <= kContentBottom ? SW_SHOW : SW_HIDE);
}

void LayoutDeleteControls(HWND hwnd, DeleteDialogState* state) {
    MoveScrolled(state->rootCheck, kContentLeft, 0, 430, 26, state->scrollOffset);
    for (const auto& tag : state->tags) {
        MoveScrolled(tag.check, kContentLeft + 18, tag.y, 420, 24, state->scrollOffset);
    }
    for (const auto& item : state->items) {
        MoveScrolled(item.check, kContentLeft + item.x, item.y, 420, 24, state->scrollOffset);
    }

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, state->contentHeight - 1);
    info.nPage = static_cast<UINT>(PageHeight());
    info.nPos = state->scrollOffset;
    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
}

void SetScrollOffset(HWND hwnd, DeleteDialogState* state, int offset) {
    int maxOffset = std::max(0, state->contentHeight - PageHeight());
    state->scrollOffset = std::clamp(offset, 0, maxOffset);
    LayoutDeleteControls(hwnd, state);
}

void SetAllChecked(DeleteDialogState* state, bool checked) {
    SendMessageW(state->rootCheck, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    for (const auto& tag : state->tags) {
        SendMessageW(tag.check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    for (const auto& item : state->items) {
        SendMessageW(item.check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SetTagChecked(DeleteDialogState* state, const std::wstring& tagName, bool checked) {
    for (const auto& item : state->items) {
        if (item.tag == tagName) {
            SendMessageW(item.check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
}

std::vector<size_t> IndicesForTag(DeleteDialogState* state, const std::wstring& tagName) {
    std::vector<size_t> indices;
    for (const auto& item : state->items) {
        if (item.tag == tagName) {
            indices.push_back(item.index);
        }
    }
    return indices;
}

std::vector<size_t> AllIndices(DeleteDialogState* state) {
    std::vector<size_t> indices;
    for (const auto& item : state->items) {
        indices.push_back(item.index);
    }
    return indices;
}

std::vector<size_t> CheckedIndices(DeleteDialogState* state) {
    std::vector<size_t> selected;
    for (const auto& item : state->items) {
        if (SendMessageW(item.check, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            selected.push_back(item.index);
        }
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

std::vector<std::wstring> AllTagNames(DeleteDialogState* state) {
    std::vector<std::wstring> tags;
    for (const auto& tag : state->tags) {
        if (tag.tag != L"전체") {
            tags.push_back(tag.tag);
        }
    }
    return tags;
}

void ConfirmAndClose(HWND hwnd, DeleteDialogState* state, std::vector<size_t> indices, std::vector<std::wstring> deletedTags = {}) {
    if (indices.empty() && deletedTags.empty()) {
        MessageBoxW(hwnd, L"지울 영상을 선택해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring messageText = indices.empty()
        ? std::to_wstring(deletedTags.size()) + L"개 태그를 정말 지우시겠습니까?"
        : std::to_wstring(indices.size()) + L"개 영상을 정말 지우시겠습니까?";
    if (MessageBoxW(hwnd, messageText.c_str(), L"BlackFix VideoShuffle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        state->result.indices = std::move(indices);
        state->result.deletedTags = std::move(deletedTags);
        DestroyWindow(hwnd);
    }
}

void ConfirmTagDelete(HWND hwnd, DeleteDialogState* state, const std::wstring& tagName) {
    std::vector<size_t> indices = IndicesForTag(state, tagName);
    if (indices.empty() && tagName == L"전체") {
        return;
    }
    std::wstring messageText = indices.empty()
        ? tagName + L" 태그를 지우시겠습니까?"
        : tagName + L" 태그의 " + std::to_wstring(indices.size()) + L"개 영상을 지우시겠습니까?";
    if (MessageBoxW(hwnd, messageText.c_str(), L"BlackFix VideoShuffle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        state->result.indices = std::move(indices);
        if (tagName != L"전체") {
            state->result.deletedTags.push_back(tagName);
        }
        DestroyWindow(hwnd);
    }
}

void CreateTreeControls(HWND hwnd, DeleteDialogState* state) {
    int y = 0;
    state->rootCheck = CreateWindowW(L"BUTTON", L"전체태그", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_NOTIFY, 0, 0, 1, 1, hwnd, MenuId(kRootCheckId), state->instance, nullptr);
    ApplyFont(state->rootCheck, state->font);
    ApplyDarkWindow(state->rootCheck);
    y += 30;

    for (const auto& choice : state->choices) {
        if (choice.tag != L"전체") {
            continue;
        }
        std::wstring itemLabel = L"- " + choice.label;
        HWND itemCheck = CreateWindowW(L"BUTTON", itemLabel.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 1, 1, hwnd, nullptr, state->instance, nullptr);
        ApplyFont(itemCheck, state->font);
        ApplyDarkWindow(itemCheck);
        state->items.push_back({choice.index, choice.tag, itemCheck, 18, y});
        y += 26;
    }

    size_t tagIndex = 0;
    for (const auto& tagName : state->tagOrder) {
        if (tagName == L"전체") {
            continue;
        }
        std::wstring label = L"- " + tagName;
        HWND tagCheck = CreateWindowW(L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_NOTIFY, 0, 0, 1, 1, hwnd, MenuId(kTagCheckBaseId + static_cast<int>(tagIndex)), state->instance, nullptr);
        ApplyFont(tagCheck, state->font);
        ApplyDarkWindow(tagCheck);
        state->tags.push_back({tagName, tagCheck, y});
        y += 26;
        ++tagIndex;

        for (const auto& choice : state->choices) {
            if (choice.tag != tagName) {
                continue;
            }

            std::wstring itemLabel = L"- " + choice.label;
            HWND itemCheck = CreateWindowW(L"BUTTON", itemLabel.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 1, 1, hwnd, nullptr, state->instance, nullptr);
            ApplyFont(itemCheck, state->font);
            ApplyDarkWindow(itemCheck);
            state->items.push_back({choice.index, choice.tag, itemCheck, 42, y});
            y += 26;
        }
    }

    state->contentHeight = y + 4;
    LayoutDeleteControls(hwnd, state);
}

LRESULT CALLBACK DeleteDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    DeleteDialogState* state = reinterpret_cast<DeleteDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<DeleteDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        ApplyDarkWindow(hwnd);
        state->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        state->backgroundBrush = CreateSolidBrush(kDarkBackground);
        state->panelBrush = CreateSolidBrush(kDarkPanel);
        HWND okButton = CreateWindowW(L"BUTTON", L"지우기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 338, 400, 78, 30, hwnd, MenuId(kOkId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 426, 400, 78, 30, hwnd, MenuId(kCancelId), state->instance, nullptr);
        ApplyFont(okButton, state->font);
        ApplyFont(cancelButton, state->font);
        ApplyDarkWindow(okButton);
        ApplyDarkWindow(cancelButton);
        CreateTreeControls(hwnd, state);
        return 0;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(state && state->backgroundBrush ? state->backgroundBrush : GetStockObject(BLACK_BRUSH));
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(ApplyControlColors(wParam, state && state->backgroundBrush ? state->backgroundBrush : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), kDarkBackground));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return reinterpret_cast<LRESULT>(ApplyControlColors(wParam, state && state->panelBrush ? state->panelBrush : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), kDarkPanel));
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->CtlType == ODT_BUTTON) {
            DrawDarkButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == kRootCheckId) {
            if (code == BN_DBLCLK) {
                ConfirmAndClose(hwnd, state, AllIndices(state), AllTagNames(state));
                return 0;
            }
            SetAllChecked(state, SendMessageW(state->rootCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }
        if (id >= kTagCheckBaseId && id < kTagCheckBaseId + static_cast<int>(state->tags.size())) {
            const auto& tag = state->tags[static_cast<size_t>(id - kTagCheckBaseId)];
            if (code == BN_DBLCLK) {
                ConfirmTagDelete(hwnd, state, tag.tag);
                return 0;
            }
            SetTagChecked(state, tag.tag, SendMessageW(tag.check, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }

        switch (id) {
        case kOkId:
            ConfirmAndClose(hwnd, state, CheckedIndices(state));
            return 0;
        case kCancelId:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_VSCROLL:
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            SetScrollOffset(hwnd, state, state->scrollOffset - 24);
            return 0;
        case SB_LINEDOWN:
            SetScrollOffset(hwnd, state, state->scrollOffset + 24);
            return 0;
        case SB_PAGEUP:
            SetScrollOffset(hwnd, state, state->scrollOffset - PageHeight());
            return 0;
        case SB_PAGEDOWN:
            SetScrollOffset(hwnd, state, state->scrollOffset + PageHeight());
            return 0;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &info);
            SetScrollOffset(hwnd, state, info.nTrackPos);
            return 0;
        }
        default:
            break;
        }
        break;
    case WM_MOUSEWHEEL:
        SetScrollOffset(hwnd, state, state->scrollOffset - GET_WHEEL_DELTA_WPARAM(wParam) / 4);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state && state->font) {
            DeleteObject(state->font);
            state->font = nullptr;
        }
        if (state && state->backgroundBrush) {
            DeleteObject(state->backgroundBrush);
            state->backgroundBrush = nullptr;
        }
        if (state && state->panelBrush) {
            DeleteObject(state->panelBrush);
            state->panelBrush = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterDeleteDialogClass(HINSTANCE instance) {
    WNDCLASSEXW dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.lpfnWndProc = DeleteDialogProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    dialogClass.lpszClassName = kDeleteDialogClassName;
    RegisterClassExW(&dialogClass);
}

}

DeleteResult ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::vector<std::wstring>& tags, const std::wstring&) {
    static bool registered = false;
    if (!registered) {
        RegisterDeleteDialogClass(instance);
        registered = true;
    }

    DeleteDialogState state;
    state.instance = instance;

    for (size_t i = 0; i < entries.size(); ++i) {
        std::wstring tag = NormalizedTagName(entries[i]);
        state.choices.push_back({i, tag, EntryLabel(entries[i])});
    }

    std::sort(state.choices.begin(), state.choices.end(), [](const DeleteChoice& left, const DeleteChoice& right) {
        if (left.tag == L"전체" && right.tag != L"전체") {
            return true;
        }
        if (right.tag == L"전체" && left.tag != L"전체") {
            return false;
        }
        if (left.tag != right.tag) {
            return left.tag < right.tag;
        }
        return left.index < right.index;
    });

    auto addTag = [&state](const std::wstring& tag) {
        std::wstring clean = tag.empty() ? L"전체" : tag;
        if (clean != L"전체" && std::find(state.tagOrder.begin(), state.tagOrder.end(), clean) == state.tagOrder.end()) {
            state.tagOrder.push_back(clean);
        }
    };

    for (const auto& choice : state.choices) {
        addTag(choice.tag);
    }
    for (const auto& tag : tags) {
        if (tag != L"전체") {
            addTag(tag);
        }
    }
    std::stable_sort(state.tagOrder.begin(), state.tagOrder.end(), [](const std::wstring& left, const std::wstring& right) {
        if (left == L"전체" && right != L"전체") {
            return true;
        }
        if (right == L"전체" && left != L"전체") {
            return false;
        }
        return left < right;
    });

    if (state.choices.empty() && state.tagOrder.empty()) {
        MessageBoxW(parent, L"삭제할 영상이나 태그가 없습니다.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
        return {};
    }

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 536;
    int height = 478;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDeleteDialogClassName, L"영상 삭제", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VSCROLL, x, y, width, height, parent, nullptr, instance, &state);
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
