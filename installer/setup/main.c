#include "../src/installer_common.h"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>

// Link against Comctl32.lib and Ole32.lib
// We'll handle this in Makefile.

// Simple Dialog Procedure
// For simplicity, we'll code the UI in C using standard windows controls if possible 
// or minimal dialog resource. Let's try to do it with a simple resource-less window or 
// a very basic dialog template.
// Actually, creating a dialog from memory template is tedious in raw C.
// Let's assume we can create a simple window with standard controls.

#define ID_EDIT_PATH 101
#define ID_BTN_BROWSE 102
#define ID_BTN_INSTALL 103
#define ID_CHECK_SHORTCUT 104
#define ID_CHECK_RUN 105
#define ID_STATIC_STATUS 106

char g_InstallPath[MAX_PATH];
HWND g_hEditPath;
HWND g_hStatus;
HWND g_hBtnInstall;
HWND g_hProgress;
BOOL g_CreateShortcut = TRUE;
BOOL g_RunNow = TRUE;

// Helper: Create Shortcut
HRESULT CreateLink(LPCSTR lpszPathObj, LPCSTR lpszPathLink, LPCSTR lpszDesc) {
    HRESULT hres;
    IShellLink* psl;
    CoInitialize(NULL);

    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                            &IID_IShellLink, (LPVOID*)&psl);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf;
        psl->lpVtbl->SetPath(psl, lpszPathObj);
        psl->lpVtbl->SetDescription(psl, lpszDesc);
        
        // Set working directory
        char workingDir[MAX_PATH];
        strncpy(workingDir, lpszPathObj, MAX_PATH);
        char* last_slash = strrchr(workingDir, '\\');
        if (last_slash) *last_slash = '\0';
        psl->lpVtbl->SetWorkingDirectory(psl, workingDir);

        hres = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hres)) {
            WCHAR wsz[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);
            hres = ppf->lpVtbl->Save(ppf, wsz, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
    return hres;
}

// Browse Folder Callback
int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) {
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return 0;
}

void DoBrowse(HWND hwndParent) {
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
        
        // Append "\HalChess" if not present
        if (strstr(path, "HalChess") == NULL) {
            strncat(path, "\\HalChess", MAX_PATH - strlen(path) - 1);
        }
        
        SetWindowTextA(g_hEditPath, path);
        CoTaskMemFree(pidl);
    }
}

DWORD WINAPI InstallThread(LPVOID lpParam) {
    (void)lpParam;
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);
    
    // Disable UI
    EnableWindow(g_hBtnInstall, FALSE);
    SetWindowTextA(g_hStatus, "Preparing...");

    // 1. Load Payload
    SetWindowTextA(g_hStatus, "Loading payload...");
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
         
         // Desktop
         if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, linkPath) == S_OK) {
             strncat(linkPath, "\\HalChess.lnk", MAX_PATH - strlen(linkPath) - 1);
             CreateLink(targetPath, linkPath, "Play HalChess");
         }
    }

    SetWindowTextA(g_hStatus, "Done!");
    
    // 4. Run Now
    if (SendMessage(GetDlgItem(GetParent(g_hStatus), ID_CHECK_RUN), BM_GETCHECK, 0, 0) == BST_CHECKED) {
         char targetPath[MAX_PATH];
         snprintf(targetPath, MAX_PATH, "%s\\HalChess.exe", path);
         System_LaunchProcess(targetPath);
    }
    
    PostMessage(GetParent(g_hStatus), WM_CLOSE, 0, 0);
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HalChessInstaller";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("HalChessInstaller", "HalChess Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 230, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
