#include "installer_common.h"
#include "../lib/miniz.h"
#include <stdio.h>
#include <windows.h>
#include <direct.h> // for _mkdir

// Helper to create directories recursively (Moved to path_utils or handled there)

bool Extract_ZipPayload(const void* zip_data, size_t zip_size, const char* dest_dir, ProgressCallback cb, void* user_data) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (cb) cb(0, "Initializing reader...", user_data);

    printf("Initializing zip reader...\n");
    if (!mz_zip_reader_init_mem(&zip_archive, zip_data, zip_size, 0)) {
        printf("Error: Failed to initialize zip reader\n");
        return false;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip_archive);
    printf("Found %d files in payload.\n", file_count);

    for (int i = 0; i < file_count; i++) {
        // Report progress
        if (cb) {
            int pct = (i * 100) / file_count;
            cb(pct, "Processing files...", user_data);
        }

        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            printf("Error: Failed to get file stat for index %d\n", i);
            // This error is not critical enough to stop extraction, just skip this file
            printf("Warning: Failed to get file stat for index %d. Skipping.\n", i);
            continue;
        }

        if (cb) {
             char status[MAX_PATH + 64];
             snprintf(status, sizeof(status), "Extracting: %s", file_stat.m_filename);
             cb((i * 100) / file_count, status, user_data);
        }

        // Determine full path
        char full_path[MAX_PATH];
        if (!Path_IsSafe(dest_dir, file_stat.m_filename, full_path, MAX_PATH)) {
            // Skip unsafe
            continue;
        }

        // Fix: Explicitly check for trailing slash to identify directories
        // Miniz might not set the attribute bit for some zips
        size_t path_len = strlen(full_path);
        if (path_len > 0 && (full_path[path_len - 1] == '/' || full_path[path_len - 1] == '\\')) {
             Path_CreateRecursive(full_path);
             continue;
        }

        // Create directories if needed
        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
            Path_CreateRecursive(full_path);
            continue;
        }

        // Ensure parent dir exists for files
        char parent_dir[MAX_PATH];
        snprintf(parent_dir, sizeof(parent_dir), "%s", full_path);
        char* last_slash = strrchr(parent_dir, '\\');
        if (!last_slash) last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0'; // Null-terminate to get parent path
            Path_CreateRecursive(parent_dir);
        }

        // Extract file
        if (!mz_zip_reader_extract_to_file(&zip_archive, i, full_path, 0)) {
            char msg[MAX_PATH + 512];
            snprintf(msg, sizeof(msg), "Failed to extract file:\n%s\n\nMiniz Error: %s\nWindows Error: %lu", 
                file_stat.m_filename, 
                mz_zip_get_error_string(mz_zip_get_last_error(&zip_archive)),
                GetLastError());
            MessageBoxA(NULL, msg, "Extraction Failure", MB_ICONERROR);
            mz_zip_reader_end(&zip_archive);
            return false;
        }
    }

    if (cb) cb(100, "Extraction complete!", user_data);
    mz_zip_reader_end(&zip_archive);
    return true;
}

bool System_LaunchProcess(const char* exe_path) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    // Hide console if it's a console app running from GUI, but our main app is GUI anyway.
    // si.wShowWindow = SW_SHOW; 
    // si.dwFlags = STARTF_USESHOWWINDOW;
    
    ZeroMemory(&pi, sizeof(pi));

    // Working directory should be the directory of the exe
    char working_dir[MAX_PATH];
    snprintf(working_dir, sizeof(working_dir), "%s", exe_path);
    char* last_bs = strrchr(working_dir, '\\');
    if (last_bs) *last_bs = '\0';
    else {
        char* last_fs = strrchr(working_dir, '/');
        if (last_fs) *last_fs = '\0';
    }

    printf("Launching: %s (CWD: %s)\n", exe_path, working_dir);

    if (!CreateProcessA(
        NULL,           // No module name (use command line)
        (LPSTR)exe_path,// Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        working_dir,    // Use exe's directory
        &si,            // Pointer to STARTUPINFO structure
        &pi             // Pointer to PROCESS_INFORMATION structure
    )) {
        printf("CreateProcess failed (%lu).\n", GetLastError());
        return false;
    }

    // Close process and thread handles. 
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
