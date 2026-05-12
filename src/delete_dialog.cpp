#include "delete_dialog.hpp"

#include <windowsx.h>

#include <algorithm>

namespace {

constexpr wchar_t kDeleteDialogClassName[] = L"BlackFixVideoShuffleDeleteDialog";
constexpr int kListId = 201;
constexpr int kOkId = 202;
constexpr int kCancelId = 203;
constexpr int kSelectTagId = 204;
constexpr int kSelectAllId = 205;
constexpr int kClearId = 206;

struct DeleteChoice {
    size_t index{};
    std::wstring tag;
    std::wstring label;
};

struct DeleteDialogState {
    HINSTANCE instance{};
    HWND list{};
    HFONT font{};
    std::wstring activeTag;
    std::vector<DeleteChoice> choices;
    std::vector<size_t> result;
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
    return (entry.title.empty() ? L"제목 없음" : entry.title) + L"  [" + NormalizedTagName(entry) + L"]";
}

void SelectMatching(DeleteDialogState* state, const std::wstring& tag) {
    SendMessageW(state->list, LB_SETSEL, FALSE, -1);
    for (size_t i = 0; i < state->choices.size(); ++i) {
        if (tag == L"전체" || state->choices[i].tag == tag) {
            SendMessageW(state->list, LB_SETSEL, TRUE, static_cast<LPARAM>(i));
        }
    }
}

std::vector<size_t> SelectedIndices(DeleteDialogState* state) {
    int count = static_cast<int>(SendMessageW(state->list, LB_GETSELCOUNT, 0, 0));
    if (count <= 0) {
        return {};
    }

    std::vector<int> positions(static_cast<size_t>(count));
    SendMessageW(state->list, LB_GETSELITEMS, static_cast<WPARAM>(positions.size()), reinterpret_cast<LPARAM>(positions.data()));

    std::vector<size_t> selected;
    selected.reserve(positions.size());
    for (int position : positions) {
        if (position >= 0 && position < static_cast<int>(state->choices.size())) {
            selected.push_back(state->choices[static_cast<size_t>(position)].index);
        }
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
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
        HWND tagButton = CreateWindowW(L"BUTTON", L"현재 태그 전체 선택", WS_CHILD | WS_VISIBLE, 16, 14, 138, 28, hwnd, MenuId(kSelectTagId), state->instance, nullptr);
        HWND allButton = CreateWindowW(L"BUTTON", L"전체 선택", WS_CHILD | WS_VISIBLE, 162, 14, 88, 28, hwnd, MenuId(kSelectAllId), state->instance, nullptr);
        HWND clearButton = CreateWindowW(L"BUTTON", L"선택 해제", WS_CHILD | WS_VISIBLE, 258, 14, 88, 28, hwnd, MenuId(kClearId), state->instance, nullptr);
        state->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL, 16, 52, 416, 230, hwnd, MenuId(kListId), state->instance, nullptr);
        HWND okButton = CreateWindowW(L"BUTTON", L"지우기", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 266, 294, 78, 30, hwnd, MenuId(kOkId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 354, 294, 78, 30, hwnd, MenuId(kCancelId), state->instance, nullptr);
        for (HWND control : {tagButton, allButton, clearButton, state->list, okButton, cancelButton}) {
            ApplyFont(control, state->font);
        }
        for (const auto& choice : state->choices) {
            SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.c_str()));
        }
        if (!state->choices.empty()) {
            SendMessageW(state->list, LB_SETSEL, TRUE, 0);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kSelectTagId:
            SelectMatching(state, state->activeTag);
            return 0;
        case kSelectAllId:
            SelectMatching(state, L"전체");
            return 0;
        case kClearId:
            SendMessageW(state->list, LB_SETSEL, FALSE, -1);
            return 0;
        case kOkId: {
            std::vector<size_t> selected = SelectedIndices(state);
            if (selected.empty()) {
                MessageBoxW(hwnd, L"지울 영상을 선택해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            std::wstring messageText = std::to_wstring(selected.size()) + L"개 영상을 정말 지우시겠습니까?";
            if (MessageBoxW(hwnd, messageText.c_str(), L"BlackFix VideoShuffle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                state->result = std::move(selected);
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

std::vector<size_t> ShowDeleteDialog(HINSTANCE instance, HWND parent, const std::vector<LinkEntry>& entries, const std::wstring& activeTag) {
    static bool registered = false;
    if (!registered) {
        RegisterDeleteDialogClass(instance);
        registered = true;
    }

    DeleteDialogState state;
    state.instance = instance;
    state.activeTag = activeTag.empty() ? L"전체" : activeTag;

    for (size_t i = 0; i < entries.size(); ++i) {
        std::wstring tag = NormalizedTagName(entries[i]);
        if (state.activeTag == L"전체" || tag == state.activeTag) {
            state.choices.push_back({i, tag, L"[현재 태그] " + EntryLabel(entries[i])});
        }
    }

    if (state.activeTag != L"전체") {
        for (size_t i = 0; i < entries.size(); ++i) {
            std::wstring tag = NormalizedTagName(entries[i]);
            if (tag == L"전체") {
                state.choices.push_back({i, tag, L"[전체 태그] " + EntryLabel(entries[i])});
            }
        }
    }

    std::sort(state.choices.begin(), state.choices.end(), [](const DeleteChoice& left, const DeleteChoice& right) {
        if (left.tag != right.tag) {
            return left.tag < right.tag;
        }
        return left.index < right.index;
    });
    state.choices.erase(std::unique(state.choices.begin(), state.choices.end(), [](const DeleteChoice& left, const DeleteChoice& right) {
        return left.index == right.index;
    }), state.choices.end());

    if (state.choices.empty()) {
        MessageBoxW(parent, L"삭제할 영상이 없습니다.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
        return {};
    }

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 464;
    int height = 374;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kDeleteDialogClassName, L"영상 삭제", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, instance, &state);
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
