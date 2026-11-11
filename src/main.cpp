#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

enum class TimerState { Idle, Running, Paused, AwaitingConfirm };
enum class Phase { Work, Rest };

struct AppState {
    HWND restEdit = nullptr;
    HWND workEdit = nullptr;
    HWND actionButton = nullptr;
    HWND pauseButton = nullptr;
    HWND resetButton = nullptr;

    HFONT activeNumberFont = nullptr;
    HFONT inactiveNumberFont = nullptr;
    HFONT uiFont = nullptr;

    HBRUSH backgroundBrush = nullptr;
    HINSTANCE instance = nullptr;
    HBITMAP startBitmap = nullptr;
    HBITMAP pauseBitmap = nullptr;
    HBITMAP confirmBitmap = nullptr;
    HBITMAP resetBitmap = nullptr;

    TimerState state = TimerState::Idle;
    Phase phase = Phase::Work;
    Phase selectedPhase = Phase::Work;

    int workMinutes = 20;
    int restMinutes = 20;
    int remainingSeconds = 0;
};

AppState& GetState(HWND hwnd) {
    return *reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void ResizeControls(HWND hwnd, int width, int height);

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
    SetWindowTextW(app.actionButton, L"Пуск");
    SetWindowTextW(app.pauseButton, L"Пауза");
    SetWindowTextW(app.resetButton, L"Сброс");
    EnableWindow(app.pauseButton, app.state == TimerState::Running ||
                                      app.state == TimerState::Paused);
    InvalidateRect(app.actionButton, nullptr, TRUE);
    InvalidateRect(app.pauseButton, nullptr, TRUE);
    InvalidateRect(app.resetButton, nullptr, TRUE);
}

void UpdateWindowTitle(HWND hwnd) {
    auto& app = GetState(hwnd);

    if (app.state == TimerState::Running) {
        const int minutes = app.remainingSeconds / 60;
        const int seconds = app.remainingSeconds % 60;
        std::wstring phase = app.phase == Phase::Work ? L"Работа" : L"Отдых";
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

    const int buttonWidth = (std::max)(90, width / 5);
    const int buttonHeight = (std::max)(45, height / 8);
    const int buttonX = (width - buttonWidth) / 2;
    const int buttonY = (std::max)(top + editHeight + 24, height - buttonHeight - margin - 4);
    MoveWindow(app.actionButton, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
    const int utilitySize = (std::max)(42, (std::min)(72, height / 9));
    const int utilityGap = 6;
    const int utilityY = height - utilitySize - margin;
    MoveWindow(app.resetButton, width - margin - utilitySize, utilityY,
               utilitySize, utilitySize, TRUE);
    MoveWindow(app.pauseButton, width - margin - utilitySize * 2 - utilityGap, utilityY,
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
    SendMessageW(app.actionButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);
    SendMessageW(app.pauseButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);
    SendMessageW(app.resetButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);

    InvalidateRect(hwnd, nullptr, TRUE);
}

void DrawButtonIcon(HDC hdc, const RECT& rect, HBITMAP bitmap) {
    if (bitmap == nullptr) {
        return;
    }

    BITMAP source{};
    GetObjectW(bitmap, sizeof(source), &source);
    HDC bitmapDc = CreateCompatibleDC(hdc);
    HGDIOBJ oldBitmap = SelectObject(bitmapDc, bitmap);
    const int width = (std::min)(source.bmWidth, rect.right - rect.left - 12);
    const int height = (std::min)(source.bmHeight, rect.bottom - rect.top - 12);
    const int x = rect.left + (rect.right - rect.left - width) / 2;
    const int y = rect.top + (rect.bottom - rect.top - height) / 2;
    StretchBlt(hdc, x, y, width, height, bitmapDc, 0, 0, source.bmWidth,
               source.bmHeight, SRCCOPY);
    SelectObject(bitmapDc, oldBitmap);
    DeleteDC(bitmapDc);
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

            app.actionButton = CreateWindowExW(
                0, L"BUTTON", L"Пуск",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1003),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.pauseButton = CreateWindowExW(
                0, L"BUTTON", L"Пауза",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1004),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.resetButton = CreateWindowExW(
                0, L"BUTTON", L"Сброс",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1005),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

            app.startBitmap = LoadBitmapW(app.instance, MAKEINTRESOURCEW(IDB_START));
            app.pauseBitmap = LoadBitmapW(app.instance, MAKEINTRESOURCEW(IDB_PAUSE));
            app.confirmBitmap = LoadBitmapW(app.instance, MAKEINTRESOURCEW(IDB_CONFIRM));
            app.resetBitmap = LoadBitmapW(app.instance, MAKEINTRESOURCEW(IDB_RESET));
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
            if (LOWORD(wParam) == 1003 && HIWORD(wParam) == BN_CLICKED) {
                auto& app = GetState(hwnd);
                if (app.state == TimerState::Idle) {
                    StartCurrentPhase(hwnd);
                } else if (app.state == TimerState::AwaitingConfirm) {
                    StartCurrentPhase(hwnd);
                }
                return 0;
            }
            if (LOWORD(wParam) == 1004 && HIWORD(wParam) == BN_CLICKED) {
                TogglePause(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == 1005 && HIWORD(wParam) == BN_CLICKED) {
                ResetTimer(hwnd);
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
                SetTextColor(hdc, RGB(0, 255, 0));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            if (control == app.workEdit) {
                SetTextColor(hdc, RGB(255, 50, 50));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw == nullptr || draw->CtlType != ODT_BUTTON) {
                break;
            }

            RECT rect = draw->rcItem;
            const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
            FillRect(draw->hDC, &rect, GetState(hwnd).backgroundBrush);
            HBRUSH borderBrush = CreateSolidBrush(RGB(90, 90, 90));
            FrameRect(draw->hDC, &rect, borderBrush);
            DeleteObject(borderBrush);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, RGB(220, 220, 220));
            HFONT font = reinterpret_cast<HFONT>(
                SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
            HGDIOBJ oldFont = SelectObject(draw->hDC, font);
            if (draw->CtlID == 1003) {
                wchar_t text[32]{};
                GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
                RECT textRect = rect;
                textRect.left += 48;
                DrawTextW(draw->hDC, text, -1, &textRect,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            auto& app = GetState(hwnd);
            HBITMAP bitmap = app.resetBitmap;
            if (draw->CtlID == 1003) {
                bitmap = app.state == TimerState::Running
                             ? app.pauseBitmap
                             : app.state == TimerState::AwaitingConfirm ? app.confirmBitmap
                                                                         : app.startBitmap;
            } else if (draw->CtlID == 1004) {
                bitmap = app.state == TimerState::Paused ? app.startBitmap : app.pauseBitmap;
            }
            DrawButtonIcon(draw->hDC, rect, bitmap);
            SelectObject(draw->hDC, oldFont);
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
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(200, 200, 200));
            HFONT labelFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(hdc, labelFont);
            RECT leftLabel{0, 8, client.right / 2 - 12, 32};
            RECT rightLabel{client.right / 2 + 12, 8, client.right, 32};
            DrawTextW(hdc, L"Отдых", -1, &leftLabel, DT_CENTER | DT_SINGLELINE);
            DrawTextW(hdc, L"Работа", -1, &rightLabel, DT_CENTER | DT_SINGLELINE);
            HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(170, 170, 170));
            HGDIOBJ oldPen = SelectObject(hdc, dividerPen);
            MoveToEx(hdc, client.right / 2, 42, nullptr);
            LineTo(hdc, client.right / 2, client.bottom - 92);
            SelectObject(hdc, oldPen);
            DeleteObject(dividerPen);
            SelectObject(hdc, oldFont);
            DeleteObject(labelFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app != nullptr) {
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
                if (app->startBitmap != nullptr) {
                    DeleteObject(app->startBitmap);
                }
                if (app->pauseBitmap != nullptr) {
                    DeleteObject(app->pauseBitmap);
                }
                if (app->confirmBitmap != nullptr) {
                    DeleteObject(app->confirmBitmap);
                }
                if (app->resetBitmap != nullptr) {
                    DeleteObject(app->resetBitmap);
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
