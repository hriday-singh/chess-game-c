#include "src/installer_common.h"
#include <windows.h>

#define ID_BTN_FAST 201
#define ID_BTN_CUSTOM 202
#define ID_LBL_TITLE 203

// Simple Mode Selection Window
LRESULT CALLBACK ModeSelectionProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            Installer_ApplySystemFont(hwnd);

            // Title
            HFONT hTitleFont = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            
            HWND hTitle = CreateWindow("STATIC", "Welcome to HalChess", WS_CHILD | WS_VISIBLE | SS_CENTER, 
                0, 30, 480, 40, hwnd, (HMENU)ID_LBL_TITLE, NULL, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

            CreateWindow("STATIC", "Select an installation mode:", WS_CHILD | WS_VISIBLE | SS_CENTER, 
                0, 75, 480, 20, hwnd, NULL, NULL, NULL);

            // Buttons - Wider and centered
            int btnWidth = 200;
            int btnHeight = 70;
            int spacing = 20;
            int totalWidth = (btnWidth * 2) + spacing;
            int startX = (500 - totalWidth) / 2;

            CreateWindow("BUTTON", "Fast Install\n(Portable Mode)", 
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE, 
                startX, 110, btnWidth, btnHeight, hwnd, (HMENU)ID_BTN_FAST, NULL, NULL);

            CreateWindow("BUTTON", "Custom Install\n(Wizard Mode)", 
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE, 
                startX + btnWidth + spacing, 110, btnWidth, btnHeight, hwnd, (HMENU)ID_BTN_CUSTOM, NULL, NULL);

            // Apply font to others
            Installer_ApplySystemFont(hwnd);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_FAST) {
                ShowWindow(hwnd, SW_HIDE); // Hide selection while installing
                ExecuteFastTrack();
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_BTN_CUSTOM) {
                ShowWindow(hwnd, SW_HIDE);
                ExecuteCustomSetup((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
                DestroyWindow(hwnd);
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    Installer_InitUI(); // Helper from install_logic.c/common

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ModeSelectionProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HalChessInstallerMode";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1)); // Custom Icon
    
    RegisterClassA(&wc);

    // Provide a wider, more comfortable window
    HWND hwnd = CreateWindowA("HalChessInstallerMode", "HalChess Installer", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 320, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
