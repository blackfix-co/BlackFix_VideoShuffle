#include "add_dialog.hpp"

#include "utils.hpp"

#include <windowsx.h>

#include <utility>

namespace {

constexpr wchar_t kDialogClassName[] = L"BlackFixVideoShuffleAddDialog";
constexpr int kTitleEditId = 101;
constexpr int kUrlEditId = 102;
constexpr int kTagComboId = 103;
constexpr int kAddButtonId = 104;
constexpr int kCancelButtonId = 105;
constexpr int kPreviewCheckId = 106;
constexpr wchar_t kTagAddText[] = L"태그 추가";
constexpr wchar_t kTagDialogClassName[] = L"BlackFixVideoShuffleTagDialog";
constexpr int kNewTagEditId = 501;
constexpr int kNewTagListId = 502;
constexpr int kNewTagOkId = 503;
constexpr int kNewTagCancelId = 504;
constexpr COLORREF kDarkBackground = RGB(24, 26, 32);
constexpr COLORREF kDarkPanel = RGB(34, 37, 45);
constexpr COLORREF kDarkText = RGB(255, 255, 255);

struct VideoChoice {
    size_t index{};
    std::wstring label;
};

struct AddDialogState {
    HINSTANCE instance{};
    HWND titleEdit{};
    HWND urlEdit{};
    HWND tagCombo{};
    HWND previewCheck{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    HBRUSH panelBrush{};
    std::vector<std::wstring> tags;
    std::vector<VideoChoice> videoChoices;
    VideoDialogInput input;
    AddResult result;
    std::wstring previousTag{L"전체"};
    std::wstring newTag;
    std::vector<size_t> newTagAssignments;
};

struct NewTagResult {
    bool accepted{};
    std::wstring tag;
    std::vector<size_t> indices;
};

struct NewTagDialogState {
    HINSTANCE instance{};
    HWND edit{};
    HWND list{};
    HFONT font{};
    HBRUSH backgroundBrush{};
    HBRUSH panelBrush{};
    std::vector<VideoChoice> choices;
    NewTagResult result;
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

HBRUSH ApplyControlColors(WPARAM wParam, HBRUSH brush, COLORREF background) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, kDarkText);
    SetBkColor(dc, background);
    SetBkMode(dc, TRANSPARENT);
    return brush;
}

HMENU MenuId(int value) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(value));
}

std::wstring VideoLabel(const LinkEntry& entry) {
    std::wstring title = entry.title.empty() ? L"제목 없음" : entry.title;
    std::wstring tag = entry.tag.empty() ? L"전체" : entry.tag;
    return title + L"  [" + tag + L"]";
}

std::vector<size_t> SelectedVideoIndices(HWND list, const std::vector<VideoChoice>& choices) {
    int count = static_cast<int>(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
    if (count <= 0) {
        return {};
    }

    std::vector<int> positions(static_cast<size_t>(count));
    SendMessageW(list, LB_GETSELITEMS, static_cast<WPARAM>(positions.size()), reinterpret_cast<LPARAM>(positions.data()));

    std::vector<size_t> indices;
    indices.reserve(positions.size());
    for (int position : positions) {
        if (position >= 0 && position < static_cast<int>(choices.size())) {
            indices.push_back(choices[static_cast<size_t>(position)].index);
        }
    }
    return indices;
}

LRESULT CALLBACK NewTagDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    NewTagDialogState* state = reinterpret_cast<NewTagDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<NewTagDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        state->backgroundBrush = CreateSolidBrush(kDarkBackground);
        state->panelBrush = CreateSolidBrush(kDarkPanel);
        HWND label = CreateWindowW(L"STATIC", L"새 태그", WS_CHILD | WS_VISIBLE, 18, 18, 64, 24, hwnd, nullptr, state->instance, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 84, 14, 278, 28, hwnd, MenuId(kNewTagEditId), state->instance, nullptr);
        HWND listLabel = CreateWindowW(L"STATIC", L"넣을 영상", WS_CHILD | WS_VISIBLE, 18, 56, 86, 24, hwnd, nullptr, state->instance, nullptr);
        state->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_MULTIPLESEL, 18, 82, 344, 156, hwnd, MenuId(kNewTagListId), state->instance, nullptr);
        HWND okButton = CreateWindowW(L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 196, 252, 78, 30, hwnd, MenuId(kNewTagOkId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 284, 252, 78, 30, hwnd, MenuId(kNewTagCancelId), state->instance, nullptr);
        for (HWND control : {label, state->edit, listLabel, state->list, okButton, cancelButton}) {
            ApplyFont(control, state->font);
        }
        for (const auto& choice : state->choices) {
            SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.c_str()));
        }
        SetFocus(state->edit);
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
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kNewTagOkId:
        case IDOK:
            state->result.tag = Sanitized(ReadWindowText(state->edit));
            if (state->result.tag.empty() || state->result.tag == L"전체" || state->result.tag == kTagAddText) {
                MessageBoxW(hwnd, L"태그 이름을 입력해줘.", L"BlackFix VideoShuffle", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            state->result.indices = SelectedVideoIndices(state->list, state->choices);
            state->result.accepted = true;
            DestroyWindow(hwnd);
            return 0;
        case kNewTagCancelId:
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

void RegisterNewTagDialogClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.lpfnWndProc = NewTagDialogProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dialogClass.lpszClassName = kTagDialogClassName;
    RegisterClassExW(&dialogClass);
    registered = true;
}

NewTagResult ShowNewTagDialog(HINSTANCE instance, HWND parent, const std::vector<VideoChoice>& choices) {
    RegisterNewTagDialogClass(instance);

    NewTagDialogState state;
    state.instance = instance;
    state.choices = choices;

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 392;
    int height = 326;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kTagDialogClassName, L"태그 추가", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, parent, nullptr, instance, &state);
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
        state->backgroundBrush = CreateSolidBrush(kDarkBackground);
        state->panelBrush = CreateSolidBrush(kDarkPanel);
        HWND titleLabel = CreateWindowW(L"STATIC", L"제목", WS_CHILD | WS_VISIBLE, 22, 22, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->titleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->input.title.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 18, 260, 28, hwnd, MenuId(kTitleEditId), state->instance, nullptr);
        HWND urlLabel = CreateWindowW(L"STATIC", L"링크", WS_CHILD | WS_VISIBLE, 22, 64, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->urlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->input.url.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 82, 60, 260, 28, hwnd, MenuId(kUrlEditId), state->instance, nullptr);
        HWND tagLabel = CreateWindowW(L"STATIC", L"태그", WS_CHILD | WS_VISIBLE, 22, 106, 54, 24, hwnd, nullptr, state->instance, nullptr);
        state->tagCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 82, 102, 260, 120, hwnd, MenuId(kTagComboId), state->instance, nullptr);
        state->previewCheck = CreateWindowW(L"BUTTON", L"1분 미리보기 반복", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 82, 136, 170, 24, hwnd, MenuId(kPreviewCheckId), state->instance, nullptr);
        HWND addButton = CreateWindowW(L"BUTTON", state->input.editing ? L"수정" : L"추가", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 176, 166, 78, 30, hwnd, MenuId(kAddButtonId), state->instance, nullptr);
        HWND cancelButton = CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 264, 166, 78, 30, hwnd, MenuId(kCancelButtonId), state->instance, nullptr);
        SendMessageW(state->tagCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kTagAddText));
        for (const auto& tag : state->tags) {
            SendMessageW(state->tagCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tag.c_str()));
        }
        SetWindowTextW(state->tagCombo, state->input.tag.empty() ? L"전체" : state->input.tag.c_str());
        state->previousTag = state->input.tag.empty() ? L"전체" : state->input.tag;
        SendMessageW(state->previewCheck, BM_SETCHECK, state->input.preview ? BST_CHECKED : BST_UNCHECKED, 0);
        for (HWND control : {titleLabel, state->titleEdit, urlLabel, state->urlEdit, tagLabel, state->tagCombo, state->previewCheck, addButton, cancelButton}) {
            ApplyFont(control, state->font);
        }
        SetFocus(state->titleEdit);
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
    case WM_COMMAND:
        if (LOWORD(wParam) == kTagComboId && HIWORD(wParam) == CBN_SELCHANGE) {
            int selection = static_cast<int>(SendMessageW(state->tagCombo, CB_GETCURSEL, 0, 0));
            if (selection == 0) {
                NewTagResult tagResult = ShowNewTagDialog(state->instance, hwnd, state->videoChoices);
                if (tagResult.accepted) {
                    state->newTag = tagResult.tag;
                    state->newTagAssignments = std::move(tagResult.indices);
                    state->result.tag = state->newTag;
                    state->result.createdTag = state->newTag;
                    state->result.tagCreated = true;
                    state->result.tagAssignments = state->newTagAssignments;
                    if (SendMessageW(state->tagCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(state->newTag.c_str())) == CB_ERR) {
                        SendMessageW(state->tagCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(state->newTag.c_str()));
                    }
                    SetWindowTextW(state->tagCombo, state->newTag.c_str());
                    state->previousTag = state->newTag;
                } else {
                    SetWindowTextW(state->tagCombo, state->previousTag.c_str());
                }
                return 0;
            }
            state->previousTag = Sanitized(ReadComboText(state->tagCombo));
            return 0;
        }
        switch (LOWORD(wParam)) {
        case kAddButtonId:
        case IDOK:
            state->result.title = Sanitized(ReadWindowText(state->titleEdit));
            state->result.url = Sanitized(ReadWindowText(state->urlEdit));
            state->result.tag = Sanitized(ReadComboText(state->tagCombo));
            state->result.preview = SendMessageW(state->previewCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (state->result.tag == kTagAddText) {
                state->result.tag = L"전체";
            }
            if (state->result.tag == state->newTag) {
                state->result.tagCreated = true;
                state->result.createdTag = state->newTag;
                state->result.tagAssignments = state->newTagAssignments;
            }
            if (state->result.url.empty() && !state->result.tagCreated) {
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

AddResult ShowVideoDialog(HINSTANCE instance, HWND parent, const std::vector<std::wstring>& tags, const std::vector<LinkEntry>& entries, const VideoDialogInput& input) {
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
    for (size_t i = 0; i < entries.size(); ++i) {
        state.videoChoices.push_back({i, VideoLabel(entries[i])});
    }

    RECT parentRect{};
    GetWindowRect(parent, &parentRect);
    int width = 370;
    int height = 236;
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
