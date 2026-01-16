#include "installer_common.h"
#include <stdio.h>

// This resource ID must match the one defined in the .rc file
#define PAYLOAD_RESOURCE_ID "PAYLOAD_ZIP"

const void* Payload_GetResource(size_t* out_size) {
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) {
        printf("Error: GetModuleHandle failed\n");
        return NULL;
    }

    // Locate the resource (Using RCDATA type)
    HRSRC hRes = FindResource(hModule, PAYLOAD_RESOURCE_ID, RT_RCDATA);
    if (!hRes) {
        // Fallback: Try looking for ID 101 or similar if string lookup fails, 
        // but for now assume named resource is used.
        printf("Error: Could not find resource %s\n", PAYLOAD_RESOURCE_ID);
        return NULL;
    }

    // Load the resource
    HGLOBAL hLoaded = LoadResource(hModule, hRes);
    if (!hLoaded) {
        printf("Error: LoadResource failed\n");
        return NULL;
    }

    // Lock the resource to get the pointer
    const void* pData = LockResource(hLoaded);
    if (!pData) {
        printf("Error: LockResource failed\n");
        return NULL;
    }

    // Get the size
    if (out_size) {
        *out_size = SizeofResource(hModule, hRes);
    }

    return pData;
}
