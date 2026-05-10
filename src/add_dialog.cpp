#include "add_dialog.hpp"

#include "utils.hpp"

#include <windowsx.h>

namespace {

constexpr wchar_t kDialogClassName[] = L"BlackFixVideoShuffleAddDialog";
constexpr int kTitleEditId = 101;
constexpr int kUrlEditId = 102;
constexpr int kTagComboId = 103;
constexpr int kAddButtonId = 104;
constexpr int kCancelButtonId = 105;

struct AddDialogState {
    HINSTANCE instance{};
    HWND titleEdit{};
    HWND urlEdit{};
    HWND tagCombo{};
    HFONT font{};
    std::vector<std::wstring> tags;
    VideoDialogInput input;
    AddResult result;
};

std::wstring ReadWindowText(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(length);
    return text;
}

std::wstring ReadComboText(HWND hwnd) {
    return ReadWindowText(hwnd);
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
        state->titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->input.title.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 18, 260, 28, hwnd, MenuId(kTitleEditId), state->instance, nullptr);
        HWND urlLabel = CreateWindowW(L"STATIC", L"링크", WS_CHILD | WS_VISIBLE, 22, 64, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->urlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->input.url.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 60, 260, 28, hwnd, MenuId(kUrlEditId), state->instance, nullptr);
        HWND tagLabel = CreateWindowW(L"STATIC", L"태그", WS_CHILD | WS_VISIBLE, 22, 106, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->tagCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 82, 102, 260, 120, hwnd, MenuId(kTagComboId), state->instance, nullptr);
        HWND addButton = CreateWindowW(L"BUTTON", state->input.editing ? L"수정" : L"추가", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 176, 150, 78, 30, hwnd, MenuId(kAddButtonId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 264, 150, 78, 30, hwnd, MenuId(kCancelButtonId), state->instance, nullptr);
        for (const auto& tag : state->tags) {
            SendMessageW(state->tagCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tag.c_str()));
        }
        SetWindowTextW(state->tagCombo, state->input.tag.empty() ? L"전체" : state->input.tag.c_str());
        for (HWND control : {titleLabel, state->titleEdit, urlLabel, state->urlEdit, tagLabel, state->tagCombo, addButton, cancelButton}) {
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
            state->result.tag = Sanitized(ReadComboText(state->tagCombo));
            if (state->result.url.empty()) {
                MessageBoxW(hwnd, L"링크를 입력해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (state->result.tag.empty()) {
                state->result.tag = L"전체";
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

AddResult ShowVideoDialog(HINSTANCE instance, HWND parent, const std::vector<std::wstring>& tags, const VideoDialogInput& input) {
    AddDialogState state;
    state.instance = instance;
    state.tags = tags;
    state.input = input;
    if (state.tags.empty()) {
        state.tags.push_back(L"전체");
    }
    if (state.input.tag.empty()) {
        state.input.tag = L"전체";
    }

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 370;
    int height = 220;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDialogClassName, input.editing ? L"영상 수정" : L"링크 추가", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, instance, &state);
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
