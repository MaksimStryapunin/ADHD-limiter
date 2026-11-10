#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

    HFONT numberFont = nullptr;
    HFONT symbolFont = nullptr;

    HBRUSH backgroundBrush = nullptr;

    TimerState state = TimerState::Idle;
    Phase phase = Phase::Work;

    int workMinutes = 20;
    int restMinutes = 20;
    int remainingSeconds = 0;
};

AppState& GetState(HWND hwnd) {
    return *reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
    const wchar_t* text = L"▶";
    if (app.state == TimerState::AwaitingConfirm) {
        text = L"✓";
    }
    SetWindowTextW(app.actionButton, text);

    const wchar_t* pauseText = app.state == TimerState::Paused ? L"▶" : L"||";
    SetWindowTextW(app.pauseButton, pauseText);
    EnableWindow(app.pauseButton, app.state == TimerState::Running ||
                                      app.state == TimerState::Paused);
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

    const int phaseMinutes = app.phase == Phase::Work ? app.workMinutes : app.restMinutes;
    app.remainingSeconds = phaseMinutes * 60;

    app.state = TimerState::Running;
    ApplyEditEnabled(hwnd, FALSE);
    SetTimer(hwnd, kTimerId, 1000, nullptr);
    UpdateButton(hwnd);
    UpdateWindowTitle(hwnd);
}

void AdvancePhase(HWND hwnd) {
    auto& app = GetState(hwnd);
    app.phase = app.phase == Phase::Work ? Phase::Rest : Phase::Work;
    StartCurrentPhase(hwnd);
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
    const int utilitySize = (std::max)(30, (std::min)(64, height / 10));
    const int utilityGap = 6;
    const int utilityY = height - utilitySize - margin;
    MoveWindow(app.resetButton, width - margin - utilitySize, utilityY,
               utilitySize, utilitySize, TRUE);
    MoveWindow(app.pauseButton, width - margin - utilitySize * 2 - utilityGap, utilityY,
               utilitySize, utilitySize, TRUE);

    const int numberFontHeight = -(std::max)(22, editHeight / 2);
    const int symbolFontHeight = -(std::max)(24, (std::min)(64, height / 8));

    if (app.numberFont != nullptr) {
        DeleteObject(app.numberFont);
    }
    if (app.symbolFont != nullptr) {
        DeleteObject(app.symbolFont);
    }

    app.numberFont = CreateFontW(numberFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    app.symbolFont = CreateFontW(symbolFontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    SendMessageW(app.workEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.numberFont), TRUE);
    SendMessageW(app.restEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.numberFont), TRUE);
    SendMessageW(app.actionButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.symbolFont), TRUE);
    SendMessageW(app.pauseButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.symbolFont), TRUE);
    SendMessageW(app.resetButton, WM_SETFONT, reinterpret_cast<WPARAM>(app.symbolFont), TRUE);

    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE: {
            auto* state = new AppState();
            state->backgroundBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
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
                0, L"BUTTON", L"▶", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1003),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.pauseButton = CreateWindowExW(
                0, L"BUTTON", L"||", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1004),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            app.resetButton = CreateWindowExW(
                0, L"BUTTON", L"↻", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1005),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);

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
                    app.phase = Phase::Work;
                    StartCurrentPhase(hwnd);
                } else if (app.state == TimerState::AwaitingConfirm) {
                    AdvancePhase(hwnd);
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
                PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC);
                app.state = TimerState::AwaitingConfirm;
                ApplyEditEnabled(hwnd, TRUE);
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
                SetTextColor(hdc, RGB(0, 128, 0));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            if (control == app.workEdit) {
                SetTextColor(hdc, RGB(180, 0, 0));
                return reinterpret_cast<INT_PTR>(app.backgroundBrush);
            }
            break;
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
            BeginPaint(hwnd, &ps);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app != nullptr) {
                KillTimer(hwnd, kTimerId);
                if (app->numberFont != nullptr) {
                    DeleteObject(app->numberFont);
                }
                if (app->symbolFont != nullptr) {
                    DeleteObject(app->symbolFont);
                }
                if (app->backgroundBrush != nullptr) {
                    DeleteObject(app->backgroundBrush);
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
