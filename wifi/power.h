/*
 * power.h - download-session CPU/backlight/keep-awake.
 *
 * WLAN system power-save (PSP Settings) lives in net/wifi.h.
 */
#ifndef PSP_WIFI_POWER_H
#define PSP_WIFI_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

void wifi_power_init(void);
void wifi_power_tick(void);
void wifi_power_download_begin(void);
void wifi_power_download_end(void);
void wifi_power_shutdown(void);
int wifi_power_cpu_mhz(void);
int wifi_power_cpu_applied(void);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_POWER_H */
