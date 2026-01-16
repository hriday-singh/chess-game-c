#include "installer_common.h"
#include <stdio.h>
#include <shlwapi.h>

// Link against shlwapi.lib (handled in Makefile via -lshlwapi)

bool Path_IsSafe(const char* base_dir, const char* relative_path, char* out_full_path, size_t max_len) {
    char combined_path[MAX_PATH];
    char canonical_base[MAX_PATH];
    char canonical_dest[MAX_PATH];

    // Get canonical absolute path for base_dir
    if (!GetFullPathNameA(base_dir, MAX_PATH, canonical_base, NULL)) {
        return false;
    }

    // Combine base + relative
    if (PathCombineA(combined_path, base_dir, relative_path) == NULL) {
        return false;
    }

    // Get canonical absolute path for the combined path
    if (!GetFullPathNameA(combined_path, MAX_PATH, canonical_dest, NULL)) {
        return false;
    }

    // Security check: The canonical destination MUST start with the canonical base.
    // This prevents "..\..\Windows\System32" attacks (Zip Slip).
    size_t base_len = strlen(canonical_base);
    if (strncmp(canonical_base, canonical_dest, base_len) != 0) {
        printf("Security Error: Path traversal detected! %s escapes %s\n", canonical_dest, canonical_base);
        return false;
    }

    // Check for directory separator at the boundary to ensure we didn't match a partial folder name
    // e.g. "C:\MyApp" matching "C:\MyAppPlus" is bad, but "C:\MyApp\File" is good.
    if (canonical_dest[base_len] != '\0' && canonical_dest[base_len] != '\\') {
         printf("Security Error: Path mismatch! %s\n", canonical_dest);
         return false;
    }

    if (out_full_path) {
        snprintf(out_full_path, max_len, "%s", canonical_dest);
    }

    return true;
}

// Helper to create directory recursively (like mkdir -p)
bool Path_CreateRecursive(const char* dir) {
    if (CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }

    // Try creating parent
    char parent[MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", dir);
    if (PathRemoveFileSpecA(parent)) {
        if (Path_CreateRecursive(parent)) {
             return (CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
        }
    }
    return false;
}
