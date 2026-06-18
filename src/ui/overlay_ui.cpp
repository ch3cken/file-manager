#include "config/app_config.h"
#include "core/database.h"
#include "search/quick_search.h"
#include "search/smart_search.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

constexpr int kHotkeyId = 1200;
constexpr int kEditId = 100;
constexpr int kQuickListId = 101;
constexpr int kSmartListId = 102;

struct AppState {
    config::AppConfig config;
    std::unique_ptr<DatabaseManager> database;
    std::unique_ptr<QuickSearch> quickSearch;
    std::unique_ptr<SmartSearch> smartSearch;
    HWND edit = nullptr;
    HWND quickList = nullptr;
    HWND smartList = nullptr;
    HWND quickLabel = nullptr;
    HWND smartLabel = nullptr;
    HFONT font = nullptr;
    std::vector<FileRecord> quickRecords;
    std::vector<SmartSearchResult> smartRecords;
};

std::unique_ptr<AppState> gState;

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        wide.data(), size);
    return wide;
}

std::wstring windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    return text;
}

std::wstring quickDisplayText(const FileRecord& record) {
    std::ostringstream stream;
    stream << record.file_name;
    if (!record.last_modified.empty()) {
        stream << "  " << record.last_modified;
    }
    if (!record.file_path.empty()) {
        stream << "  " << record.file_path;
    }
    return utf8ToWide(stream.str());
}

std::wstring smartDisplayText(const SmartSearchResult& result) {
    std::ostringstream stream;
    stream << result.record.file_name << "  score=" << result.score;
    if (!result.record.file_path.empty()) {
        stream << "  " << result.record.file_path;
    }
    return utf8ToWide(stream.str());
}

void setFont(HWND hwnd) {
    if (gState && gState->font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(gState->font), TRUE);
    }
}

HWND createChild(HWND parent,
                 const wchar_t* className,
                 const wchar_t* text,
                 DWORD style,
                 int id) {
    HWND hwnd = CreateWindowExW(0, className, text, style | WS_CHILD | WS_VISIBLE,
                                0, 0, 100, 30, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    setFont(hwnd);
    return hwnd;
}

void resizeControls(HWND hwnd) {
    if (!gState) {
        return;
    }
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int padding = 18;
    const int editHeight = 36;
    const int labelHeight = 22;
    const int gap = 14;
    const int listTop = padding + editHeight + gap + labelHeight;
    const int listHeight = std::max(80, height - listTop - padding);
    const int columnWidth = (width - padding * 2 - gap) / 2;

    MoveWindow(gState->edit, padding, padding, width - padding * 2, editHeight, TRUE);
    MoveWindow(gState->quickLabel, padding, padding + editHeight + gap, columnWidth, labelHeight, TRUE);
    MoveWindow(gState->smartLabel, padding + columnWidth + gap, padding + editHeight + gap, columnWidth, labelHeight, TRUE);
    MoveWindow(gState->quickList, padding, listTop, columnWidth, listHeight, TRUE);
    MoveWindow(gState->smartList, padding + columnWidth + gap, listTop, columnWidth, listHeight, TRUE);
}

void populateList(HWND list, const wchar_t* emptyText) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(emptyText));
}

void updateResults() {
    if (!gState || !gState->quickSearch || !gState->smartSearch) {
        return;
    }

    const std::string query = wideToUtf8(windowText(gState->edit));
    gState->quickRecords.clear();
    gState->smartRecords.clear();
    SendMessageW(gState->quickList, LB_RESETCONTENT, 0, 0);
    SendMessageW(gState->smartList, LB_RESETCONTENT, 0, 0);

    if (query.empty()) {
        populateList(gState->quickList, L"Type to search");
        populateList(gState->smartList, L"Type to search");
        return;
    }

    const search::ParsedSearchQuery parsed = search::parseSearchQuery(query);
    gState->quickRecords = gState->quickSearch->search(parsed, 40);
    gState->smartRecords = gState->smartSearch->searchLexically(parsed, 40);

    if (gState->quickRecords.empty()) {
        populateList(gState->quickList, L"No results found");
    } else {
        for (const auto& record : gState->quickRecords) {
            const std::wstring text = quickDisplayText(record);
            SendMessageW(gState->quickList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
    }

    if (gState->smartRecords.empty()) {
        populateList(gState->smartList, L"No results found");
    } else {
        for (const auto& result : gState->smartRecords) {
            const std::wstring text = smartDisplayText(result);
            SendMessageW(gState->smartList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
    }
}

void openPath(const std::string& path) {
    if (path.empty()) {
        return;
    }
    const std::wstring widePath = utf8ToWide(path);
    ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openSelected(HWND list, const std::vector<FileRecord>& records) {
    const LRESULT selection = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selection < 0 || static_cast<std::size_t>(selection) >= records.size()) {
        return;
    }
    openPath(records[static_cast<std::size_t>(selection)].file_path);
}

void openSelectedSmart(HWND list, const std::vector<SmartSearchResult>& records) {
    const LRESULT selection = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (selection < 0 || static_cast<std::size_t>(selection) >= records.size()) {
        return;
    }
    openPath(records[static_cast<std::size_t>(selection)].record.file_path);
}

void showOverlay(HWND hwnd) {
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int width = 920;
    const int height = 520;
    const int left = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int top = workArea.top + ((workArea.bottom - workArea.top) - height) / 3;
    SetWindowPos(hwnd, HWND_TOPMOST, left, top, width, height, SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(gState->edit);
    SendMessageW(gState->edit, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        gState->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        gState->edit = createChild(hwnd, L"EDIT", L"",
                                   WS_BORDER | ES_AUTOHSCROLL, kEditId);
        gState->quickLabel = createChild(hwnd, L"STATIC", L"Quick Search",
                                         SS_LEFT, 0);
        gState->smartLabel = createChild(hwnd, L"STATIC", L"Smart Search",
                                         SS_LEFT, 0);
        gState->quickList = createChild(hwnd, L"LISTBOX", L"",
                                        WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                        kQuickListId);
        gState->smartList = createChild(hwnd, L"LISTBOX", L"",
                                        WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                        kSmartListId);
        populateList(gState->quickList, L"Type to search");
        populateList(gState->smartList, L"Type to search");
        RegisterHotKey(hwnd, kHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_SPACE);
        return 0;

    case WM_SIZE:
        resizeControls(hwnd);
        return 0;

    case WM_HOTKEY:
        if (wParam == kHotkeyId) {
            showOverlay(hwnd);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kEditId && HIWORD(wParam) == EN_CHANGE) {
            updateResults();
            return 0;
        }
        if (LOWORD(wParam) == kQuickListId && HIWORD(wParam) == LBN_DBLCLK) {
            openSelected(gState->quickList, gState->quickRecords);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (LOWORD(wParam) == kSmartListId && HIWORD(wParam) == LBN_DBLCLK) {
            openSelectedSmart(gState->smartList, gState->smartRecords);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, kHotkeyId);
        if (gState && gState->font) {
            DeleteObject(gState->font);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool initializeState() {
    try {
        gState = std::make_unique<AppState>();
        gState->config = config::loadOrCreateConfig();
        gState->database = std::make_unique<DatabaseManager>(gState->config.databasePath);
        gState->quickSearch = std::make_unique<QuickSearch>(*gState->database);
        gState->smartSearch = std::make_unique<SmartSearch>(*gState->database);
        return true;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "FileManager Overlay", MB_ICONERROR | MB_OK);
        return false;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!initializeState()) {
        return 1;
    }

    const wchar_t* className = L"FileManagerOverlayWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                className,
                                L"FileManager Search",
                                WS_POPUP | WS_THICKFRAME | WS_CAPTION,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                920,
                                520,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
    if (!hwnd) {
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

#else
int main() {
    return 0;
}
#endif
