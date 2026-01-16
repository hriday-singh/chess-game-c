#define CINTERFACE
#define COBJMACROS
#include "installer_common.h"
#include <windows.h>
#include <stdio.h>
#include <shlobj.h>
#include <commctrl.h>

// =================================================================================
// FAST TRACK LOGIC
// =================================================================================

int ExecuteFastTrack(void) {
    // 1. Determine Install Location
    char current_dir[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, current_dir) == 0) {
        MessageBoxA(NULL, "Failed to get current directory.", "FastTrack Error", MB_ICONERROR);
        return 1;
    }

    char install_dir[MAX_PATH];
    snprintf(install_dir, MAX_PATH, "%s\\HalChess", current_dir);

    // 2. Load Payload
    size_t payload_size = 0;
    const void* payload_data = Payload_GetResource(&payload_size);

    if (!payload_data || payload_size == 0) {
        MessageBoxA(NULL, "Installer payload corrupted or missing.", "FastTrack Error", MB_ICONERROR);
        return 1;
    }

    // 3. Extract Payload
    HCURSOR hCursor = LoadCursor(NULL, IDC_WAIT);
    SetCursor(hCursor);

    if (!Extract_ZipPayload(payload_data, payload_size, install_dir)) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        MessageBoxA(NULL, "Failed to extract game files.\nCheck write permissions or disk space.", "FastTrack Error", MB_ICONERROR);
        return 1;
    }
    
    SetCursor(LoadCursor(NULL, IDC_ARROW));

    // 4. Launch Game
    char game_exe[MAX_PATH];
    snprintf(game_exe, MAX_PATH, "%s\\HalChess.exe", install_dir);

    if (!System_LaunchProcess(game_exe)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Installation successful, but failed to launch game at:\n%s", game_exe);
        MessageBoxA(NULL, msg, "FastTrack Warning", MB_ICONWARNING);
        return 1;
    }

    return 0;
}

// =================================================================================
// CUSTOM SETUP LOGIC
// =================================================================================

#define ID_EDIT_PATH 101
#define ID_BTN_BROWSE 102
#define ID_BTN_INSTALL 103
#define ID_CHECK_SHORTCUT 104
#define ID_CHECK_RUN 105
#define ID_STATIC_STATUS 106

static char g_InstallPath[MAX_PATH];
static HWND g_hEditPath;
static HWND g_hStatus;
static HWND g_hBtnInstall;
static BOOL g_SetupSuccess = FALSE;

// Helper: Create Shortcut
static HRESULT CreateLink(LPCSTR lpszPathObj, LPCSTR lpszPathLink, LPCSTR lpszDesc) {
    HRESULT hres;
    IShellLinkA* psl;
    CoInitialize(NULL);

    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                            &IID_IShellLinkA, (LPVOID*)&psl);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf;
        
        // Use Macros or Vtbl
        IShellLinkA_SetPath(psl, lpszPathObj);
        IShellLinkA_SetDescription(psl, lpszDesc);
        
        char workingDir[MAX_PATH];
        strncpy(workingDir, lpszPathObj, MAX_PATH);
        char* last_slash = strrchr(workingDir, '\\');
        if (last_slash) *last_slash = '\0';
        IShellLinkA_SetWorkingDirectory(psl, workingDir);

        hres = IShellLinkA_QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hres)) {
            WCHAR wsz[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);
            IPersistFile_Save(ppf, wsz, TRUE);
            IPersistFile_Release(ppf);
        }
        IShellLinkA_Release(psl);
    }
    CoUninitialize();
    return hres;
}

static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) {
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return 0;
}

static void DoBrowse(HWND hwndParent) {
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);

    BROWSEINFOA bi = { 0 };
    bi.hwndOwner = hwndParent;
    bi.lpszTitle = "Select Installation Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = (LPARAM)path;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        SHGetPathFromIDListA(pidl, path);
        if (strstr(path, "HalChess") == NULL) {
            strncat(path, "\\HalChess", MAX_PATH - strlen(path) - 1);
        }
        SetWindowTextA(g_hEditPath, path);
        CoTaskMemFree(pidl);
    }
}

static DWORD WINAPI InstallThread(LPVOID lpParam) {
    (void)lpParam;
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);
    
    EnableWindow(g_hBtnInstall, FALSE);
    SetWindowTextA(g_hStatus, "Preparing...");

    // 1. Load Payload
    size_t payload_size = 0;
    const void* payload_data = Payload_GetResource(&payload_size);
    
    if (!payload_data) {
        MessageBoxA(NULL, "Failed to load payload.", "Error", MB_ICONERROR);
        EnableWindow(g_hBtnInstall, TRUE);
        return 1;
    }

    // 2. Extract
    SetWindowTextA(g_hStatus, "Extracting files...");
    if (!Extract_ZipPayload(payload_data, payload_size, path)) {
        MessageBoxA(NULL, "Extraction failed.", "Error", MB_ICONERROR);
        EnableWindow(g_hBtnInstall, TRUE);
        return 1;
    }

    // 3. Shortcuts
    if (SendMessage(GetDlgItem(GetParent(g_hStatus), ID_CHECK_SHORTCUT), BM_GETCHECK, 0, 0) == BST_CHECKED) {
         SetWindowTextA(g_hStatus, "Creating shortcut...");
         char linkPath[MAX_PATH];
         char targetPath[MAX_PATH];
         snprintf(targetPath, MAX_PATH, "%s\\HalChess.exe", path);
         
         if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, linkPath) == S_OK) {
             strncat(linkPath, "\\HalChess.lnk", MAX_PATH - strlen(linkPath) - 1);
             CreateLink(targetPath, linkPath, "Play HalChess");
         }
    }

    SetWindowTextA(g_hStatus, "Done!");
    g_SetupSuccess = TRUE;
    
    // 4. Run Now
    if (SendMessage(GetDlgItem(GetParent(g_hStatus), ID_CHECK_RUN), BM_GETCHECK, 0, 0) == BST_CHECKED) {
         char targetPath[MAX_PATH];
         snprintf(targetPath, MAX_PATH, "%s\\HalChess.exe", path);
         System_LaunchProcess(targetPath);
    }
    
    PostMessage(GetParent(g_hStatus), WM_CLOSE, 0, 0);
    return 0;
}

static LRESULT CALLBACK SetupWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Default Path: %LOCALAPPDATA%\HalChess
            char defaultPath[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, defaultPath) == S_OK) {
                strncat(defaultPath, "\\HalChess", MAX_PATH - strlen(defaultPath) - 1);
            } else {
                strcpy(defaultPath, "C:\\HalChess");
            }

            CreateWindow("STATIC", "Install Location:", WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, NULL, NULL);
            g_hEditPath = CreateWindow("EDIT", defaultPath, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 280, 25, hwnd, (HMENU)ID_EDIT_PATH, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 300, 35, 75, 25, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

            CreateWindow("BUTTON", "Create Desktop Shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, 70, 200, 20, hwnd, (HMENU)ID_CHECK_SHORTCUT, NULL, NULL);
            CheckDlgButton(hwnd, ID_CHECK_SHORTCUT, BST_CHECKED);

            CreateWindow("BUTTON", "Run HalChess after install", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, 95, 200, 20, hwnd, (HMENU)ID_CHECK_RUN, NULL, NULL);
            CheckDlgButton(hwnd, ID_CHECK_RUN, BST_CHECKED);

            g_hBtnInstall = CreateWindow("BUTTON", "Install", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 150, 130, 100, 30, hwnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);
            g_hStatus = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 10, 170, 360, 20, hwnd, (HMENU)ID_STATIC_STATUS, NULL, NULL);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_BROWSE) {
                DoBrowse(hwnd);
            } else if (LOWORD(wParam) == ID_BTN_INSTALL) {
                CreateThread(NULL, 0, InstallThread, NULL, 0, NULL);
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int ExecuteCustomSetup(HINSTANCE hInstance) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = SetupWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HalChessSetupClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClassA(&wc)) {
        // Register failed, maybe already registered
    }

    HWND hwnd = CreateWindowA("HalChessSetupClass", "HalChess Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 230, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return g_SetupSuccess ? 0 : 1;
}
