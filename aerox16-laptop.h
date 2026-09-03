// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop.h - AERO X16 laptop WMI driver (shared definitions)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 */

#ifndef AEROX16_LAPTOP_H
#define AEROX16_LAPTOP_H

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/wmi.h>

#define GIGABYTE_LAPTOP_VERSION "0.02"
#define AEROX16_LAPTOP_FILE  KBUILD_MODNAME

/* ACPI WMI GUIDs (_SB_.PCI0.AMW0._WDG) */
#define WMI_EVENT "ABBC0F72-8EA1-11D1-00A0-C90629100000"
#define WMI_METHOD_WMBC "ABBC0F6F-8EA1-11D1-00A0-C90629100000"
#define WMI_METHOD_WMBD "ABBC0F75-8EA1-11D1-00A0-C90629100000"

/* WMI method arguments (EC opcodes) */
#define GPU_QBOOST       0x51
#define FAN_SILENT_MODE  0x57
#define POWER_ON_TIME    0x63
#define CHARGING_MODE    0x64
#define CHARGING_LIMIT   0x65
#define FAN_PWM          0x50
#define FAN_CUSTOM_MODE  0x67
#define FAN_INDEX_VALUE  0x68
#define FAN_FIXED_MODE   0x6A
#define FAN_CUSTOM_SPEED 0x6B
#define BATT_CYCLE2      0x6D
#define BATT_CYCLE       0x6E
#define FAN_AUTO_MODE    0x70
#define FAN_GAMING_MODE  0x71
#define USB_SLEEP        0x7A
#define USB_HIBERNATE    0x7B
#define WIFI_TOGGLE      0xC2
#define TOUCHPAD_ENABLED 0xCA
#define TEMP_CPU         0xE1
#define TEMP_GPU         0xE2
#define FAN_CPU_RPM      0xE4
#define FAN_GPU_RPM      0xE5
#define FAN_THREE_RPM    0xE8
#define FAN_FOUR_RPM     0xE9
#define LIGHT_SENSOR     0xF7
#define FAN_SILENT_OLD   0xFA
#define LIGHT_SENSOR_NEW 0xFC

#define CPU_FAN_DUTY     0x46
#define GPU_FAN_DUTY     0x47

/* AERO X16 DSDT-confirmed controls */
#define KBD_BACKLIGHT    0xF6	/* KBLL - keyboard backlight brightness */
#define KBD_AUTO         0xD9	/* KBAT - keyboard auto enable bit */
#define POWER_LED        0x87	/* PLED - power LED */
#define BATTERY_LED      0x88	/* BLED - battery LED */
#define FN_LOCK          0xC9	/* FNKS - Fn Lock */
#define MUTE_STATUS      0xC7	/* MUTE - mute state */

#define FAN_CURVE_POINTS 15

struct fan_curve_data {
	u8 temperature[FAN_CURVE_POINTS];
	u8 speed[FAN_CURVE_POINTS];
};

struct aerox16_laptop_wmi {
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct fan_curve_data fan_curve;

	int fan_mode;
	int fan_custom_speed;
	int charge_mode;
	int charge_limit;
	int gpu_boost;
	int fan_curve_index;

	u8 fan_silent_method;
	u8 debug_method;
	u8 dual_fan_speed_enabled;
	u8 light_sensor_method;
};

extern struct platform_driver platform_driver;
extern const struct attribute_group aerox16_laptop_attr_group;
extern const struct hwmon_chip_info aerox16_laptop_chip_info;
extern u8 fan_modes[];

#define DMI_EXACT_MATCH_GIGABYTE_LAPTOP_FAMILY(name) \
	{ .matches = { \
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "GIGABYTE"), \
		DMI_EXACT_MATCH(DMI_PRODUCT_FAMILY, name) \
	} }

#define DMI_EXACT_MATCH_GIGABYTE_LEGACY_DEVICE(name) \
	{ .matches = { \
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "GIGABYTE"), \
		DMI_EXACT_MATCH(DMI_PRODUCT_NAME, name) \
	} }

#define TOGGLE_DEVICE(_device, _id) \
static ssize_t _device##_toggle_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	int ret, output; \
	ret = aerox16_laptop_get_devstate(_id, &output); \
	if (ret) \
		return ret; \
	return sysfs_emit(buf, "%d\n", output); \
} \
static DEVICE_ATTR_RO(_device##_toggle);

int aerox16_laptop_get_devstate2(u32 method_id, u32 arg2, void *result);
int aerox16_laptop_get_devstate(u32 method_id, void *result);
int aerox16_laptop_set_devstate(u32 method_id, u32 arg2, int *result);

u16 convert_fan_rpm(int val);
umode_t aerox16_laptop_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel);
int aerox16_laptop_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val);

int aerox16_laptop_probe(struct device *dev);
void aerox16_laptop_exit(void);
int aerox16_laptop_init(void);

#endif /* AEROX16_LAPTOP_H */