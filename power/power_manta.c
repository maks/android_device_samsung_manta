/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/time.h>
#include <stdbool.h>
#include <pthread.h>
//#define LOG_NDEBUG 0

#define LOG_TAG "MantaPowerHAL"
#include <android/log.h>
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define CPU_MAX_FREQ_PATH "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
#define LOW_POWER_MAX_FREQ "800000"
#define NORMAL_MAX_FREQ "1700000"

struct manta_power_module {
    struct power_module base;
    pthread_mutex_t lock;
    int boostpulse_fd;
    int boostpulse_warned;
    const char *touchscreen_power_path;
};

static char scaling_max_freq_screen_on[51];

static bool low_power_mode = false;

static void sysfs_write(const char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

static void sysfs_read(const char *path, char *s,size_t buflen)
{
    char buf[80];
    int len;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = read(fd, s, buflen);
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

static void power_init(struct power_module *module)
{
    struct manta_power_module *manta = (struct manta_power_module *) module;
    struct dirent **namelist;
    int n;

    sysfs_read(CPU_MAX_FREQ_PATH, scaling_max_freq_screen_on, sizeof(scaling_max_freq_screen_on));

    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/target_loads", "70 300000:70 400000:75 500000:80 800000:85 1000000:70 1100000:80 1200000:85 1300000:90 1400000:95 1500000:99");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay","80000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/sync_freq", "1700000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_load", "95");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/up_threshold_any_cpu_freq", "1400000");
}

static void power_set_interactive(struct power_module *module, int on)
{
    struct manta_power_module *manta = (struct manta_power_module *) module;
    char buf[80];
    int ret;

    ALOGV("power_set_interactive: %d\n", on);

    if(!on) {
      sysfs_read(CPU_MAX_FREQ_PATH, scaling_max_freq_screen_on,sizeof(scaling_max_freq_screen_on));
   }

    /*
     * Lower maximum frequency when screen is off.  CPU 0 and 1 share a
     * cpufreq policy.
     */
    sysfs_write(CPU_MAX_FREQ_PATH,
                (!on || low_power_mode) ? LOW_POWER_MAX_FREQ : scaling_max_freq_screen_on);

    ALOGV("power_set_interactive: %d done\n", on);
}

static void manta_power_hint(struct power_module *module, power_hint_t hint,
                             void *data)
{
    struct manta_power_module *manta = (struct manta_power_module *) module;
    struct timespec now, diff;
    char buf[80];
    int len;

    switch (hint) {
     case POWER_HINT_INTERACTION:
        break;

     case POWER_HINT_VSYNC:
        break;

    case POWER_HINT_LOW_POWER:
        pthread_mutex_lock(&manta->lock);
        if (data)
            sysfs_write(CPU_MAX_FREQ_PATH, LOW_POWER_MAX_FREQ);
        else
            sysfs_write(CPU_MAX_FREQ_PATH, NORMAL_MAX_FREQ);
        low_power_mode = data;
        pthread_mutex_unlock(&manta->lock);
        break;
    default:
            break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct manta_power_module HAL_MODULE_INFO_SYM = {
    .base = {
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = POWER_MODULE_API_VERSION_0_2,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = POWER_HARDWARE_MODULE_ID,
            .name = "Manta Power HAL",
            .author = "The Android Open Source Project",
            .methods = &power_module_methods,
        },

        .init = power_init,
        .setInteractive = power_set_interactive,
        .powerHint = manta_power_hint,
    },

    .lock = PTHREAD_MUTEX_INITIALIZER,
    .boostpulse_fd = -1,
    .boostpulse_warned = 0,
};

