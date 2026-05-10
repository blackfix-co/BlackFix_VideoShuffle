#include "delete_dialog.hpp"

#include <windowsx.h>

#include <limits>

namespace {

constexpr wchar_t kDeleteDialogClassName[] = L"BlackFixVideoShuffleDeleteDialog";
constexpr int kListId = 201;
constexpr int kOkId = 202;
constexpr int kCancelId = 203;

struct DeleteChoice {
    size_t index{};
    std::wstring label;
};

struct DeleteDialogState {
    HINSTANCE instance{};
    HWND list{};
    HFONT font{};
    std::vector<DeleteChoice> choices;
    size_t result{std::numeric_limits<size_t>::max()};
};

HMENU MenuId(int value) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
}

void ApplyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring EntryLabel(const LinkEntry& entry) {
    return (entry.title.empty() ? L"제목 없음" : entry.title) + L"  [" + (entry.tag.empty() ? L"전체" : entry.tag) + L"]";
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
        state->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 16, 16, 398, 218, hwnd, MenuId(kListId), state->instance, nullptr);
        HWND okButton = CreateWindowW(L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 248, 246, 78, 30, hwnd, MenuId(kOkId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 336, 246, 78, 30, hwnd, MenuId(kCancelId), state->instance, nullptr);
        ApplyFont(state->list, state->font);
        ApplyFont(okButton, state->font);
        ApplyFont(cancelButton, state->font);
        for (const auto& choice : state->choices) {
            SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.c_str()));
        }
        if (!state->choices.empty()) {
            SendMessageW(state->list, LB_SETCURSEL, 0, 0);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kOkId: {
            int selected = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
            if (selected < 0 || selected >= static_cast<int>(state->choices.size())) {
                return 0;
            }
            if (MessageBoxW(hwnd, L"정말 지우시겠습니까?", L"BlackFix VideoShuffle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                state->result = state->choices[static_cast<size_t>(selected)].index;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case kCancelId:
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

size_t ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::wstring& activeTag) {
    static bool registered = false;
    if (!registered) {
        RegisterDeleteDialogClass(instance);
        registered = true;
    }

    DeleteDialogState state;
    state.instance = instance;

    for (size_t i = 0; i < entries.size(); ++i) {
        std::wstring tag = entries[i].tag.empty() ? L"전체" : entries[i].tag;
        if (tag == activeTag && activeTag != L"전체") {
            state.choices.push_back({i, L"[현재 태그] " + EntryLabel(entries[i])});
        }
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        std::wstring tag = entries[i].tag.empty() ? L"전체" : entries[i].tag;
        if (tag == L"전체") {
            state.choices.push_back({i, L"[전체 태그] " + EntryLabel(entries[i])});
        }
    }

    if (state.choices.empty()) {
        MessageBoxW(parent, L"삭제할 영상이 없습니다.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
        return std::numeric_limits<size_t>::max();
    }

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 448;
    int height = 326;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDeleteDialogClassName, L"영상 삭제", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, instance, &state);
    if (!dialog) {
        return std::numeric_limits<size_t>::max();
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
