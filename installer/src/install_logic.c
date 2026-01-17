#define CINTERFACE
#define COBJMACROS
#include "installer_common.h"
#include <windows.h>
#include <stdio.h>
#include <shlobj.h>
#include <commctrl.h>

// =================================================================================
// UI HELPERS
// =================================================================================

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontButton = NULL;

void Installer_InitUI(void) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Create a modern system font (Segoe UI)
    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -32; // Title: ~24pt
    lf.lfWidth = 0;
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    snprintf(lf.lfFaceName, sizeof(lf.lfFaceName), "Segoe UI");
    g_hFontTitle = CreateFontIndirectA(&lf);

    lf.lfHeight = -20; // Normal: ~15pt
    lf.lfWeight = FW_NORMAL;
    g_hFontNormal = CreateFontIndirectA(&lf);

    lf.lfHeight = -22; // Button: ~16pt
    lf.lfWeight = FW_SEMIBOLD;
    g_hFontButton = CreateFontIndirectA(&lf);
}

HFONT Installer_GetFontTitle(void) { return g_hFontTitle; }
HFONT Installer_GetFontNormal(void) { return g_hFontNormal; }
HFONT Installer_GetFontButton(void) { return g_hFontButton; }

void Installer_ApplyFont(HWND hwnd, HFONT hFont) {
    if (hFont) {
        SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
}

void Installer_ApplySystemFont(HWND hwnd) {
    Installer_ApplyFont(hwnd, g_hFontNormal);
    
    // Apply to children (fallback logic)
    HWND hChild = GetWindow(hwnd, GW_CHILD);
    while (hChild) {
        SendMessage(hChild, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
}

void Installer_CenterWindow(HWND hwnd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int windowWidth = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

void Installer_DrawRoundedButton(DRAWITEMSTRUCT* dis, COLORREF bgColor, COLORREF textColor, HFONT hFont) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    BOOL isPressed = dis->itemState & ODS_SELECTED;
    // BOOL isFocused = dis->itemState & ODS_FOCUS;
    BOOL isDisabled = dis->itemState & ODS_DISABLED;

    // Fill background (window background color)
    HBRUSH hWinBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, hWinBrush);
    DeleteObject(hWinBrush);

    COLORREF buttonBg = bgColor;
    COLORREF buttonText = textColor;
    COLORREF borderColor;

    // Determine colors based on state
    if (isDisabled) {
        buttonBg = RGB(230, 230, 230);
        buttonText = RGB(160, 160, 160);
        borderColor = RGB(210, 210, 210);
    } else if (isPressed) {
        // Darken the background slightly
        int r = GetRValue(bgColor), g = GetGValue(bgColor), b = GetBValue(bgColor);
        buttonBg = RGB(max(0, r - 30), max(0, g - 30), max(0, b - 30));
        borderColor = RGB(max(0, r - 50), max(0, g - 50), max(0, b - 50));
    } else {
        buttonBg = bgColor;
        int r = GetRValue(bgColor), g = GetGValue(bgColor), b = GetBValue(bgColor);
        borderColor = RGB(max(0, r - 40), max(0, g - 40), max(0, b - 40));
    }

    // Draw main rounded button body
    HBRUSH hBrush = CreateSolidBrush(buttonBg);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    SelectObject(hdc, hBrush);
    SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, 12, 12);

    // Draw Text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, buttonText);
    SelectObject(hdc, hFont ? hFont : g_hFontNormal);

    char text[256];
    GetWindowTextA(dis->hwndItem, text, sizeof(text));
    
    // Calculate Text Height for Vertical Centering
    RECT calcRect = rc;
    // Reduce rect padding for text to avoid hitting edges
    InflateRect(&calcRect, -4, -2); 
    
    // Measure height required
    DrawTextA(hdc, text, -1, &calcRect, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    
    int textHeight = calcRect.bottom - calcRect.top;
    int buttonHeight = rc.bottom - rc.top;
    
    // Center vertically
    int topOffset = (buttonHeight - textHeight) / 2;
    
    RECT drawRect = rc;
    drawRect.top += topOffset;
    drawRect.bottom = drawRect.top + textHeight;
    InflateRect(&drawRect, -4, 0); // Keep horizontal padding

    if (isPressed) {
        OffsetRect(&drawRect, 1, 1);
    }

    DrawTextA(hdc, text, -1, &drawRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

    // Clean up
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

// Forward Declarations
static HRESULT CreateLink(LPCSTR lpszPathObj, LPCSTR lpszPathLink, LPCSTR lpszDesc);

// =================================================================================
// FAST TRACK LOGIC
// =================================================================================

#define ID_FT_PROGRESS 301
#define ID_FT_STATUS   302
#define ID_FT_LAUNCH   303
#define ID_FT_BACK     304
#define WM_INSTALL_COMPLETE (WM_USER + 1)

typedef struct {
    HWND hwnd;
    HWND hProgress;
    HWND hStatus;
    HWND hInfoStatus; // "Setting up portable..." label
    HWND hLaunchBtn;
    HWND hBackBtn;
    char installDir[MAX_PATH];
    BOOL success;
    BOOL backRequested;
} FastTrackContext;

static void FastTrack_ProgressCb(int pct, const char* status, void* user_data) {
    FastTrackContext* ctx = (FastTrackContext*)user_data;
    if (ctx && ctx->hProgress) {
        PostMessage(ctx->hProgress, PBM_SETPOS, (WPARAM)pct, 0);
        if (ctx->hStatus) SetWindowTextA(ctx->hStatus, status);
    }
}

static DWORD WINAPI FastTrack_Worker(LPVOID lpParam) {
    FastTrackContext* ctx = (FastTrackContext*)lpParam;
    
    // 1. Load Payload
    SetWindowTextA(ctx->hStatus, "Loading payload...");
    size_t payload_size = 0;
    const void* payload_data = Payload_GetResource(&payload_size);

    if (!payload_data || payload_size == 0) {
        MessageBoxA(NULL, "Installer payload corrupted or missing.", "FastTrack Error", MB_ICONERROR);
        ctx->success = FALSE;
        PostMessage(ctx->hwnd, WM_CLOSE, 0, 0);
        return 1;
    }

    // 2. Extract with Progress
    if (!Extract_ZipPayload(payload_data, payload_size, ctx->installDir, FastTrack_ProgressCb, ctx)) {
        ctx->success = FALSE;
        MessageBoxA(NULL, "Failed to extract game files.\nCheck write permissions or disk space.", "FastTrack Error", MB_ICONERROR);
        PostMessage(ctx->hwnd, WM_CLOSE, 0, 0);
    } else {
        ctx->success = TRUE;
        
        // Signal UI thread that we are done
        PostMessage(ctx->hwnd, WM_INSTALL_COMPLETE, 0, 0);
    }

    return 0;
}

static LRESULT CALLBACK FastTrackProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static FastTrackContext* s_ctx = NULL;

    switch (uMsg) {
        case WM_CREATE: {
             CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
             s_ctx = (FastTrackContext*)cs->lpCreateParams;
             s_ctx->hwnd = hwnd;
             return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                Installer_DrawRoundedButton(dis, RGB(250, 250, 250), RGB(20, 20, 20), Installer_GetFontButton());
            }
            return TRUE;
        }
        case WM_INSTALL_COMPLETE: {
             // Show Launch Button and hide progress/info/back
             ShowWindow(s_ctx->hProgress, SW_HIDE);
             if (s_ctx->hInfoStatus) ShowWindow(s_ctx->hInfoStatus, SW_HIDE);
             if (s_ctx->hBackBtn) ShowWindow(s_ctx->hBackBtn, SW_HIDE);
             
             // Update Status text to be more prominent
             SetWindowTextA(s_ctx->hStatus, "Installation Successful!");
             
             // Move status to a more centered position for the end screen
             SetWindowPos(s_ctx->hStatus, NULL, 50, 100, 500, 40, SWP_NOZORDER);
             Installer_ApplyFont(s_ctx->hStatus, Installer_GetFontTitle());

             // Create Shortcut in the root directory (outside the HalChess folder)
             char rootDir[MAX_PATH];
             char shortcutPath[MAX_PATH + 64];
             char exePath[MAX_PATH + 64];
             
             snprintf(rootDir, sizeof(rootDir), "%s", s_ctx->installDir);
             char* last_slash = strrchr(rootDir, '\\');
             if (last_slash) {
                 *last_slash = '\0'; // Get parent directory
                 
                 snprintf(exePath, sizeof(exePath), "%s\\HalChess.exe", s_ctx->installDir);
                 snprintf(shortcutPath, sizeof(shortcutPath), "%s\\HalChess.lnk", rootDir);
                 CreateLink(exePath, shortcutPath, "Play HalChess (Portable)");
             }

             // Create Launch Button at center bottom
             s_ctx->hLaunchBtn = CreateWindow("BUTTON", "Launch HalChess Now", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
                 150, 180, 300, 60, hwnd, (HMENU)ID_FT_LAUNCH, GetModuleHandle(NULL), NULL);
             Installer_ApplyFont(s_ctx->hLaunchBtn, Installer_GetFontButton());

             InvalidateRect(hwnd, NULL, TRUE);
             UpdateWindow(hwnd);
             return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_FT_LAUNCH) {
                char game_exe[MAX_PATH + 32];
                snprintf(game_exe, sizeof(game_exe), "%s\\HalChess.exe", s_ctx->installDir);
                System_LaunchProcess(game_exe);
                s_ctx->success = TRUE;
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_FT_BACK) {
                s_ctx->backRequested = TRUE;
                DestroyWindow(hwnd);
            }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (s_ctx->backRequested) {
                PostQuitMessage(INSTALLER_RET_BACK);
            } else if (s_ctx->success) {
                PostQuitMessage(INSTALLER_RET_SUCCESS);
            } else {
                PostQuitMessage(INSTALLER_RET_ERROR);
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int ExecuteFastTrack(void) {
    char current_dir[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, current_dir) == 0) return 1;

    FastTrackContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (strlen(current_dir) + 10 >= sizeof(ctx.installDir)) {
        return 1; // Path too long
    }
    snprintf(ctx.installDir, sizeof(ctx.installDir), "%s\\HalChess", current_dir);

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FastTrackProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "HalChessFastTrackClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm = (HICON)LoadImage(wc.hInstance, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowA("HalChessFastTrackClass", "Installing HalChess (Portable)", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, wc.hInstance, &ctx);

    Installer_CenterWindow(hwnd);
    Installer_ApplySystemFont(hwnd);

    ctx.hInfoStatus = CreateWindow("STATIC", "Setting up portable installation. Please wait...", WS_CHILD | WS_VISIBLE | SS_CENTER, 
        20, 60, 560, 40, hwnd, NULL, NULL, NULL);

    ctx.hProgress = CreateWindow(PROGRESS_CLASS, "", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 
        50, 150, 500, 40, hwnd, (HMENU)ID_FT_PROGRESS, NULL, NULL);
    
    ctx.hStatus = CreateWindow("STATIC", "Initializing...", WS_CHILD | WS_VISIBLE | SS_CENTER, 
        50, 220, 500, 40, hwnd, (HMENU)ID_FT_STATUS, NULL, NULL);

    // Back Button (Top Left)
    ctx.hBackBtn = CreateWindow("BUTTON", "Back", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 
        20, 10, 80, 30, hwnd, (HMENU)ID_FT_BACK, NULL, NULL);
    Installer_ApplyFont(ctx.hBackBtn, Installer_GetFontButton());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    CreateThread(NULL, 0, FastTrack_Worker, &ctx, 0, NULL);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam; // Returns return code from PostQuitMessage
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
#define ID_LBL_PATH 107
#define ID_BTN_BACK 108
#define ID_PROGRESS_BAR 109

static HWND g_hEditPath;
static HWND g_hStatus;
static HWND g_hBtnInstall;
static HWND g_hProgress;
static BOOL g_SetupSuccess = FALSE;
static BOOL g_BackRequested = FALSE;

// Helper: Create Shortcut
static HRESULT CreateLink(LPCSTR lpszPathObj, LPCSTR lpszPathLink, LPCSTR lpszDesc) {
    HRESULT hres;
    IShellLinkA* psl;
    CoInitialize(NULL);

    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                            &IID_IShellLinkA, (LPVOID*)&psl);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf;
        
        // Use VTable access directly for C compatibility
        psl->lpVtbl->SetPath(psl, lpszPathObj);
        psl->lpVtbl->SetDescription(psl, lpszDesc);
        
        char workingDir[MAX_PATH];
        if (strlen(lpszPathObj) >= sizeof(workingDir)) {
             psl->lpVtbl->Release(psl);
             CoUninitialize();
             return E_FAIL;
        }
        snprintf(workingDir, sizeof(workingDir), "%s", lpszPathObj);
        char* last_slash = strrchr(workingDir, '\\');
        if (last_slash) *last_slash = '\0';
        psl->lpVtbl->SetWorkingDirectory(psl, workingDir);

        hres = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hres)) {
            WCHAR wsz[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);
            ppf->lpVtbl->Save(ppf, wsz, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
    return hres;
}

static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    (void)lParam; // Unused
    if (uMsg == BFFM_INITIALIZED) {
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return 0;
}

static void DoBrowse(HWND hwndParent) {
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);

    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = hwndParent;
    bi.lpszTitle = "Select Installation Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = (LPARAM)path;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        SHGetPathFromIDListA(pidl, path);
        if (strstr(path, "HalChess") == NULL) {
            size_t current_len = strlen(path);
            if (current_len + 10 < sizeof(path)) {
                snprintf(path + current_len, sizeof(path) - current_len, "\\HalChess");
            }
        }
        SetWindowTextA(g_hEditPath, path);
        CoTaskMemFree(pidl);
    }
}

// Progress Callback for Custom Install
static void Custom_ProgressCb(int pct, const char* status, void* user_data) {
    (void)user_data;
    PostMessage(g_hProgress, PBM_SETPOS, (WPARAM)pct, 0);
    SetWindowTextA(g_hStatus, status);
}

static DWORD WINAPI InstallThread(LPVOID lpParam) {
    (void)lpParam;
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);
    
    EnableWindow(g_hBtnInstall, FALSE);
    SetWindowTextA(g_hStatus, "Preparing...");
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);

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
    if (!Extract_ZipPayload(payload_data, payload_size, path, Custom_ProgressCb, NULL)) {
        MessageBoxA(NULL, "Extraction failed.", "Error", MB_ICONERROR);
        EnableWindow(g_hBtnInstall, TRUE);
        return 1;
    }

    // 3. Shortcuts
    if (SendMessage(GetDlgItem(GetParent(g_hStatus), ID_CHECK_SHORTCUT), BM_GETCHECK, 0, 0) == BST_CHECKED) {
         SetWindowTextA(g_hStatus, "Creating shortcut...");
         SendMessage(g_hProgress, PBM_SETPOS, 100, 0); // User feedback
         char linkPath[MAX_PATH];
         char targetPath[MAX_PATH];
         if (strlen(path) + 15 < MAX_PATH) {
            snprintf(targetPath, MAX_PATH, "%s\\HalChess.exe", path);
         
          if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, linkPath) == S_OK) {
                size_t current_len = strlen(linkPath);
                if (current_len + 15 < sizeof(linkPath)) {
                    snprintf(linkPath + current_len, sizeof(linkPath) - current_len, "\\HalChess.lnk");
                    CreateLink(targetPath, linkPath, "Play HalChess");
                }
            }
         }
    }

    SetWindowTextA(g_hStatus, "Done!");
    g_SetupSuccess = TRUE;
    
    // 4. Run Now
    if (SendMessage(GetDlgItem(GetParent(g_hStatus), ID_CHECK_RUN), BM_GETCHECK, 0, 0) == BST_CHECKED) {
         char targetPath[MAX_PATH];
         if (strlen(path) + 15 < MAX_PATH) {
            snprintf(targetPath, MAX_PATH, "%s\\HalChess.exe", path);
            System_LaunchProcess(targetPath);
         }
    }
    
    PostMessage(GetParent(g_hStatus), WM_CLOSE, 0, 0);
    return 0;
}

// Recursively delete directory
static void RecursiveDelete(const char* path) {
    char searchPath[MAX_PATH];
    if (strlen(path) + 5 >= sizeof(searchPath)) return;
    snprintf(searchPath, sizeof(searchPath), "%s\\*.*", path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
            char filePath[4096];
            if (strlen(path) + strlen(fd.cFileName) + 2 < sizeof(filePath)) {
                snprintf(filePath, sizeof(filePath), "%s\\%s", path, fd.cFileName);
                
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    RecursiveDelete(filePath);
                    RemoveDirectoryA(filePath);
                } else {
                    DeleteFileA(filePath);
                }
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    RemoveDirectoryA(path);
}

static DWORD WINAPI UninstallThread(LPVOID lpParam) {
    (void)lpParam;
    char path[MAX_PATH];
    GetWindowTextA(g_hEditPath, path, MAX_PATH);

    int ret = MessageBoxA(NULL, "Are you sure you want to remove HalChess and all its components?", "Confirm Uninstall", MB_YESNO | MB_ICONQUESTION);
    if (ret != IDYES) {
        EnableWindow(g_hBtnInstall, TRUE);
        return 0;
    }

    EnableWindow(g_hBtnInstall, FALSE);
    SetWindowTextA(g_hStatus, "Uninstalling...");
    
    // 1. Delete Shortcut
    char desktopPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath) == S_OK) {
        char linkPath[MAX_PATH + 32];
        if (strlen(desktopPath) + 15 < sizeof(linkPath)) {
            snprintf(linkPath, sizeof(linkPath), "%s\\HalChess.lnk", desktopPath);
            DeleteFileA(linkPath);
        }
    }

    // 2. Delete Files
    RecursiveDelete(path);

    SetWindowTextA(g_hStatus, "Uninstalled.");
    MessageBoxA(NULL, "Uninstallation Complete.", "Info", MB_OK);
    
    PostMessage(GetParent(g_hStatus), WM_CLOSE, 0, 0);
    return 0;
}


static LRESULT CALLBACK SetupWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            Installer_ApplySystemFont(hwnd);
            
            // Default Path: %LOCALAPPDATA%\HalChess
            char defaultPath[MAX_PATH];
            BOOL isUpdate = FALSE;

            if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, defaultPath) == S_OK) {
                size_t current_len = strlen(defaultPath);
                if (current_len + 10 < sizeof(defaultPath)) {
                    snprintf(defaultPath + current_len, sizeof(defaultPath) - current_len, "\\HalChess");
                    
                    // Check if exists
                    char exeCheck[MAX_PATH + 32];
                    if (strlen(defaultPath) + 15 < sizeof(exeCheck)) {
                        snprintf(exeCheck, sizeof(exeCheck), "%s\\HalChess.exe", defaultPath);
                        if (GetFileAttributesA(exeCheck) != INVALID_FILE_ATTRIBUTES) {
                            isUpdate = TRUE;
                        }
                    }
                }
            } else {
                snprintf(defaultPath, sizeof(defaultPath), "C:\\HalChess");
            }

            // --- Layout: Airy and Spacious (700x500) ---
            
            // Install Location Label
            HWND hLocLabel = CreateWindow("STATIC", "Installation Directory:", WS_CHILD | WS_VISIBLE, 40, 40, 300, 30, hwnd, NULL, NULL, NULL);
            Installer_ApplyFont(hLocLabel, Installer_GetFontNormal());
            
            // Edit Path
            g_hEditPath = CreateWindow("EDIT", defaultPath, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 40, 75, 500, 35, hwnd, (HMENU)ID_EDIT_PATH, NULL, NULL);
            Installer_ApplyFont(g_hEditPath, Installer_GetFontNormal());
            
            // Browse Button
            HWND hBrowse = CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 550, 75, 100, 35, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
            Installer_ApplyFont(hBrowse, Installer_GetFontButton());
            InvalidateRect(hBrowse, NULL, TRUE);

            // Shortcuts
            HWND hShortcut = CreateWindow("BUTTON", "Create Desktop Shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 140, 400, 30, hwnd, (HMENU)ID_CHECK_SHORTCUT, NULL, NULL);
            Installer_ApplyFont(hShortcut, Installer_GetFontNormal());
            CheckDlgButton(hwnd, ID_CHECK_SHORTCUT, BST_CHECKED);

            // Run After Install
            HWND hRun = CreateWindow("BUTTON", "Run HalChess after installation", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 185, 400, 30, hwnd, (HMENU)ID_CHECK_RUN, NULL, NULL);
            Installer_ApplyFont(hRun, Installer_GetFontNormal());
            CheckDlgButton(hwnd, ID_CHECK_RUN, BST_CHECKED);

            // Progress Bar
            g_hProgress = CreateWindow(PROGRESS_CLASS, "", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 40, 260, 610, 25, hwnd, (HMENU)ID_PROGRESS_BAR, NULL, NULL);

            // Buttons Area
            int btnY = 350;
            if (isUpdate) {
                // Update and Uninstall buttons side by side
                g_hBtnInstall = CreateWindow("BUTTON", "Update Now", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 180, btnY, 150, 50, hwnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);
                CreateWindow("BUTTON", "Uninstall", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 370, btnY, 150, 50, hwnd, (HMENU)999, NULL, NULL);
            } else {
                // Centered Install button
                g_hBtnInstall = CreateWindow("BUTTON", "Install Now", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 275, btnY, 150, 50, hwnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);
            }
 
            // Status Static
            g_hStatus = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 40, 420, 610, 30, hwnd, (HMENU)ID_STATIC_STATUS, NULL, NULL);
            Installer_ApplyFont(g_hStatus, Installer_GetFontNormal());
            
            // Back Button (Top Left)
            CreateWindow("BUTTON", "Back", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
                20, 5, 80, 30, hwnd, (HMENU)ID_BTN_BACK, NULL, NULL);
            // Move status to avoid overlap
            SetWindowPos(g_hStatus, NULL, 40, 310, 610, 30, SWP_NOZORDER);

            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                Installer_DrawRoundedButton(dis, RGB(250, 250, 250), RGB(20, 20, 20), Installer_GetFontButton());
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_BROWSE) {
                DoBrowse(hwnd);
            } else if (LOWORD(wParam) == ID_BTN_INSTALL) {
                CreateThread(NULL, 0, InstallThread, NULL, 0, NULL);
            } else if (LOWORD(wParam) == 999) { // Uninstall
                CreateThread(NULL, 0, UninstallThread, NULL, 0, NULL);
            } else if (LOWORD(wParam) == ID_BTN_BACK) {
                g_BackRequested = TRUE;
                DestroyWindow(hwnd);
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_BackRequested) {
                 PostQuitMessage(INSTALLER_RET_BACK);
            } else if (g_SetupSuccess) {
                 PostQuitMessage(INSTALLER_RET_SUCCESS);
            } else {
                 PostQuitMessage(INSTALLER_RET_ERROR);
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int ExecuteCustomSetup(HINSTANCE hInstance) {
    g_BackRequested = FALSE;
    g_SetupSuccess = FALSE;
    Installer_InitUI();

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SetupWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HalChessSetupClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    
    RegisterClassExA(&wc);

    // Spacious Window Size (700x500)
    HWND hwnd = CreateWindowA("HalChessSetupClass", "HalChess Setup Wizard", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 500, NULL, NULL, hInstance, NULL);

    Installer_CenterWindow(hwnd);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return msg.wParam;
}
