#ifndef DIMMIT_COMPAT_IOREGISTRY_H
#define DIMMIT_COMPAT_IOREGISTRY_H

/*
 * IORegistryEntryCopyPath() exists only since OS X 10.11. The vendored ddcctl
 * (vendor/ddcctl/src/DDC.c) calls it to read an IORegistry path -- used to
 * detect AMD GPUs (which need a longer DDC reply delay) and to print device
 * paths. We force-include this header into the ddcctl C build (see
 * CMakeLists.txt) to make those calls work on 10.9/10.10.
 *
 * When the deployment target is < 10.11 we route every call to our own
 * dimmit_IORegistryEntryCopyPath() (implemented in compat_ioregistry.c via the
 * since-10.0 IORegistryEntryGetPath()). The rename is load-bearing, and a plain
 * matching prototype is NOT enough: building against a newer SDK (e.g. a Tahoe
 * host targeting -mmacosx-version-min=10.9) still declares the system
 * IORegistryEntryCopyPath attributed to IOKit.framework, so an unrenamed call
 * two-level-binds to IOKit and dyld aborts at launch on 10.9 ("Symbol not
 * found: _IORegistryEntryCopyPath"). Changing the called symbol name is what
 * keeps the reference out of the binary. We include <IOKit/IOKitLib.h> here,
 * before defining the macro, so the system declaration is processed under its
 * real name (and include-guarded) rather than being rewritten. On 10.11+ the
 * whole block is a no-op and the system implementation is used.
 */
#include <AvailabilityMacros.h>

#if !defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED < 101100

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

CFStringRef dimmit_IORegistryEntryCopyPath(io_registry_entry_t entry, const io_name_t plane);
#define IORegistryEntryCopyPath dimmit_IORegistryEntryCopyPath

#endif

#endif /* DIMMIT_COMPAT_IOREGISTRY_H */
