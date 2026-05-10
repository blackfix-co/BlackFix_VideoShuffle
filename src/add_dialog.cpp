#include "add_dialog.hpp"

#include "utils.hpp"

#include <windowsx.h>

namespace {

constexpr wchar_t kDialogClassName[] = L"BlackFixVideoShuffleAddDialog";
constexpr int kTitleEditId = 101;
constexpr int kUrlEditId = 102;
constexpr int kAddButtonId = 103;
constexpr int kCancelButtonId = 104;

struct AddDialogState {
    HINSTANCE instance{};
    HWND titleEdit{};
    HWND urlEdit{};
    HFONT font{};
    AddResult result;
};

std::wstring ReadWindowText(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(length);
    return text;
}

void ApplyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HMENU MenuId(int value) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
}

LRESULT CALLBACK AddDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AddDialogState* state = reinterpret_cast<AddDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AddDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        HWND titleLabel = CreateWindowW(L"STATIC", L"제목", WS_CHILD | WS_VISIBLE, 22, 22, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 18, 260, 28, hwnd, MenuId(kTitleEditId), state->instance, nullptr);
        HWND urlLabel = CreateWindowW(L"STATIC", L"링크", WS_CHILD | WS_VISIBLE, 22, 64, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->urlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 60, 260, 28, hwnd, MenuId(kUrlEditId), state->instance, nullptr);
        HWND addButton = CreateWindowW(L"BUTTON", L"추가", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 176, 108, 78, 30, hwnd, MenuId(kAddButtonId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 264, 108, 78, 30, hwnd, MenuId(kCancelButtonId), state->instance, nullptr);
        for (HWND control : {titleLabel, state->titleEdit, urlLabel, state->urlEdit, addButton, cancelButton}) {
            ApplyFont(control, state->font);
        }
        SetFocus(state->titleEdit);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kAddButtonId:
        case IDOK:
            state->result.title = Sanitized(ReadWindowText(state->titleEdit));
            state->result.url = Sanitized(ReadWindowText(state->urlEdit));
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

}

void RegisterAddDialogClass(HINSTANCE instance) {
    WNDCLASSEXW dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.lpfnWndProc = AddDialogProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dialogClass.lpszClassName = kDialogClassName;
    RegisterClassExW(&dialogClass);
}

AddResult ShowAddDialog(HINSTANCE instance, HWND parent) {
    AddDialogState state;
    state.instance = instance;

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 370;
    int height = 178;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDialogClassName, L"링크 추가", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, instance, &state);
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
