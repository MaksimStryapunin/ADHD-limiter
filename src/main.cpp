#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "resource.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <mmsystem.h>

namespace {
constexpr UINT_PTR kTimerId = 1;
constexpr int kMinMinutes = 1;
constexpr int kMaxMinutes = 720;
constexpr int kDefaultMinutes = 20;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayOpenCommand = 40001;
constexpr UINT kTrayExitCommand = 40002;

enum class TimerState { Idle, Running, Paused, AwaitingConfirm };
enum class Phase { Work, Rest };

struct AppState {
    HWND restEdit = nullptr;
    HWND workEdit = nullptr;
    HWND restLabel = nullptr;
    HWND workLabel = nullptr;
    HWND nameEdit = nullptr;
    HWND actionButton = nullptr;
    HWND resetButton = nullptr;
    WNDPROC nameEditOriginalProc = nullptr;

    HFONT activeNumberFont = nullptr;
    HFONT inactiveNumberFont = nullptr;
    HFONT uiFont = nullptr;

    HBRUSH backgroundBrush = nullptr;
    HINSTANCE instance = nullptr;
    HICON bigIcon = nullptr;
    HICON smallIcon = nullptr;
    NOTIFYICONDATAW trayIcon{};
    bool trayAdded = false;
    TimerState state = TimerState::Idle;
    Phase phase = Phase::Work;
    Phase selectedPhase = Phase::Work;

    int workMinutes = 20;
    int restMinutes = 20;
    int remainingSeconds = 0;
    std::wstring restName = L"Отдых";
    std::wstring workName = L"Работа";
    Phase editingPhase = Phase::Work;
};

AppState& GetState(HWND hwnd) {
    return *reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void ResizeControls(HWND hwnd, int width, int height);
LRESULT CALLBACK NameEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

void RemoveTrayIcon(HWND hwnd) {
    auto& app = GetState(hwnd);
    if (app.trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &app.trayIcon);
        app.trayAdded = false;
    }
}

void RestoreWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

void AddTrayIcon(HWND hwnd) {
    auto& app = GetState(hwnd);
    app.trayIcon = {};
    app.trayIcon.cbSize = sizeof(app.trayIcon);
    app.trayIcon.hWnd = hwnd;
    app.trayIcon.uID = kTrayIconId;
    app.trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    app.trayIcon.uCallbackMessage = kTrayMessage;
    app.trayIcon.hIcon = app.smallIcon;
    lstrcpynW(app.trayIcon.szTip, L"ADHD-limiter", ARRAYSIZE(app.trayIcon.szTip));
    app.trayAdded = Shell_NotifyIconW(NIM_ADD, &app.trayIcon) == TRUE;
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayOpenCommand, L"Открыть");
    AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Выход");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, hwnd,
                   nullptr);
    DestroyMenu(menu);
}

int MeasureTextWidth(HWND hwnd, const std::wstring& text) {
    auto& app = GetState(hwnd);
    HDC dc = GetDC(hwnd);
    HGDIOBJ oldFont = SelectObject(dc, app.uiFont);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);
    return size.cx + 16;
}

int ReadMinutes(HWND edit, int fallback) {
    wchar_t buffer[64]{};
    GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));

    try {
        const int value = std::stoi(buffer);
        return std::clamp(value, kMinMinutes, kMaxMinutes);
    } catch (...) {
        return fallback;
    }
}

void SetMinutes(HWND edit, int value) {
    const int normalized = std::clamp(value, kMinMinutes, kMaxMinutes);
    SetWindowTextW(edit, std::to_wstring(normalized).c_str());
}

void UpdateButton(HWND hwnd) {
    auto& app = GetState(hwnd);
    InvalidateRect(app.actionButton, nullptr, TRUE);
    InvalidateRect(app.resetButton, nullptr, TRUE);
}

void UpdateWindowTitle(HWND hwnd) {
    auto& app = GetState(hwnd);

    if (app.state == TimerState::Running) {
        const int minutes = app.remainingSeconds / 60;
        const int seconds = app.remainingSeconds % 60;
        const std::wstring& phase = app.phase == Phase::Work ? app.workName : app.restName;
        std::wstring title = L"ADHD-limiter — " + phase + L" " +
                             (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L":" +
                             (seconds < 10 ? L"0" : L"") + std::to_wstring(seconds);
        SetWindowTextW(hwnd, title.c_str());
        return;
    }
    if (app.state == TimerState::AwaitingConfirm) {
        SetWindowTextW(hwnd, L"ADHD-limiter — Таймер завершен, подтвердите переход");
        return;
    }
    if (app.state == TimerState::Paused) {
        SetWindowTextW(hwnd, L"ADHD-limiter — Пауза");
        return;
    }
    SetWindowTextW(hwnd, L"ADHD-limiter");
}

void UpdateNameLabels(HWND hwnd) {
    auto& app = GetState(hwnd);
    SetWindowTextW(app.restLabel, app.restName.c_str());
    SetWindowTextW(app.workLabel, app.workName.c_str());
}

void CommitNameEdit(HWND hwnd) {
    auto& app = GetState(hwnd);
    if (app.nameEdit == nullptr) {
        return;
    }

    wchar_t buffer[128]{};
    GetWindowTextW(app.nameEdit, buffer, static_cast<int>(std::size(buffer)));
    std::wstring name = buffer;
    if (!name.empty()) {
        if (app.editingPhase == Phase::Work) {
            app.workName = name;
        } else {
            app.restName = name;
        }
    }
    HWND edit = app.nameEdit;
    app.nameEdit = nullptr;
    DestroyWindow(edit);
    UpdateNameLabels(hwnd);
    UpdateWindowTitle(hwnd);
}

void CancelNameEdit(HWND hwnd) {
    auto& app = GetState(hwnd);
    if (app.nameEdit == nullptr) {
        return;
    }
    HWND edit = app.nameEdit;
    app.nameEdit = nullptr;
    DestroyWindow(edit);
}

void BeginNameEdit(HWND hwnd, Phase phase) {
    auto& app = GetState(hwnd);
    if (app.nameEdit != nullptr || app.state == TimerState::Running) {
        return;
    }

    app.editingPhase = phase;
    HWND label = phase == Phase::Work ? app.workLabel : app.restLabel;
    RECT rect{};
    GetWindowRect(label, &rect);
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rect), 2);
    const std::wstring& name = phase == Phase::Work ? app.workName : app.restName;
    const int editWidth = MeasureTextWidth(hwnd, name);
    const int labelCenter = (rect.left + rect.right) / 2;
    const int left = (std::max)(0, labelCenter - editWidth / 2);
    app.nameEdit = CreateWindowExW(
        0, L"EDIT", name.c_str(),
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL | ES_MULTILINE,
        left, rect.top, editWidth, rect.bottom - rect.top + 8,
        hwnd, reinterpret_cast<HMENU>(1008), app.instance, nullptr);
    app.nameEditOriginalProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(app.nameEdit, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(NameEditProc)));
    SendMessageW(app.nameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);
    SendMessageW(app.nameEdit, EM_SETSEL, 0, -1);
    SetFocus(app.nameEdit);
}

LRESULT CALLBACK NameEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN) {
        HWND parent = GetParent(hwnd);
        if (wParam == VK_RETURN) {
            CommitNameEdit(parent);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            CancelNameEdit(parent);
            return 0;
        }
    }
    HWND parent = GetParent(hwnd);
    if (parent != nullptr) {
        auto& app = GetState(parent);
        if (app.nameEditOriginalProc != nullptr) {
            return CallWindowProcW(app.nameEditOriginalProc, hwnd, message, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void SetActivePhase(HWND hwnd, Phase phase) {
    auto& app = GetState(hwnd);
    if (app.state == TimerState::Running) {
        return;
    }
    app.selectedPhase = phase;
    RECT client{};
    GetClientRect(hwnd, &client);
    ResizeControls(hwnd, client.right, client.bottom);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void ApplyEditEnabled(HWND hwnd, BOOL enabled) {
    auto& app = GetState(hwnd);
    EnableWindow(app.workEdit, enabled);
    EnableWindow(app.restEdit, enabled);
}

void ResetTimer(HWND hwnd) {
    auto& app = GetState(hwnd);
    KillTimer(hwnd, kTimerId);
    app.state = TimerState::Idle;
    app.phase = Phase::Work;
    app.selectedPhase = Phase::Work;
    app.remainingSeconds = 0;
    app.workMinutes = kDefaultMinutes;
    app.restMinutes = kDefaultMinutes;
    SetMinutes(app.workEdit, app.workMinutes);
    SetMinutes(app.restEdit, app.restMinutes);
    ApplyEditEnabled(hwnd, TRUE);
    UpdateButton(hwnd);
    UpdateWindowTitle(hwnd);
}

void TogglePause(HWND hwnd) {
    auto& app = GetState(hwnd);
    if (app.state == TimerState::Running) {
        KillTimer(hwnd, kTimerId);
        app.state = TimerState::Paused;
    } else if (app.state == TimerState::Paused) {
        app.state = TimerState::Running;
        SetTimer(hwnd, kTimerId, 1000, nullptr);
    } else {
        return;
    }
    UpdateButton(hwnd);
    UpdateWindowTitle(hwnd);
}

void StartCurrentPhase(HWND hwnd) {
    auto& app = GetState(hwnd);
    app.workMinutes = ReadMinutes(app.workEdit, app.workMinutes);
    app.restMinutes = ReadMinutes(app.restEdit, app.restMinutes);

    SetMinutes(app.workEdit, app.workMinutes);
    SetMinutes(app.restEdit, app.restMinutes);

    app.phase = app.selectedPhase;
    const int phaseMinutes = app.phase == Phase::Work ? app.workMinutes : app.restMinutes;
    app.remainingSeconds = phaseMinutes * 60;
    SetMinutes(app.phase == Phase::Work ? app.workEdit : app.restEdit, phaseMinutes);

    app.state = TimerState::Running;
    ApplyEditEnabled(hwnd, FALSE);
    SetTimer(hwnd, kTimerId, 1000, nullptr);
    UpdateButton(hwnd);
    UpdateWindowTitle(hwnd);
}

void ResizeControls(HWND hwnd, int width, int height) {
    auto& app = GetState(hwnd);

    const int margin = (std::max)(12, width / 30);
    const int top = (std::max)(16, height / 5);
    const int editHeight = (std::max)(40, height / 4);
    const int gap = (std::max)(30, width / 10);
    const int editWidth = (std::max)(90, (width - gap - margin * 2) / 2);

    const int leftX = margin;
    const int rightX = width - margin - editWidth;

    MoveWindow(app.restEdit, leftX, top, editWidth, editHeight, TRUE);
    MoveWindow(app.workEdit, rightX, top, editWidth, editHeight, TRUE);
    MoveWindow(app.restLabel, leftX, 8, editWidth, 32, TRUE);
    MoveWindow(app.workLabel, rightX, 8, editWidth, 32, TRUE);

    const int buttonWidth = (std::max)(52, (std::min)(68, height / 7));
    const int buttonHeight = buttonWidth;
    const int buttonX = (width - buttonWidth) / 2;
    const int buttonY = (std::max)(top + editHeight + 24, height - buttonHeight - margin - 4);
    MoveWindow(app.actionButton, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
    const int utilitySize = (std::max)(42, (std::min)(72, height / 9));
    const int utilityY = height - utilitySize - margin;
    MoveWindow(app.resetButton, width - margin - utilitySize, utilityY,
               utilitySize, utilitySize, TRUE);

    const int activeNumberFontHeight = -(std::max)(48, (std::min)(120, editHeight * 3 / 4));
    const int inactiveNumberFontHeight = -(std::max)(30, (std::min)(60, editHeight / 2));
    const int uiFontHeight = -(std::max)(12, (std::min)(22, height / 18));

    if (app.activeNumberFont != nullptr) {
        DeleteObject(app.activeNumberFont);
    }
    if (app.inactiveNumberFont != nullptr) {
        DeleteObject(app.inactiveNumberFont);
    }
    if (app.uiFont != nullptr) {
        DeleteObject(app.uiFont);
    }

    app.activeNumberFont = CreateFontW(activeNumberFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE,
                                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    app.inactiveNumberFont = CreateFontW(inactiveNumberFontHeight, 0, 0, 0, FW_BOLD, FALSE,
                                         FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    app.uiFont = CreateFontW(uiFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    SendMessageW(app.workEdit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(app.selectedPhase == Phase::Work
                                              ? app.activeNumberFont
                                              : app.inactiveNumberFont),
                 TRUE);
    SendMessageW(app.restEdit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(app.selectedPhase == Phase::Rest
                                              ? app.activeNumberFont
                                              : app.inactiveNumberFont),
                 TRUE);
    SendMessageW(app.restLabel, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);
    SendMessageW(app.workLabel, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);

    InvalidateRect(hwnd, nullptr, TRUE);
}

void DrawButtonIcon(HDC hdc, const RECT& rect, UINT controlId, bool paused) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int cx = rect.left + width / 2;
    const int cy = rect.top + height / 2;
    const int size = (std::min)(width, height) / 3;
    HBRUSH brush = CreateSolidBrush(RGB(220, 220, 220));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));

    if (controlId == 1005) {
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(220, 220, 220));
        SelectObject(hdc, pen);
        Arc(hdc, cx - size, cy - size, cx + size, cy + size, cx - size / 2,
            cy - size, cx + size, cy - size / 2);
        POINT arrow[] = {{cx + size, cy - size / 2},
                         {cx + size - 8, cy - size / 2 - 8},
                         {cx + size - 8, cy - size / 2 + 8}};
        Polygon(hdc, arrow, 3);
        DeleteObject(SelectObject(hdc, oldPen));
        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
        return;
    }

    if (paused) {
        RECT left{cx - size, cy - size, cx - size / 3, cy + size};
        RECT right{cx + size / 3, cy - size, cx + size, cy + size};
        FillRect(hdc, &left, brush);
        FillRect(hdc, &right, brush);
    } else {
        POINT triangle[] = {{cx - size, cy - size}, {cx - size, cy + size},
                            {cx + size, cy}};
        Polygon(hdc, triangle, 3);
    }
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

void PlayCompletionSound() {
    constexpr wchar_t kAlarmPath[] = L"C:\\Windows\\Media\\Alarm10.wav";
    const DWORD attributes = GetFileAttributesW(kAlarmPath);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        PlaySoundW(kAlarmPath, nullptr, SND_FILENAME | SND_ASYNC);
    } else {
        PlaySoundW(L"SystemExclamation", nullptr, SND_ALIAS | SND_ASYNC);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE: {
            auto* state = new AppState();
            state->backgroundBrush = CreateSolidBrush(RGB(25, 25, 25));
            state->instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }
        case WM_CREATE: {
            auto& app = GetState(hwnd);
            app.restEdit = CreateWindowExW(
                0, L"EDIT", L"20",
                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1001),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

            app.workEdit = CreateWindowExW(
                0, L"EDIT", L"20",
                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1002),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

            app.restLabel = CreateWindowExW(
                0, L"STATIC", app.restName.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1006),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.workLabel = CreateWindowExW(
                0, L"STATIC", app.workName.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1007),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

            app.actionButton = CreateWindowExW(
                0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1003),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.resetButton = CreateWindowExW(
                0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1005),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

            app.bigIcon = static_cast<HICON>(
                LoadImageW(app.instance, MAKEINTRESOURCEW(IDI_MAIN_ICON), IMAGE_ICON, 32, 32,
                           LR_DEFAULTCOLOR));
            app.smallIcon = static_cast<HICON>(
                LoadImageW(app.instance, MAKEINTRESOURCEW(IDI_MAIN_ICON), IMAGE_ICON, 16, 16,
                           LR_DEFAULTCOLOR));
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app.bigIcon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app.smallIcon));
            AddTrayIcon(hwnd);
            UpdateButton(hwnd);
            UpdateWindowTitle(hwnd);
            return 0;
        }
        case WM_SIZE: {
            ResizeControls(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 420;
            info->ptMinTrackSize.y = 280;
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == kTrayOpenCommand) {
                RestoreWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == kTrayExitCommand) {
                RemoveTrayIcon(hwnd);
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == 1003 && HIWORD(wParam) == BN_CLICKED) {
                auto& app = GetState(hwnd);
                if (app.state == TimerState::Idle) {
                    StartCurrentPhase(hwnd);
                } else if (app.state == TimerState::AwaitingConfirm) {
                    StartCurrentPhase(hwnd);
                } else if (app.state == TimerState::Running ||
                           app.state == TimerState::Paused) {
                    TogglePause(hwnd);
                }
                return 0;
            }
            if (LOWORD(wParam) == 1005 && HIWORD(wParam) == BN_CLICKED) {
                ResetTimer(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == STN_CLICKED) {
                if (LOWORD(wParam) == 1006) {
                    BeginNameEdit(hwnd, Phase::Rest);
                } else if (LOWORD(wParam) == 1007) {
                    BeginNameEdit(hwnd, Phase::Work);
                }
                return 0;
            }
            if (LOWORD(wParam) == 1008 && HIWORD(wParam) == EN_KILLFOCUS) {
                CommitNameEdit(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == EN_SETFOCUS) {
                if (LOWORD(wParam) == 1001) {
                    SetActivePhase(hwnd, Phase::Rest);
                } else if (LOWORD(wParam) == 1002) {
                    SetActivePhase(hwnd, Phase::Work);
                }
            }
            return 0;
        }
        case kTrayMessage: {
            if (lParam == WM_LBUTTONDBLCLK) {
                RestoreWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
            SetActivePhase(hwnd, x < client.right / 2 ? Phase::Rest : Phase::Work);
            return 0;
        }
        case WM_TIMER: {
            if (wParam != kTimerId) {
                return 0;
            }

            auto& app = GetState(hwnd);
            if (app.state != TimerState::Running) {
                return 0;
            }

            if (app.remainingSeconds > 0) {
                --app.remainingSeconds;
                HWND activeEdit = app.phase == Phase::Work ? app.workEdit : app.restEdit;
                SetMinutes(activeEdit, app.remainingSeconds / 60);
                UpdateWindowTitle(hwnd);
            }

            if (app.remainingSeconds == 0) {
                KillTimer(hwnd, kTimerId);
                PlayCompletionSound();
                app.state = TimerState::AwaitingConfirm;
                app.selectedPhase = app.phase == Phase::Work ? Phase::Rest : Phase::Work;
                ApplyEditEnabled(hwnd, TRUE);
                RECT client{};
                GetClientRect(hwnd, &client);
                ResizeControls(hwnd, client.right, client.bottom);
                UpdateButton(hwnd);
                UpdateWindowTitle(hwnd);
            }
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            auto& app = GetState(hwnd);
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (control == app.restEdit) {
                const bool inactive = app.state == TimerState::Running &&
                                      app.phase != Phase::Rest;
                SetTextColor(hdc, inactive ? RGB(150, 150, 150) : RGB(0, 255, 0));
                SetBkColor(hdc, RGB(25, 25, 25));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            if (control == app.workEdit) {
                const bool inactive = app.state == TimerState::Running &&
                                      app.phase != Phase::Work;
                SetTextColor(hdc, inactive ? RGB(150, 150, 150) : RGB(255, 50, 50));
                SetBkColor(hdc, RGB(25, 25, 25));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            if (control == app.nameEdit) {
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(25, 25, 25));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            auto& app = GetState(hwnd);
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            SetBkColor(hdc, RGB(25, 25, 25));
            SetTextColor(hdc, RGB(200, 200, 200));
            if (control == app.restLabel || control == app.workLabel) {
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_CLOSE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_DRAWITEM: {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw == nullptr || draw->CtlType != ODT_BUTTON) {
                break;
            }

            RECT rect = draw->rcItem;
            FillRect(draw->hDC, &rect, GetState(hwnd).backgroundBrush);
            HBRUSH borderBrush = CreateSolidBrush(RGB(90, 90, 90));
            FrameRect(draw->hDC, &rect, borderBrush);
            DeleteObject(borderBrush);
            auto& app = GetState(hwnd);
            DrawButtonIcon(draw->hDC, rect, draw->CtlID, app.state == TimerState::Running);
            return TRUE;
        }
        case WM_ERASEBKGND: {
            auto& app = GetState(hwnd);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, app.backgroundBrush);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(170, 170, 170));
            HGDIOBJ oldPen = SelectObject(hdc, dividerPen);
            MoveToEx(hdc, client.right / 2, 42, nullptr);
            LineTo(hdc, client.right / 2, client.bottom - 92);
            SelectObject(hdc, oldPen);
            DeleteObject(dividerPen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app != nullptr) {
                RemoveTrayIcon(hwnd);
                KillTimer(hwnd, kTimerId);
                if (app->activeNumberFont != nullptr) {
                    DeleteObject(app->activeNumberFont);
                }
                if (app->inactiveNumberFont != nullptr) {
                    DeleteObject(app->inactiveNumberFont);
                }
                if (app->uiFont != nullptr) {
                    DeleteObject(app->uiFont);
                }
                if (app->backgroundBrush != nullptr) {
                    DeleteObject(app->backgroundBrush);
                }
                if (app->bigIcon != nullptr) {
                    DestroyIcon(app->bigIcon);
                }
                if (app->smallIcon != nullptr) {
                    DestroyIcon(app->smallIcon);
                }
                delete app;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    const wchar_t kClassName[] = L"ADHDLimiterWindowClass";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_MAIN_ICON), IMAGE_ICON, 32, 32,
                   LR_DEFAULTCOLOR));
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"ADHD-limiter", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
        nullptr, nullptr, instance, nullptr);

    if (hwnd == nullptr) {
        return 1;
    }

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
