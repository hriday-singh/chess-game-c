#ifndef INSTALLER_COMMON_H
#define INSTALLER_COMMON_H

#include <windows.h>
#include <stdbool.h>

// Payload Utils
const void* Payload_GetResource(size_t* out_size);

// Extraction Utils
bool Extract_ZipPayload(const void* zip_data, size_t zip_size, const char* dest_dir);

// Path Utils
bool Path_IsSafe(const char* base_dir, const char* relative_path, char* out_full_path, size_t max_len);

// System Utils
bool System_LaunchProcess(const char* exe_path);

// High-Level Workflows
// Run the FastTrack installation (install to CWD\HalChess, launch immediately)
// Returns 0 on success, non-zero on error.
int ExecuteFastTrack(void);

// Run the Custom Setup Wizard (Path selection, Shortcuts, etc.)
// Returns 0 on success, non-zero on error/cancellation.
int ExecuteCustomSetup(HINSTANCE hInstance);

#endif // INSTALLER_COMMON_H
