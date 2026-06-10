#ifndef DIMMIT_COMPAT_IOREGISTRY_H
#define DIMMIT_COMPAT_IOREGISTRY_H

/*
 * IORegistryEntryCopyPath() is only declared starting in OS X 10.11. The
 * vendored ddcctl (vendor/ddcctl/src/DDC.c) calls it to read an IORegistry
 * path -- used to detect AMD GPUs (which need a longer DDC reply delay) and to
 * print device paths. On 10.9/10.10 the missing prototype makes the compiler
 * assume an int return, which truncates the returned CFStringRef pointer to 32
 * bits on x86_64 and crashes. We force-include this header into the ddcctl
 * build so the call site gets a correct prototype; compat_ioregistry.c
 * supplies the implementation (via IORegistryEntryGetPath, available since
 * 10.0). On 10.11+ the system declares and defines it, so this is a no-op.
 */
#include <AvailabilityMacros.h>

#if !defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED < 101100

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

CFStringRef IORegistryEntryCopyPath(io_registry_entry_t entry, const io_name_t plane);

#endif

#endif /* DIMMIT_COMPAT_IOREGISTRY_H */
