#define _CRT_SECURE_NO_WARNINGS
#include "src/installer_common.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#define ID_BTN_FAST 201
#define ID_BTN_CUSTOM 202
#define ID_LBL_TITLE 203

// Simple Mode Selection Window
LRESULT CALLBACK ModeSelectionProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            Installer_ApplySystemFont(hwnd);

            // Check for existing installation
            char defaultPath[MAX_PATH];
            BOOL isUpdate = FALSE;
            if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, defaultPath) == S_OK) {
                size_t current_len = strlen(defaultPath);
                snprintf(defaultPath + current_len, sizeof(defaultPath) - current_len, "\\HalChess\\HalChess.exe");
                if (GetFileAttributesA(defaultPath) != INVALID_FILE_ATTRIBUTES) {
                    isUpdate = TRUE;
                }
            }

            // Title
            HWND hTitle = CreateWindow("STATIC", "Welcome to HalChess", WS_CHILD | WS_VISIBLE | SS_CENTER, 
                0, 60, 680, 50, hwnd, (HMENU)ID_LBL_TITLE, NULL, NULL);
            Installer_ApplyFont(hTitle, Installer_GetFontTitle());

            HWND hDesc = CreateWindow("STATIC", isUpdate ? "Existing installation detected" : "Please select an installation mode", WS_CHILD | WS_VISIBLE | SS_CENTER, 
                0, 120, 680, 30, hwnd, NULL, NULL, NULL);
            Installer_ApplyFont(hDesc, Installer_GetFontNormal());

            // Buttons - Wider and centered
            int btnWidth = 260;
            int btnHeight = 120;
            int spacing = 40;
            int totalWidth = (btnWidth * 2) + spacing;
            int startX = (700 - totalWidth) / 2;

            CreateWindow("BUTTON", "Fast Install\n(Portable Mode)", 
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_MULTILINE, 
                startX, 180, btnWidth, btnHeight, hwnd, (HMENU)ID_BTN_FAST, NULL, NULL);

            CreateWindow("BUTTON", isUpdate ? "Update / Uninstall\n(Advanced Mode)" : "Custom Install\n(Wizard Mode)", 
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_MULTILINE, 
                startX + btnWidth + spacing, 180, btnWidth, btnHeight, hwnd, (HMENU)ID_BTN_CUSTOM, NULL, NULL);

            return 0;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                Installer_DrawRoundedButton(dis, RGB(250, 250, 250), RGB(20, 20, 20), Installer_GetFontButton());
            }
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_FAST) {
                ShowWindow(hwnd, SW_HIDE); // Hide selection while installing
                int ret = ExecuteFastTrack();
                if (ret == INSTALLER_RET_BACK) {
                    ShowWindow(hwnd, SW_SHOW);
                } else {
                    DestroyWindow(hwnd);
                }
            } else if (LOWORD(wParam) == ID_BTN_CUSTOM) {
                ShowWindow(hwnd, SW_HIDE);
                int ret = ExecuteCustomSetup((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
                 if (ret == INSTALLER_RET_BACK) {
                    ShowWindow(hwnd, SW_SHOW);
                } else {
                    DestroyWindow(hwnd);
                }
            }
            break;

        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);

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

    // Provide a wider, more comfortable window (700x500)
    HWND hwnd = CreateWindowA("HalChessInstallerMode", "HalChess Installer", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 500, NULL, NULL, hInstance, NULL);

    Installer_CenterWindow(hwnd);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
