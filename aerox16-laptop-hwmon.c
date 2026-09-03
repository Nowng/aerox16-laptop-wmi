// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-hwmon.c - AERO X16 laptop hwmon sensor interface (Layer 1)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 *
 *  This is the layer that speaks "hwmon" to the kernel, which is how tools like
 *  ` sensors` and GNOME's hardware monitor get their numbers. It takes the raw
 *  EC values from the WMI layer and repackages them as standard hwmon sensor
 *  channels (temperatures in millidegrees C, fan RPMs, PWM duty). Keep the
 *  translation honest here - userspace trusts hwmon to give real values.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/dmi.h>

#include <linux/acpi.h>

#include "aerox16-laptop.h"

/*
 * convert_fan_rpm - reverse the byte order of a fan RPM.
 *
 * The embedded controller stores fan RPM as a big-endian 16-bit value, but x86
 * is little-endian, so the bytes come back swapped. rol16(x, 8) rotates the 16
 * bit value left by 8 bits, which swaps the two bytes and gives you the real
 * RPM. Without this, every fan reading would be garbage - literally backwards.
 */
u16 convert_fan_rpm(int val)
{
    u16 fan_rpm = val;
    return rol16(fan_rpm, 8);
}

/*
 * aerox16_laptop_hwmon_is_visible - which attributes does each channel expose?
 *
 * The kernel calls this before it tries to read an attribute, so we can decide
 * what's readable and with what permissions. Everything this driver offers is
 * read-only (0444), because we don't let hwmon tools write fan curves - that's
 * the sysfs layer's job. Returning 0 for anything else means "not exposed".
 */
umode_t aerox16_laptop_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
    switch (type) {
    case hwmon_temp:
        /* Temperatures are always readable as temp_input. */
        if (attr == hwmon_temp_input)
            return 0444;
        break;
    case hwmon_fan:
        /* Fan channels are readable as fan_input. */
        if (attr == hwmon_fan_input)
            return 0444;
        break;
    case hwmon_pwm:
        /* PWM duty and its enable flag are both readable. */
        if (attr == hwmon_pwm_input || attr == hwmon_pwm_enable)
            return 0444;
        break;
    default:
        break;
    }
    /* Anything we don't recognise is hidden from userspace. */
    return 0;
}

/*
 * aerox16_laptop_hwmon_read - the core read dispatch.
 *
 * The kernel calls this for every hwmon sensor it wants to sample. `type` tells
 * us whether it's asking for a temperature, a fan RPM, or a PWM duty; `channel`
 * picks which specific sensor. We switch on both and pull the value from the EC.
 * All readings are read-only in this driver.
 */
int aerox16_laptop_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
    int ret, output;
    u8 result;
    /* The four fan channels, in order: CPU, GPU, fan 3, fan 4. */
    u8 fan_channels[] = { FAN_CPU_RPM, FAN_GPU_RPM, FAN_THREE_RPM, FAN_FOUR_RPM };
    /* The two PWM duty channels: CPU and GPU fans. */
    u8 fan_pwm_channels[] = { FAN_PWM, GPU_FAN_DUTY };

    switch (type) {
    case hwmon_temp:
        /*
         * Three temperature sensors:
         *   0 -> CPU temp (from WMI)
         *   1 -> GPU temp (from WMI)
         *   2 -> motherboard temp. This one CAN'T be read over WMI, so we fall
         *        back to a raw EC read (ec_read 0x62). The WMI layer just doesn't
         *        expose it, which is fine - we reach around it directly.
         */
        switch (channel) {
        case 0:
            ret = aerox16_laptop_get_devstate(TEMP_CPU, &output);
            if (ret)
                break;
            /* EC reports Celsius; hwmon wants millidegrees, so * 1000. */
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
        /*
         * Endianness depends on the machine. On "GIGABYTE AERO" (the only thing
         * we support) the RPM comes back little-endian and usable directly. Any
         * other board would need convert_fan_rpm() - but since we refuse to run
         * on non-AERO hardware, this branch is effectively dead code. It's kept
         * for correctness/defence, not because it ever executes.
         */
        if (!strcmp(dmi_get_system_info(DMI_PRODUCT_FAMILY), "GIGABYTE AERO"))
            *val = output;
        else
            *val = convert_fan_rpm(output);
        break;
    case hwmon_pwm:
        switch (attr) {
        case hwmon_pwm_input:
            /* Current PWM duty for the selected fan channel. */
            ret = aerox16_laptop_get_devstate(fan_pwm_channels[channel], &output);
            if (ret)
                break;
            *val = output;
            break;
        case hwmon_pwm_enable:
            /* We don't expose writable PWM enable; report it as 0. */
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

/*
 * The channel descriptors. Each HWMON_CHANNEL_INFO() declares a group of
 * sensors (temp has 3, fan has 4, pwm has 2) and what attribute each exposes.
 * The trailing NULL terminates the list the kernel walks. This is what tells
 * hwmon "here are your channels and how many sensors each one has".
 */
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

/*
 * The ops vector: the function pointers hwmon calls back into. We provide read
 * (sample a value) and is_visible (decide what's exposed). That's all hwmon needs
 * from us for a purely-read-only sensor device.
 */
static const struct hwmon_ops aerox16_laptop_hwmon_ops = {
    .read = aerox16_laptop_hwmon_read,
    .is_visible = aerox16_laptop_hwmon_is_visible,
};

/*
 * The chip info struct that ties the ops and channels together. This is passed
 * to hwmon_device_register_with_info() during init and is what makes our sensors
 * actually appear under /sys/.../hwmonN/. One struct, one device.
 */
const struct hwmon_chip_info aerox16_laptop_chip_info = {
    .ops = &aerox16_laptop_hwmon_ops,
    .info = aerox16_laptop_hwmon_info,
};
