/*
 * netconf.h - modal sceUtilityNetconf scan/configure flow.
 */
#ifndef PSP_WIFI_NETCONF_H
#define PSP_WIFI_NETCONF_H

#define NETCONF_TIMEOUT_MS 180000
#define NETCONF_SHUTDOWN_PUMP_FRAMES 300
#define NETCONF_TEXT_REPAINT_FRAMES 2
#define NETCONF_BG_COLOR 0xFF101820u
#define WIFI_FB_STRIDE 512
#define WIFI_FB_SIZE (WIFI_FB_STRIDE * 272 * 4)

typedef enum {
    NETCONF_RESULT_OK = 0,
    NETCONF_RESULT_EXITED = 1,
    NETCONF_ERR_INIT = -2,
    NETCONF_ERR_STATUS = -3,
    NETCONF_ERR_TIMEOUT = -4
} NetconfResult;

NetconfResult netconf_run(volatile const int *exit_requested, int *raw_error);

#endif /* PSP_WIFI_NETCONF_H */
