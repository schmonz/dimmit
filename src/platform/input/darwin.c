#include "platform/input/input.h"

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdio.h>

/* macOS surfaces the brightness keys as HID Consumer-page usages, below the
 * CGEventTap layer (the spike confirmed a tap sees nothing). We observe them
 * with an IOHIDManager -- no GUI session and (on Mavericks) no permission
 * needed. The macOS step convention (1/16 of range) lives here, not in the
 * daemon core. Observe-only: on our no-internal-display targets the OS does
 * nothing with these keys, so there's nothing to consume. */

#define CONSUMER_PAGE          0x0C
#define BRIGHTNESS_UP_USAGE    0x6F   /* Consumer DisplayBrightnessIncrement */
#define BRIGHTNESS_DOWN_USAGE  0x70   /* Consumer DisplayBrightnessDecrement */

static input_adjust_fn g_on_adjust = NULL;
static IOHIDManagerRef g_mgr = NULL;
static CFRunLoopRef    g_loop = NULL;
static pthread_t       g_thread;
static int             g_thread_started = 0;

static void value_cb(void *ctx, IOReturn res, void *sender, IOHIDValueRef value) {
    (void)ctx; (void)res; (void)sender;
    if (IOHIDValueGetIntegerValue(value) == 0) return;          /* press only, not release */
    IOHIDElementRef el = IOHIDValueGetElement(value);
    if (IOHIDElementGetUsagePage(el) != CONSUMER_PAGE) return;  /* ignore mouse/keyboard noise */
    uint32_t usage = IOHIDElementGetUsage(el);
    if (!g_on_adjust) return;
    if (usage == BRIGHTNESS_UP_USAGE)        g_on_adjust(+1.0 / 16.0);
    else if (usage == BRIGHTNESS_DOWN_USAGE) g_on_adjust(-1.0 / 16.0);
}

static void *hid_thread(void *arg) {
    (void)arg;
    g_loop = CFRunLoopGetCurrent();
    IOHIDManagerScheduleWithRunLoop(g_mgr, g_loop, kCFRunLoopDefaultMode);
    CFRunLoopRun();
    return NULL;
}

int input_start(input_adjust_fn on_adjust) {
    g_on_adjust = on_adjust;
    g_mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!g_mgr) return -1;
    IOHIDManagerSetDeviceMatching(g_mgr, NULL);                 /* all devices; filter in cb */
    IOHIDManagerRegisterInputValueCallback(g_mgr, value_cb, NULL);
    if (IOHIDManagerOpen(g_mgr, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        fprintf(stderr, "dimmit: brightness-key capture off (HID open failed). On modern "
                "macOS, grant dimmitd Input Monitoring. The dimmit-up/down clients still work.\n");
        CFRelease(g_mgr); g_mgr = NULL; return -1;
    }
    if (pthread_create(&g_thread, NULL, hid_thread, NULL) != 0) {
        IOHIDManagerClose(g_mgr, kIOHIDOptionsTypeNone); CFRelease(g_mgr); g_mgr = NULL;
        return -1;
    }
    g_thread_started = 1;
    return 0;
}

void input_stop(void) {
    if (g_loop) CFRunLoopStop(g_loop);
    if (g_thread_started) { pthread_join(g_thread, NULL); g_thread_started = 0; }
    if (g_mgr) {
        IOHIDManagerClose(g_mgr, kIOHIDOptionsTypeNone);
        CFRelease(g_mgr); g_mgr = NULL;
    }
}
