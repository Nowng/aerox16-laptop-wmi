// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-sysfs.c - AERO X16 laptop sysfs control interface
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>

#include "aerox16-laptop.h"

/*
 * Fan mode.
 * 0 = normal, 1 = silent, 2 = gaming, 3 = custom,
 * 4 = auto-maximum, 5 = fixed speed
 */
static int disable_custom_fan_mode(int mode)
{
	int ret, result;

	if (mode == 5) {
		ret = aerox16_laptop_set_devstate(FAN_FIXED_MODE, 0, &result);
		if (ret)
			return ret;
	} else if (mode == 4) {
		ret = aerox16_laptop_set_devstate(FAN_GAMING_MODE, 0, &result);
		if (ret)
			return ret;
	}

	ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 0, &result);
	if (ret)
		return ret;

	return 0;
}

static int set_fan_mode(struct aerox16_laptop_wmi *gigabyte, u32 fan_mode)
{
	int ret, result;

	if (fan_mode == FAN_FIXED_MODE || fan_mode == FAN_AUTO_MODE) {
		if (gigabyte->fan_mode < 3) {
			if (gigabyte->fan_mode > 0) {
				ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
				if (ret)
					return ret;
			}
			ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 1, &result);
			if (ret)
				return ret;
		}
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
		if (gigabyte->fan_mode > 3) {
			pr_warn("Custom mode is already enabled\n");
			return 0;
		} else if (gigabyte->fan_mode > 0) {
			ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
			if (ret)
				return ret;
		}
		ret = aerox16_laptop_set_devstate(FAN_CUSTOM_MODE, 1, &result);
		if (ret)
			return ret;
	} else {
		if (gigabyte->fan_mode >= 3) {
			ret = disable_custom_fan_mode(gigabyte->fan_mode);
			if (ret)
				return ret;
		} else if (gigabyte->fan_mode > 0) {
			ret = aerox16_laptop_set_devstate(fan_modes[gigabyte->fan_mode], 0, &result);
			if (ret)
				return ret;
		}
		if (fan_mode != 0) {
			ret = aerox16_laptop_set_devstate(fan_mode, 1, &result);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static ssize_t fan_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->fan_mode);
}

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
	if (gigabyte->fan_mode == fan_mode) {
		pr_warn("Already set to that fan mode\n");
		return count;
	}
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

static ssize_t fan_custom_speed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->fan_custom_speed);
}

static ssize_t fan_custom_speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, output;
	unsigned int speed;
	struct aerox16_laptop_wmi *gigabyte;

	ret = kstrtouint(buf, 0, &speed);
	if (ret)
		return ret;
	ret = aerox16_laptop_set_devstate(FAN_CUSTOM_SPEED, speed, &output);
	if (ret)
		return ret;
	gigabyte = dev_get_drvdata(dev);
	if (gigabyte->dual_fan_speed_enabled) {
		ret = aerox16_laptop_set_devstate(GPU_FAN_DUTY, speed, &output);
		if (ret)
			return ret;
	}
	gigabyte->fan_custom_speed = speed;
	return count;
}

static ssize_t fan_pwm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret, output;
	ret = aerox16_laptop_get_devstate(FAN_PWM, &output);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", output);
}

static ssize_t charge_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->charge_mode);
}

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

static ssize_t charge_limit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->charge_limit);
}

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

static ssize_t gpu_boost_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->gpu_boost);
}

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

static ssize_t fan_curve_index_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", gigabyte->fan_curve_index);
}

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

static ssize_t fan_curve_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	int index = gigabyte->fan_curve_index;
	return sysfs_emit(buf, "%d %d\n",
			  gigabyte->fan_curve.temperature[index],
			  gigabyte->fan_curve.speed[index]);
}

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
	payload = data << 8 | gigabyte->fan_curve_index;
	ret = aerox16_laptop_set_devstate(FAN_INDEX_VALUE, payload, &output);
	if (ret)
		return ret;
	gigabyte->fan_curve.temperature[gigabyte->fan_curve_index] = data;
	gigabyte->fan_curve.speed[gigabyte->fan_curve_index] = data >> 8;
	return count;
}

static ssize_t battery_cycle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret, cyc1, cyc2;
	ret = aerox16_laptop_get_devstate(BATT_CYCLE, &cyc1);
	if (ret)
		return ret;
	ret = aerox16_laptop_get_devstate(BATT_CYCLE2, &cyc2);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", max(cyc1, cyc2));
}

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

static ssize_t power_on_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	u8 output[5];
	ret = aerox16_laptop_get_devstate(POWER_ON_TIME, &output);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d %d %d %d %d\n",
			  output[0], output[1], output[2], output[3], output[4]);
}

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

static ssize_t debug_method_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret, output;
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);
	ret = aerox16_laptop_get_devstate(gigabyte->debug_method, &output);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d, %d\n", gigabyte->debug_method, output);
}

/* Newly added controls from DSDT */

static ssize_t keyboard_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret, output;
	ret = aerox16_laptop_get_devstate(KBD_BACKLIGHT, &output);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", output);
}

static ssize_t keyboard_brightness_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
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

const struct attribute_group aerox16_laptop_attr_group = {
	.attrs = aerox16_laptop_attributes,
};