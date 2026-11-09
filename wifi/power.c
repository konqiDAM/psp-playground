/*
 * power.c - download-session clock/backlight/keep-awake glue.
 */
#include "power.h"

#include <pspimpose.h>
#include <pspkernel.h>
#include <psppower.h>

#include <string.h>

#include "download_policy.h"

static DlClock s_clock;
static DlBacklight s_backlight;
static uint32_t s_last_tick_ms;
static int s_cpu_mhz;
static int s_cpu_applied;

static uint32_t now_ms(void)
{
    return (uint32_t)sceKernelGetSystemTimeLow() / 1000u;
}

void wifi_power_init(void)
{
    memset(&s_clock, 0, sizeof(s_clock));
    memset(&s_backlight, 0, sizeof(s_backlight));
    s_last_tick_ms = 0;
    s_cpu_mhz = scePowerGetCpuClockFrequencyInt();
    s_cpu_applied = 0;
}

void wifi_power_tick(void)
{
    uint32_t now = now_ms();
    if (!dl_due(now, s_last_tick_ms, DL_POWER_TICK_MS))
        return;
    s_last_tick_ms = now;
    scePowerTick(PSP_POWER_TICK_SUSPEND);
}

void wifi_power_download_begin(void)
{
    int cpu = scePowerGetCpuClockFrequencyInt();
    int bus = scePowerGetBusClockFrequencyInt();
    int current_s = sceImposeGetBacklightOffTime();

    s_cpu_mhz = cpu;
    s_cpu_applied = 0;
    if (dl_clock_begin(&s_clock, cpu, bus)) {
        scePowerSetClockFrequency(333, 333, 166);
        s_cpu_mhz = scePowerGetCpuClockFrequencyInt();
        if (s_cpu_mhz != 333) {
            s_clock.applied = 0; /* firmware kept the old clock */
            s_cpu_applied = 0;
        } else {
            s_cpu_applied = 1;
        }
    }
    if (dl_backlight_begin(&s_backlight, current_s, DL_BACKLIGHT_DOWNLOAD_S)) {
        if (sceImposeSetBacklightOffTime(DL_BACKLIGHT_DOWNLOAD_S) < 0)
            s_backlight.changed = 0;
    }
    s_last_tick_ms = now_ms();
}

void wifi_power_download_end(void)
{
    int restore = 0;
    int cpu = 0;
    int bus = 0;

    if (dl_backlight_end(&s_backlight, &restore))
        sceImposeSetBacklightOffTime(restore);
    if (dl_clock_end(&s_clock, &cpu, &bus)) {
        scePowerSetClockFrequency(333, cpu, bus);
        s_cpu_mhz = scePowerGetCpuClockFrequencyInt();
        s_cpu_applied = 0;
    }
}

void wifi_power_shutdown(void)
{
    wifi_power_download_end();
}

int wifi_power_cpu_mhz(void)
{
    return s_cpu_mhz;
}

int wifi_power_cpu_applied(void)
{
    return s_cpu_applied;
}
