// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-hwmon.c - AERO X16 laptop hwmon sensor interface (Layer 1)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 */


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/dmi.h>

#include <linux/acpi.h>

#include "aerox16-laptop.h"

/*
 * Helper method. Reverses byte order of fan RPM.
 * This is needed, since the embedded controller stores the value in big-endian
 * while x86 is little-endian.
 */
u16 convert_fan_rpm(int val)
{
    u16 fan_rpm = val;
    return rol16(fan_rpm, 8);
}

umode_t aerox16_laptop_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
    switch (type) {
    case hwmon_temp:
        switch (attr) {
        case hwmon_temp_input:
            return 0444;
        default:
            break;
        }
        break;
    case hwmon_fan:
        switch (attr) {
        case hwmon_fan_input:
            return 0444;
        default:
            break;
        }
        break;
    case hwmon_pwm:
        switch (attr) {
        case hwmon_pwm_input:
            return 0444;
        case hwmon_pwm_enable:
            return 0444;
        default:
            break;
        }
    default:
        break;
    }
    return 0;
}

int aerox16_laptop_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
    int ret, output;
    u8 result;
    u8 fan_channels[] = { FAN_CPU_RPM, FAN_GPU_RPM, FAN_THREE_RPM, FAN_FOUR_RPM };
    u8 fan_pwm_channels[] = { FAN_PWM, GPU_FAN_DUTY };

    switch (type) {
    case hwmon_temp:
        switch (channel) {
        case 0:
            ret = aerox16_laptop_get_devstate(TEMP_CPU, &output);
            if (ret)
                break;
            *val = output * 1000;
            break;
        case 1:
            ret = aerox16_laptop_get_devstate(TEMP_GPU, &output);
            if (ret)
                break;
            *val = output * 1000;
            break;
        case 2:
            // Motherboard temp cannot be read through WMI
            ret = ec_read(0x62, &result);
            if (ret)
                break;
            *val = result * 1000;
            break;
        default:
            *val = 0;
            break;
        }
        break;
    case hwmon_fan:
        ret = aerox16_laptop_get_devstate(fan_channels[channel], &output);
        if (ret)
            break;
        // Gigabyte AERO laptops store fan RPM in little-endian.
        // Only "GIGABYTE AERO" (AERO X16) is supported; all other models are disabled.
        if (!strcmp(dmi_get_system_info(DMI_PRODUCT_FAMILY), "GIGABYTE AERO"))
            *val = output;
        else
            *val = convert_fan_rpm(output);
        break;
    case hwmon_pwm:
        switch (attr) {
        case hwmon_pwm_input:
            ret = aerox16_laptop_get_devstate(fan_pwm_channels[channel], &output);
            if (ret)
                break;
            *val = output;
            break;
        case hwmon_pwm_enable:
            *val = 0;
            break;
        default:
            break;
        }
    default:
        break;
    }
    return 0;
}

static const struct hwmon_channel_info *aerox16_laptop_hwmon_info[] = {
    HWMON_CHANNEL_INFO(temp,
                       HWMON_T_INPUT,
                       HWMON_T_INPUT,
                       HWMON_T_INPUT),
    HWMON_CHANNEL_INFO(fan,
                       HWMON_F_INPUT,
                       HWMON_F_INPUT,
                       HWMON_F_INPUT,
                       HWMON_F_INPUT),
    HWMON_CHANNEL_INFO(pwm,
                       HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
                       HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
    NULL
};

static const struct hwmon_ops aerox16_laptop_hwmon_ops = {
    .read = aerox16_laptop_hwmon_read,
    .is_visible = aerox16_laptop_hwmon_is_visible,
};

const struct hwmon_chip_info aerox16_laptop_chip_info = {
    .ops = &aerox16_laptop_hwmon_ops,
    .info = aerox16_laptop_hwmon_info,
};
