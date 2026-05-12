#include "delete_dialog.hpp"

#include <windowsx.h>

#include <algorithm>

namespace {

constexpr wchar_t kDeleteDialogClassName[] = L"BlackFixVideoShuffleDeleteDialog";
constexpr int kDeleteAllId = 201;
constexpr int kClearId = 202;
constexpr int kOkId = 203;
constexpr int kCancelId = 204;
constexpr int kTagButtonBaseId = 3000;
constexpr int kContentTop = 54;
constexpr int kContentBottom = 386;
constexpr int kContentLeft = 16;
constexpr int kContentWidth = 488;

struct DeleteChoice {
    size_t index{};
    std::wstring tag;
    std::wstring label;
};

struct ItemControl {
    size_t index{};
    std::wstring tag;
    HWND check{};
    int y{};
};

struct GroupControl {
    std::wstring tag;
    HWND label{};
    HWND button{};
    int y{};
};

struct DeleteDialogState {
    HINSTANCE instance{};
    HFONT font{};
    std::vector<DeleteChoice> choices;
    std::vector<GroupControl> groups;
    std::vector<ItemControl> items;
    std::vector<size_t> result;
    int contentHeight{};
    int scrollOffset{};
};

HMENU MenuId(int value) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
}

void ApplyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    for (const auto& group : state->groups) {
        MoveScrolled(group.label, kContentLeft, group.y, 320, 24, state->scrollOffset);
        MoveScrolled(group.button, kContentLeft + 354, group.y - 2, 70, 26, state->scrollOffset);
    }
    for (const auto& item : state->items) {
        MoveScrolled(item.check, kContentLeft + 18, item.y, 440, 24, state->scrollOffset);
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

void SetGroupChecked(DeleteDialogState* state, const std::wstring& tag, bool checked) {
    for (const auto& item : state->items) {
        if (item.tag == tag) {
            SendMessageW(item.check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
}

void SetAllChecked(DeleteDialogState* state, bool checked) {
    for (const auto& item : state->items) {
        SendMessageW(item.check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
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

void ConfirmAndClose(HWND hwnd, DeleteDialogState* state, std::vector<size_t> indices) {
    if (indices.empty()) {
        MessageBoxW(hwnd, L"지울 영상을 선택해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring messageText = std::to_wstring(indices.size()) + L"개 영상을 정말 지우시겠습니까?";
    if (MessageBoxW(hwnd, messageText.c_str(), L"BlackFix VideoShuffle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        state->result = std::move(indices);
        DestroyWindow(hwnd);
    }
}

void CreateGroupedControls(HWND hwnd, DeleteDialogState* state) {
    int y = 0;
    std::wstring currentTag;
    size_t groupIndex = 0;

    for (const auto& choice : state->choices) {
        if (choice.tag != currentTag) {
            currentTag = choice.tag;
            HWND label = CreateWindowW(L"STATIC", currentTag.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr, state->instance, nullptr);
            HWND button = CreateWindowW(L"BUTTON", L"선택", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hwnd, MenuId(kTagButtonBaseId + static_cast<int>(groupIndex)), state->instance, nullptr);
            ApplyFont(label, state->font);
            ApplyFont(button, state->font);
            state->groups.push_back({currentTag, label, button, y});
            y += 28;
            ++groupIndex;
        }

        HWND check = CreateWindowW(L"BUTTON", choice.label.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 1, 1, hwnd, nullptr, state->instance, nullptr);
        ApplyFont(check, state->font);
        state->items.push_back({choice.index, choice.tag, check, y});
        y += 26;
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
        state->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        HWND deleteAllButton = CreateWindowW(L"BUTTON", L"전체삭제", WS_CHILD | WS_VISIBLE, 16, 14, 84, 28, hwnd, MenuId(kDeleteAllId), state->instance, nullptr);
        HWND clearButton = CreateWindowW(L"BUTTON", L"선택해제", WS_CHILD | WS_VISIBLE, 108, 14, 84, 28, hwnd, MenuId(kClearId), state->instance, nullptr);
        HWND okButton = CreateWindowW(L"BUTTON", L"지우기", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 338, 400, 78, 30, hwnd, MenuId(kOkId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 426, 400, 78, 30, hwnd, MenuId(kCancelId), state->instance, nullptr);
        for (HWND control : {deleteAllButton, clearButton, okButton, cancelButton}) {
            ApplyFont(control, state->font);
        }
        CreateGroupedControls(hwnd, state);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= kTagButtonBaseId && id < kTagButtonBaseId + static_cast<int>(state->groups.size())) {
            SetGroupChecked(state, state->groups[static_cast<size_t>(id - kTagButtonBaseId)].tag, true);
            return 0;
        }

        switch (id) {
        case kDeleteAllId: {
            std::vector<size_t> all;
            all.reserve(state->items.size());
            for (const auto& item : state->items) {
                all.push_back(item.index);
            }
            ConfirmAndClose(hwnd, state, std::move(all));
            return 0;
        }
        case kClearId:
            SetAllChecked(state, false);
            return 0;
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
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dialogClass.lpszClassName = kDeleteDialogClassName;
    RegisterClassExW(&dialogClass);
}

}

std::vector<size_t> ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::wstring&) {
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
        if (left.tag != right.tag) {
            return left.tag < right.tag;
        }
        return left.index < right.index;
    });

    if (state.choices.empty()) {
        MessageBoxW(parent, L"삭제할 영상이 없습니다.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
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
