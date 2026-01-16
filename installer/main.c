#include "src/installer_common.h"
#include <windows.h>
#include <stdio.h>

#define ID_BTN_FAST 201
#define ID_BTN_CUSTOM 202

// Simple Mode Selection Window
LRESULT CALLBACK ModeSelectionProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Choose Installation Mode:", WS_CHILD | WS_VISIBLE | SS_CENTER, 
                0, 20, 300, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("BUTTON", "Fast Install\n(Portable Mode)", 
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE, 
                25, 60, 110, 60, hwnd, (HMENU)ID_BTN_FAST, NULL, NULL);

            CreateWindow("BUTTON", "Custom Install\n(Wizard Mode)", 
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE, 
                165, 60, 110, 60, hwnd, (HMENU)ID_BTN_CUSTOM, NULL, NULL);
            return 0;

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

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ModeSelectionProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HalChessInstallerMode";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Or load custom icon
    
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("HalChessInstallerMode", "HalChess Installer", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 310, 200, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
