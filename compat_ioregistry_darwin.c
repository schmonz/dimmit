#include "compat_ioregistry_darwin.h"

#if !defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED < 101100

/*
 * Pre-10.11 implementation of IORegistryEntryCopyPath() in terms of the
 * long-standing IORegistryEntryGetPath(). io_string_t is a char[512] buffer;
 * the path is well under that for framebuffer entries.
 */
CFStringRef IORegistryEntryCopyPath(io_registry_entry_t entry, const io_name_t plane) {
    io_string_t path;
    if (IORegistryEntryGetPath(entry, plane, path) != KERN_SUCCESS) {
        return NULL;
    }
    return CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
}

#endif
