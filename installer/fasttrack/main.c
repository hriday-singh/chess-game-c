#include "../src/installer_common.h"
#include <windows.h>
#include <stdio.h>
#include <shlobj.h>

// FastTrack Installer:
// 1. Installs to CWD\HalChess
// 2. Extracts embedded payload
// 3. Launches HalChess.exe

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

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
    // Show a simple splash or just wait cursor? Let's use wait cursor for simplicity as per "minimal" UI spec.
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
