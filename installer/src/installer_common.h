#ifndef INSTALLER_COMMON_H
#define INSTALLER_COMMON_H

#include <windows.h>
#include <stdbool.h>

// Payload Utils
const void* Payload_GetResource(size_t* out_size);

// Extraction Utils
typedef void (*ProgressCallback)(int percentage, const char* status_text, void* user_data);
bool Extract_ZipPayload(const void* zip_data, size_t zip_size, const char* dest_dir, ProgressCallback cb, void* user_data);

// Path Utils
bool Path_IsSafe(const char* base_dir, const char* relative_path, char* out_full_path, size_t max_len);
bool Path_CreateRecursive(const char* dir);

// System Utils
bool System_LaunchProcess(const char* exe_path);

// Basic UI Helpers
void Installer_InitUI(void);
void Installer_ApplySystemFont(HWND divisor);
void Installer_ApplyFont(HWND hwnd, HFONT hFont);
void Installer_CenterWindow(HWND hwnd);
HFONT Installer_GetFontTitle(void);
HFONT Installer_GetFontNormal(void);
HFONT Installer_GetFontButton(void);
void Installer_DrawRoundedButton(DRAWITEMSTRUCT* dis, COLORREF bgColor, COLORREF textColor, HFONT hFont);

// High-Level Workflows
// Run the FastTrack installation (install to CWD\HalChess, launch immediately)
// Returns 0 on success, non-zero on error.
int ExecuteFastTrack(void);

// Run the Custom Setup Wizard (Path selection, Shortcuts, etc.)
// Returns 0 on success, non-zero on error/cancellation.
int ExecuteCustomSetup(HINSTANCE hInstance);

#endif // INSTALLER_COMMON_H
