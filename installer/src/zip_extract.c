#include "installer_common.h"
#include "../lib/miniz.h"
#include <stdio.h>
#include <windows.h>
#include <direct.h> // for _mkdir

// Helper to create directories recursively
static bool EnsureDirectoryExists(const char* path) {
    char temp[MAX_PATH];
    snprintf(temp, sizeof(temp), "%s", path);
    
    // Iterate through path and create missing directories
    for (char* p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char backup = *p;
            *p = '\0';
            if (_mkdir(temp) != 0) {
                 if (errno != EEXIST) {
                      // It might be a drive root or something we can't create, ignore if it exists
                      DWORD attr = GetFileAttributesA(temp);
                      if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                          // Failed
                      }
                 }
            }
            *p = backup;
        }
    }
    return (_mkdir(temp) == 0 || errno == EEXIST);
}

bool Extract_ZipPayload(const void* zip_data, size_t zip_size, const char* dest_dir) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    printf("Initializing zip reader...\n");
    if (!mz_zip_reader_init_mem(&zip_archive, zip_data, zip_size, 0)) {
        printf("Error: Failed to initialize zip reader\n");
        return false;
    }

    int file_count = (int)mz_zip_reader_get_num_files(&zip_archive);
    printf("Found %d files in payload.\n", file_count);

    for (int i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            printf("Error: Failed to get file stat for index %d\n", i);
            mz_zip_reader_end(&zip_archive);
            return false;
        }

        char full_path[MAX_PATH];
        // Security check: validate path and ensure it stays within dest_dir
        if (!Path_IsSafe(dest_dir, file_stat.m_filename, full_path, sizeof(full_path))) {
            printf("Skipping unsafe path: %s\n", file_stat.m_filename);
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
            printf("Creating directory: %s\n", full_path);
            EnsureDirectoryExists(full_path);
        } else {
            printf("Extracting: %s\n", full_path);
            
            // Ensure parent directory exists
            char parent_dir[MAX_PATH];
            snprintf(parent_dir, sizeof(parent_dir), "%s", full_path);
            char* last_slash = strrchr(parent_dir, '\\');
            if (!last_slash) last_slash = strrchr(parent_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                EnsureDirectoryExists(parent_dir);
            }

            if (!mz_zip_reader_extract_to_file(&zip_archive, i, full_path, 0)) {
                printf("Error: Failed to extract file %s\n", full_path);
                // Continue or fail? Let's fail for integrity.
                mz_zip_reader_end(&zip_archive);
                return false;
            }
        }
    }

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
    strncpy(working_dir, exe_path, MAX_PATH);
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
        printf("CreateProcess failed (%d).\n", GetLastError());
        return false;
    }

    // Close process and thread handles. 
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
