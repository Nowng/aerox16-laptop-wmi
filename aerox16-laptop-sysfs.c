// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-sysfs.c - AERO X16 laptop sysfs control interface
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 *
 *  This is the user-facing layer. Every function here backs a file under
 *  /sys/devices/platform/aerox16_laptop/, so `cat`-ing a node reads state and
 *  `echo`-ing to one changes it. The pattern is always the same:
 *    *_show() : read something and format it into buf (via sysfs_emit)
 *    *_store(): parse input from the user, push it to the EC, remember it
 *  Read-only nodes only implement _show(); read-write ones implement both.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>

#include "aerox16-laptop.h"

/*
 * Fan mode layout (matches fan_modes[] in probe.c):
 *   0 = normal, 1 = silent, 2 = gaming, 3 = custom, 4 = auto-maximum, 5 = fixed
 * The comments on set_fan_mode() explain how switching modes interacts with the
 * underlying EC opcodes.
 */

/*
 * disable_custom_fan_mode - turn off the "custom" mode before switching away.
 *
 * The EC won't let you switch fan modes while custom/fan-curve mode is still
 * enabled, so if we're leaving a mode >= 3 (custom/auto/fixed) we first have to
 * disable custom mode. This helper does exactly that one write. Called before we
 * enter silent/gaming/normal.
 */
static int disable_custom_fan_mode(int mode)
{
    int ret, result;

    /* Fixed (5) and auto-maximum (4) both enable custom internally, so we have
     * to turn it off for each of them explicitly. */
    if (mode == 5) {
        ret = aerox16_laptop_set_devstate(FAN_FIXED_MODE, 0, &result);
        if (ret)
            return ret;
    } else if (mode == 4) {
        ret = aerox16_laptop_set_devstate(FAN_GAMING_MODE, 0, &result);
        if (ret)
            return ret;
    }

    /* Always clear custom mode last - it's the common denominator. */
    ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 0, &result);
    if (ret)
        return ret;

    return 0;
}

/*
 * set_fan_mode - the opinionated core of fan control.
 *
 * This is where the user's clean 0-5 fan_mode number gets translated into the
 * messy reality of EC opcodes, and it's deliberately verbose because the EC has
 * weird rules:
 *   - Some modes (auto/fixed) REQUIRE custom mode to be enabled first.
 *   - You can't switch modes while custom mode is on, so we disable it on the way
 *     out of custom/auto/fixed territory.
 *   - The "auto" mode needs the saved custom speed passed as its argument; the
 *     others just get a 1 to mean "enable me".
 *
 * The nested ifs below are ugly but they're implementing those rules literally.
 * I'd rather read this than read a one-liner that silently does the wrong thing.
 */
static int set_fan_mode(struct aerox16_laptop_wmi *gigabyte, u32 fan_mode)
{
    int ret, result;

    /*
     * auto and fixed both need custom mode enabled - they're built on top of it.
     */
    if (fan_mode == FAN_FIXED_MODE || fan_mode == FAN_AUTO_MODE) {
        /*
         * If we're currently in a "base" mode (0,1,2), we may need to disable
         * whatever's currently enabled before enabling custom. fan_mode < 3 and
         * > 0 means an actual named mode is on.
         */
        if (gigabyte->fan_mode < 3) {
            if (gigabyte->fan_mode > 0) {
                ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
                if (ret)
                    return ret;
            }
            /* Enable custom mode as the base for auto/fixed. */
            ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 1, &result);
            if (ret)
                return ret;
        }
        /*
         * If we're already in custom/auto/fixed territory (fan_mode > 3), we may
         * need to disable the currently-active named mode first. Auto-maximum (4)
         * is really gaming-mode + custom; fixed (5) is fixed-mode + custom.
         */
        if (gigabyte->fan_mode > 3) {
            if (gigabyte->fan_mode == 4) {
                ret = aerox16_laptop_set_devstate(FAN_GAMING_MODE, 0, &result);
                if (ret)
                    return ret;
            } else {
                ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
                if (ret)
                    return ret;
            }
        }
        /*
         * Now actually set the target. Auto needs our saved custom speed as an
         * argument; fixed just takes a plain enable.
         */
        if (fan_mode == FAN_AUTO_MODE) {
            ret = aerox16_laptop_set_devstate(fan_mode, gigabyte->fan_custom_speed, &result);
            if (ret)
                return ret;
        } else {
            ret = aerox16_laptop_set_devstate(fan_mode, 1, &result);
            if (ret)
                return ret;
        }
    } else if (fan_mode == FAN_CUSTOM_MODE) {
        /* Switching INTO custom mode. If we're already past the base modes, custom
         * is presumably on - warn and no-op rather than fight the EC. */
        if (gigabyte->fan_mode > 3) {
            pr_warn("Custom mode is already enabled\n");
            return 0;
        } else if (gigabyte->fan_mode > 0) {
            /* Disable whatever named mode is currently on before enabling custom. */
            ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
            if (ret)
                return ret;
        }
        ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 1, &result);
        if (ret)
            return ret;
    } else {
        /*
         * Switching to a base mode (normal/silent/gaming, i.e. fan_mode 0-2).
         * If we're currently in custom/auto/fixed, we have to disable custom mode
         * first - that's what disable_custom_fan_mode() is for.
         */
        if (gigabyte->fan_mode >= 3) {
            ret = disable_custom_fan_mode(gigabyte->fan_mode);
            if (ret)
                return ret;
        } else if (gigabyte->fan_mode > 0) {
            /* Disable the currently-enabled named mode. */
            ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
            if (ret)
                return ret;
        }
        /* Finally enable the requested base mode (with 1 = "enable"). Skip the
         * write entirely for normal mode (0), which is the default/off state. */
        if (fan_mode != 0) {
            ret = aerox16_laptop_set_devstate(fan_mode, 1, &result);
            if (ret)
                return ret;
        }
    }
    return 0;
}

/* fan_mode_show - report the current fan mode. */
static ssize_t fan_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->fan_mode);
}

/*
 * fan_mode_store - let userspace pick a fan mode (0-5).
 *
 * Parse the input, reject garbage, refuse no-op writes (why rewrite the same
 * value?), then hand off to set_fan_mode() and cache the result.
 */
static ssize_t fan_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    unsigned int fan_mode = 0;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &fan_mode);
    if (ret) {
        pr_err("kstrtouint failed\n");
        return count;
    }
    gigabyte = dev_get_drvdata(dev);
    /* No point writing the mode we already have - just warn and bail. */
    if (gigabyte->fan_mode == fan_mode) {
        pr_warn("Already set to that fan mode\n");
        return count;
    }
    /* Only 0-5 are valid. Beyond that, it's a typo, not a feature. */
    if (fan_mode > 5) {
        pr_err("Invalid fan mode\n");
        return -EINVAL;
    }
    ret = set_fan_mode(gigabyte, fan_modes[fan_mode]);
    if (ret)
        return ret;
    gigabyte->fan_mode = fan_mode;
    return count;
}

/* fan_custom_speed_show - report the saved custom/fan-curve speed. */
static ssize_t fan_custom_speed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->fan_custom_speed);
}

/*
 * fan_custom_speed_store - set the custom fan speed (0-255).
 *
 * This is the duty value used in custom/auto/fixed modes. If this board has a
 * second fan (dual_fan_speed_enabled), we push the same duty to the GPU fan too.
 */
static ssize_t fan_custom_speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int speed;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &speed);
    if (ret)
        return ret;
    /* Tell the EC the new custom speed. */
    ret = aerox16_laptop_set_devstate(FAN_CUSTOM_SPEED, speed, &output);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    /* If there's a second fan, mirror the duty onto it as well. */
    if (gigabyte->dual_fan_speed_enabled) {
        ret = aerox16_laptop_set_devstate(GPU_FAN_DUTY, speed, &output);
        if (ret)
            return ret;
    }
    gigabyte->fan_custom_speed = speed;
    return count;
}

/* fan_pwm_show - report the current CPU-fan PWM duty. */
static ssize_t fan_pwm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(FAN_PWM, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

/* charge_mode_show - report the charging mode (0 normal / 1 custom). */
static ssize_t charge_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->charge_mode);
}

/*
 * charge_mode_store - choose normal or custom charging.
 *
 * The EC wants the mode shifted left by 2 (the low bits are reserved), so we
 * write `mode << 2`. Then cache it.
 */
static ssize_t charge_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int mode;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &mode);
    if (ret)
        return ret;
    if (mode > 1) {
        pr_err("Invalid charge mode\n");
        return -EINVAL;
    }
    ret = aerox16_laptop_set_devstate(CHARGING_MODE, mode << 2, &output);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    gigabyte->charge_mode = mode;
    return count;
}

/* charge_limit_show - report the charge-ceiling percentage. */
static ssize_t charge_limit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->charge_limit);
}

/*
 * charge_limit_store - set the charge ceiling (60-100%).
 *
 * The EC won't accept a limit below 60% (that's their "protect the battery"
 * floor), so anything outside 60-100 is rejected outright.
 */
static ssize_t charge_limit_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int limit;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &limit);
    if (ret)
        return ret;
    if (limit > 100 || limit < 60) {
        pr_err("Invalid charge limit\n");
        return -EINVAL;
    }
    ret = aerox16_laptop_set_devstate(CHARGING_LIMIT, limit, &output);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    gigabyte->charge_limit = limit;
    return count;
}

/* gpu_boost_show - report the GPU boost state. */
static ssize_t gpu_boost_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->gpu_boost);
}

/*
 * gpu_boost_store - set the GPU boost mode (0-3).
 *
 * 1 enables performance boost; higher values are more aggressive. >3 is garbage.
 */
static ssize_t gpu_boost_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int mode;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &mode);
    if (ret)
        return ret;
    if (mode > 3) {
        pr_err("Invalid boost mode\n");
        return -EINVAL;
    }
    ret = aerox16_laptop_set_devstate(GPU_QBOOST, mode, &output);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    gigabyte->gpu_boost = mode;
    return count;
}

/* fan_curve_index_show - report which fan-curve point is "selected". */
static ssize_t fan_curve_index_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%d\n", gigabyte->fan_curve_index);
}

/*
 * fan_curve_index_store - pick which of the 15 fan-curve points to edit.
 *
 * This doesn't change anything hardware-wise; it just selects an index so the
 * next fan_curve_data write lands on the right point. Out-of-range is rejected.
 */
static ssize_t fan_curve_index_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    unsigned int index;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtouint(buf, 0, &index);
    if (ret)
        return ret;
    if (index >= FAN_CURVE_POINTS) {
        pr_err("Invalid fan curve index\n");
        return -EINVAL;
    }
    gigabyte = dev_get_drvdata(dev);
    gigabyte->fan_curve_index = index;
    return count;
}

/* fan_curve_data_show - report the (temperature, speed) pair at the selected index. */
static ssize_t fan_curve_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    int index = gigabyte->fan_curve_index;
    return sysfs_emit(buf, "%d %d\n", gigabyte->fan_curve.temperature[index], gigabyte->fan_curve.speed[index]);
}

/*
 * fan_curve_data_store - write a (temperature, speed) pair for the selected point.
 *
 * The user sends a single number; the EC wants it packed as (speed << 8 |
 * temperature). So we split the incoming value: the low byte is temperature, the
 * high byte is speed. (The README's "(fan_speed * 256) + temperature" formula is
 * exactly this packing.) We remember both halves back in the curve table.
 */
static ssize_t fan_curve_data_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    u16 data;
    u32 payload;
    struct aerox16_laptop_wmi *gigabyte;

    ret = kstrtou16(buf, 0, &data);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    /* Rebuild the packed value: speed in the high byte, index already selected. */
    payload = data << 8 | gigabyte->fan_curve_index;
    ret = aerox16_laptop_set_devstate(FAN_INDEX_VALUE, payload, &output);
    if (ret)
        return ret;
    gigabyte->fan_curve.temperature[gigabyte->fan_curve_index] = data;
    gigabyte->fan_curve.speed[gigabyte->fan_curve_index] = data >> 8;
    return count;
}

/* battery_cycle_show - report the max of the two battery cycle-count reads. */
static ssize_t battery_cycle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, cyc1, cyc2;
    ret = aerox16_laptop_get_devstate(BATT_CYCLE, &cyc1);
    if (ret)
        return ret;
    ret = aerox16_laptop_get_devstate(BATT_CYCLE2, &cyc2);
    if (ret)
        return ret;
    /* The cycle count is split across two opcodes; report the larger. */
    return sysfs_emit(buf, "%d\n", max(cyc1, cyc2));
}

/*
 * light_sensor_show - report ambient light.
 *
 * Which read path we take depends on what probe() decided about this board: the
 * "new" firmware returns a 4-byte buffer, the "old" one a single int. We branch
 * on the method probe() cached earlier.
 */
static ssize_t light_sensor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output_old;
    u8 output[4];
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);

    if (gigabyte->light_sensor_method == LIGHT_SENSOR_NEW)
        ret = aerox16_laptop_get_devstate(gigabyte->light_sensor_method, &output);
    else
        ret = aerox16_laptop_get_devstate(gigabyte->light_sensor_method, &output_old);
    if (ret)
        return ret;
    if (gigabyte->light_sensor_method == LIGHT_SENSOR_NEW)
        return sysfs_emit(buf, "%d %d %d %d\n", output[0], output[1], output[2], output[3]);
    return sysfs_emit(buf, "%d\n", output_old);
}

/* power_on_time_show - report the 5-byte power-on time breakdown. */
static ssize_t power_on_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret;
    u8 output[5];
    ret = aerox16_laptop_get_devstate(POWER_ON_TIME, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d %d %d %d %d\n", output[0], output[1], output[2], output[3], output[4]);
}

/*
 * debug_method_store - set the target opcode for the debug_method node.
 *
 * This is a general-purpose "query whatever opcode I point it at" tool. It just
 * remembers the chosen opcode; debug_method_show() then reads that opcode. Handy
 * for poking at arbitrary EC values during development.
 */
static ssize_t debug_method_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    u8 data;
    struct aerox16_laptop_wmi *gigabyte;
    ret = kstrtou8(buf, 0, &data);
    if (ret)
        return ret;
    gigabyte = dev_get_drvdata(dev);
    gigabyte->debug_method = data;
    return count;
}

/* debug_method_show - read whatever opcode debug_method currently points at. */
static ssize_t debug_method_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
    ret = aerox16_laptop_get_devstate(gigabyte->debug_method, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d, %d\n", gigabyte->debug_method, output);
}

/*
 * Newly added controls from DSDT.
 *
 * Everything below is keyboard-backlight, LED, Fn-Lock and mute control - all
 * simple get/set pairs that follow the same boring, correct pattern: parse,
 * bounds-check, push to the EC, done. They're short because there's genuinely no
 * complexity here; the interesting stuff is all up in the fan/charge logic.
 */

static ssize_t keyboard_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(KBD_BACKLIGHT, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t keyboard_brightness_store(struct device *dev, struct device_attribute *attr, const char *buf,
                                         size_t count)
{
    int ret, output;
    unsigned int level;
    ret = kstrtouint(buf, 0, &level);
    if (ret)
        return ret;
    if (level > 255)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(KBD_BACKLIGHT, level, &output);
    if (ret)
        return ret;
    return count;
}

static ssize_t keyboard_auto_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(KBD_AUTO, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t keyboard_auto_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int val;
    ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val > 1)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(KBD_AUTO, val, &output);
    if (ret)
        return ret;
    return count;
}

static ssize_t power_led_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(POWER_LED, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t power_led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int val;
    ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val > 1)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(POWER_LED, val, &output);
    if (ret)
        return ret;
    return count;
}

static ssize_t battery_led_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(BATTERY_LED, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t battery_led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int val;
    ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val > 1)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(BATTERY_LED, val, &output);
    if (ret)
        return ret;
    return count;
}

static ssize_t fn_lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(FN_LOCK, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t fn_lock_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int val;
    ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val > 1)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(FN_LOCK, val, &output);
    if (ret)
        return ret;
    return count;
}

static ssize_t mute_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret, output;
    ret = aerox16_laptop_get_devstate(MUTE_STATUS, &output);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d\n", output);
}

static ssize_t mute_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret, output;
    unsigned int val;
    ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val > 1)
        return -EINVAL;
    ret = aerox16_laptop_set_devstate(MUTE_STATUS, val, &output);
    if (ret)
        return ret;
    return count;
}

/*
 * Attribute table. Each DEVICE_ATTR_RW builds a read-write node, each
 * DEVICE_ATTR_RO a read-only one, and TOGGLE_DEVICE a read-only boolean toggle.
 * The array below is what actually shows up in sysfs - the kernel walks it to
 * build the directory listing. Order here is the order they appear.
 */
static DEVICE_ATTR_RW(fan_mode);
static DEVICE_ATTR_RW(fan_custom_speed);
static DEVICE_ATTR_RO(fan_pwm);
static DEVICE_ATTR_RW(charge_mode);
static DEVICE_ATTR_RW(charge_limit);
static DEVICE_ATTR_RW(gpu_boost);
static DEVICE_ATTR_RW(fan_curve_index);
static DEVICE_ATTR_RW(fan_curve_data);
static DEVICE_ATTR_RO(battery_cycle);
static DEVICE_ATTR_RO(light_sensor);
static DEVICE_ATTR_RO(power_on_time);
static DEVICE_ATTR_RW(debug_method);
static DEVICE_ATTR_RW(keyboard_brightness);
static DEVICE_ATTR_RW(keyboard_auto);
static DEVICE_ATTR_RW(power_led);
static DEVICE_ATTR_RW(battery_led);
static DEVICE_ATTR_RW(fn_lock);
static DEVICE_ATTR_RW(mute);
TOGGLE_DEVICE(usb_charge_s3, USB_SLEEP);
TOGGLE_DEVICE(usb_charge_s4, USB_HIBERNATE);

static struct attribute *aerox16_laptop_attributes[] = {
    &dev_attr_fan_mode.attr,
    &dev_attr_fan_custom_speed.attr,
    &dev_attr_fan_pwm.attr,
    &dev_attr_charge_mode.attr,
    &dev_attr_charge_limit.attr,
    &dev_attr_usb_charge_s3_toggle.attr,
    &dev_attr_usb_charge_s4_toggle.attr,
    &dev_attr_gpu_boost.attr,
    &dev_attr_fan_curve_index.attr,
    &dev_attr_fan_curve_data.attr,
    &dev_attr_battery_cycle.attr,
    &dev_attr_light_sensor.attr,
    &dev_attr_power_on_time.attr,
    &dev_attr_debug_method.attr,
    &dev_attr_keyboard_brightness.attr,
    &dev_attr_keyboard_auto.attr,
    &dev_attr_power_led.attr,
    &dev_attr_battery_led.attr,
    &dev_attr_fn_lock.attr,
    &dev_attr_mute.attr,
    NULL
};

/*
 * The attribute group. This is the struct passed to sysfs_create_group() during
 * init - it's what turns the raw attribute array into a real directory in
 * /sys/devices/platform/. One group, one device, nothing fancy.
 */
const struct attribute_group aerox16_laptop_attr_group = {
    .attrs = aerox16_laptop_attributes,
};
