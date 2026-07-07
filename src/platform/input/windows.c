#include "platform/input/input.h"

#include <windows.h>
#include <hidusage.h>
#include <hidpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Windows surfaces the brightness keys as HID Consumer-page usages (like macOS).
 * We observe them with the Raw Input API on a dedicated thread: a message-only
 * window registered for the Consumer Control collection with RIDEV_INPUTSINK
 * (so a background daemon receives the keys with no focus). Observe-only: the OS
 * doesn't drive external monitors from these keys, so there's nothing to consume.
 * The step convention (1/16 of range) lives here, matching darwin.c.
 *
 * Best-effort: on many laptops the keys are handled by firmware and never reach
 * user space -- registration still succeeds, but no WM_INPUT arrives. The
 * dimmit-up/down clients remain the input in that case. */

#define CONSUMER_PAGE          0x0C
#define CONSUMER_CTRL_USAGE    0x01
#define BRIGHTNESS_UP_USAGE    0x6F   /* Consumer DisplayBrightnessIncrement */
#define BRIGHTNESS_DOWN_USAGE  0x70   /* Consumer DisplayBrightnessDecrement */

static input_adjust_fn g_on_adjust = NULL;
static pthread_t       g_thread;
static int             g_thread_started = 0;
static HWND            g_hwnd = NULL;
static DWORD           g_thread_id = 0;   /* for PostThreadMessage on stop */
static HANDLE          g_ready = NULL;    /* signalled once the thread is up/failed */
static volatile LONG   g_start_ok = 0;

/* Decode one WM_INPUT: pull the raw HID report(s), then use the device's
 * preparsed data to list the active Consumer-page usages and act on 0x6F/0x70. */
static void handle_raw_input(HRAWINPUT hri) {
    UINT size = 0;
    if (GetRawInputData(hri, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        return;
    BYTE stackbuf[256];
    BYTE *buf = (size <= sizeof(stackbuf)) ? stackbuf : (BYTE *)malloc(size);
    if (!buf) return;
    if (GetRawInputData(hri, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
        goto out_buf;

    RAWINPUT *ri = (RAWINPUT *)buf;
    if (ri->header.dwType != RIM_TYPEHID) goto out_buf;

    UINT ppsize = 0;
    if (GetRawInputDeviceInfo(ri->header.hDevice, RIDI_PREPARSEDDATA, NULL, &ppsize) != 0 || ppsize == 0)
        goto out_buf;
    PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)malloc(ppsize);
    if (!pp) goto out_buf;
    if (GetRawInputDeviceInfo(ri->header.hDevice, RIDI_PREPARSEDDATA, pp, &ppsize) == (UINT)-1)
        goto out_pp;

    ULONG maxus = HidP_MaxUsageListLength(HidP_Input, CONSUMER_PAGE, pp);
    if (maxus == 0) goto out_pp;
    USAGE *usages = (USAGE *)malloc(maxus * sizeof(USAGE));
    if (!usages) goto out_pp;

    DWORD count   = ri->data.hid.dwCount;
    DWORD hidsize = ri->data.hid.dwSizeHid;
    for (DWORD r = 0; r < count; r++) {
        BYTE *report = ri->data.hid.bRawData + (size_t)r * hidsize;
        ULONG n = maxus;
        if (HidP_GetUsages(HidP_Input, CONSUMER_PAGE, 0, usages, &n, pp,
                           (PCHAR)report, hidsize) != HIDP_STATUS_SUCCESS)
            continue;
        for (ULONG i = 0; i < n && g_on_adjust; i++) {
            if (usages[i] == BRIGHTNESS_UP_USAGE)        g_on_adjust(+1.0 / 16.0);
            else if (usages[i] == BRIGHTNESS_DOWN_USAGE) g_on_adjust(-1.0 / 16.0);
        }
    }
    free(usages);
out_pp:
    free(pp);
out_buf:
    if (buf != stackbuf) free(buf);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) { handle_raw_input((HRAWINPUT)lp); return 0; }
    return DefWindowProc(h, msg, wp, lp);
}

static void *input_thread(void *arg) {
    (void)arg;
    g_thread_id = GetCurrentThreadId();
    HINSTANCE hinst = GetModuleHandle(NULL);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = "DimmitInputWindow";
    RegisterClassExA(&wc);   /* harmless if already registered */

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "dimmit-input",
                             0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hinst, NULL);
    if (!g_hwnd) { InterlockedExchange(&g_start_ok, 0); SetEvent(g_ready); return NULL; }

    RAWINPUTDEVICE rid;
    rid.usUsagePage = CONSUMER_PAGE;
    rid.usUsage     = CONSUMER_CTRL_USAGE;
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = g_hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(g_hwnd); g_hwnd = NULL;
        InterlockedExchange(&g_start_ok, 0); SetEvent(g_ready); return NULL;
    }

    InterlockedExchange(&g_start_ok, 1);
    SetEvent(g_ready);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {   /* WM_QUIT -> returns 0 -> exit */
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    return NULL;
}

int input_start(input_adjust_fn on_adjust) {
    g_on_adjust = on_adjust;
    g_start_ok = 0;
    g_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ready) return -1;
    if (pthread_create(&g_thread, NULL, input_thread, NULL) != 0) {
        CloseHandle(g_ready); g_ready = NULL; return -1;
    }
    g_thread_started = 1;
    WaitForSingleObject(g_ready, INFINITE);
    CloseHandle(g_ready); g_ready = NULL;
    if (!g_start_ok) {
        pthread_join(g_thread, NULL); g_thread_started = 0; g_thread_id = 0;
        fprintf(stderr, "dimmit: brightness-key capture off (Raw Input registration "
                        "failed). The dimmit-up/down clients still work.\n");
        return -1;
    }
    return 0;
}

void input_stop(void) {
    if (!g_thread_started) return;
    if (g_thread_id) PostThreadMessage(g_thread_id, WM_QUIT, 0, 0);
    pthread_join(g_thread, NULL);
    g_thread_started = 0;
    g_thread_id = 0;
}
