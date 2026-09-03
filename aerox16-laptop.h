// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop.h - AERO X16 laptop WMI driver (shared definitions)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 *
 *  This is the header that ties the whole thing together. Every other .c file
 *  in this driver includes it, so anything you touch here ripples out to all
 *  of them. That means you have to be careful and un-precious at the same time:
 *  change signatures or layout thoughtfully, but don't get emotionally attached
 *  to declarations. This file exists to serve the drivers, not the reverse.
 */

#ifndef AEROX16_LAPTOP_H
#define AEROX16_LAPTOP_H

/*
 * Standard Linux kernel headers. We're a plain platform + WMI driver, so the
 * list is boring and predictable:
 *   - acpi.h    : talk to the ACPI/WMI layer that actually reaches the EC
 *   - dmi.h     : figure out which laptop we're running on (we refuse to run
 *                 anywhere that isn't a Gigabyte AERO)
 *   - hwmon.h / hwmon-sysfs.h : expose temperature/fan/pwm readings to userspace
 *   - platform_device.h : the driver-model glue
 *   - slab.h    : kmalloc/kfree, memory allocation
 *   - sysfs.h   : creating sysfs attributes
 *   - module.h  : this is a loadable kernel module, deal with it
 *   - wmi.h     : the generic WMI plumbing from the kernel
 */
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

/* Driver version. It's 0.02 because that's literally what it is. Don't lie
 * to userspace about how polished this is. */
#define GIGABYTE_LAPTOP_VERSION "0.02"
/* Convenience alias: the module name, used as the sysfs device name too. */
#define AEROX16_LAPTOP_FILE  KBUILD_MODNAME

/*
 * ACPI WMI GUIDs (_SB_.PCI0.AMW0._WDG).
 *
 * The embedded controller (EC) hides a bunch of methods behind an ACPI device
 * called "WMI Device Group". Each method is identified by a 128-bit GUID.
 * These are the three this driver actually uses:
 *
 *   WMI_EVENT    - the standard Microsoft generic WMI event GUID. You don't
 *                  call it directly; it's how the EC pushes notifications to
 *                  us. Leave it alone.
 *   WMI_METHOD_WMBC - Gigabyte's "read me a value" method. You hand it a
 *                  method_id (one of the EC opcodes below) and it returns
 *                  whatever the EC felt like giving. See get_devstate2().
 *   WMI_METHOD_WMBD - Gigabyte's "take this value, EC" method. The write
 *                  direction. WMBC reads, WMBD writes. That's basically the
 *                  entire protocol.
 *
 * Those GUIDs look intimidating but they're just opaque identifiers baked into
 * the laptop's firmware. The ugly truth: if Gigabyte's BIOS/EC changes a GUID,
 * this driver silently stops working and you'll waste an afternoon debugging
 * "why doesn't anything respond" when the real answer is "the target moved".
 */
#define WMI_EVENT "ABBC0F72-8EA1-11D1-00A0-C90629100000"
#define WMI_METHOD_WMBC "ABBC0F6F-8EA1-11D1-00A0-C90629100000"
#define WMI_METHOD_WMBD "ABBC0F75-8EA1-11D1-00A0-C90629100000"

/*
 * WMI method arguments (a.k.a. EC opcodes).
 *
 * When you call WMBC/WMBD you don't pass a friendly name - you pass one of
 * these numeric opcodes as the "method_id". Think of this table as the shared
 * vocabulary between this driver and the firmware. If you add a new control,
 * add its opcode here first, or you'll be chasing a magic number around the
 * codebase like a man chasing his own tail.
 *
 * The values are whatever the DSDT says they are; they look random because the
 * firmware author had reasons of their own that we're not privy to.
 */
#define GPU_QBOOST       0x51      /* Force GPU boost on/off              */
#define FAN_SILENT_MODE  0x57      /* Silent (passive) fan mode           */
#define POWER_ON_TIME    0x63      /* How long the system has been on     */
#define CHARGING_MODE    0x64      /* Normal vs custom charging           */
#define CHARGING_LIMIT   0x65      /* Charge ceiling (e.g. 60/80/100%)    */
#define FAN_PWM          0x50      /* Current CPU-fan PWM duty            */
#define FAN_CUSTOM_MODE  0x67      /* Enable the user-defined fan curve    */
#define FAN_INDEX_VALUE  0x68      /* (temp,speed) pair at a curve index   */
#define FAN_FIXED_MODE   0x6A      /* Fixed-speed fan mode                 */
#define FAN_CUSTOM_SPEED 0x6B      /* The RPM-ish duty used in custom mode */
#define BATT_CYCLE2      0x6D      /* Battery cycle count, high word       */
#define BATT_CYCLE       0x6E      /* Battery cycle count, low word        */
#define FAN_AUTO_MODE    0x70      /* Auto fan mode (curve-based)          */
#define FAN_GAMING_MODE  0x71      /* Gaming fan mode                      */
#define USB_SLEEP        0x7A      /* USB S3 (sleep) power output status   */
#define USB_HIBERNATE    0x7B      /* USB S4 (hibernate) power output      */
#define WIFI_TOGGLE      0xC2      /* WiFi radio on/off                    */
#define TOUCHPAD_ENABLED 0xCA      /* Touchpad enabled/disabled            */
#define TEMP_CPU         0xE1      /* CPU temperature                      */
#define TEMP_GPU         0xE2      /* GPU temperature                      */
#define FAN_CPU_RPM      0xE4      /* CPU fan RPM                          */
#define FAN_GPU_RPM      0xE5      /* GPU fan RPM                          */
#define FAN_THREE_RPM    0xE8      /* Third fan RPM (if any)               */
#define FAN_FOUR_RPM     0xE9      /* Fourth fan RPM (if any)              */
#define LIGHT_SENSOR     0xF7      /* Ambient light sensor (old single int)*/
#define FAN_SILENT_OLD   0xFA      /* Legacy silent-mode probe             */
#define LIGHT_SENSOR_NEW 0xFC      /* Ambient light sensor (new 4-byte buf)*/

/* These two are the duty-cycle reads for the CPU and GPU fans specifically. */
#define CPU_FAN_DUTY     0x46
#define GPU_FAN_DUTY     0x47

/*
 * Controls confirmed to exist in the AERO X16's DSDT (the ACPI description).
 * "Confirmed" is doing a lot of work here: it means we saw them in the firmware
 * tables, which is about the best guarantee you can get from an EC. Each one is
 * an opcode into the same WMI method, distinguished by this value.
 */
#define KBD_BACKLIGHT    0xF6	/* KBLL - keyboard backlight brightness   */
#define KBD_AUTO         0xD9	/* KBAT - keyboard auto-brightness enable */
#define POWER_LED        0x87	/* PLED - power LED                       */
#define BATTERY_LED      0x88	/* BLED - battery LED                     */
#define FN_LOCK          0xC9	/* FNKS - Fn Lock                        */
#define MUTE_STATUS      0xC7	/* MUTE - mute state                      */

/* Number of points in the user-programmable fan curve. Hard-coded at 15. */
#define FAN_CURVE_POINTS 15

/*
 * A single point in the fan curve: a temperature (Celsius) and the fan speed
 * to use at/above that temperature. Two u8 arrays, parallel, no null
 * terminators, no mercy - just index them together or suffer the consequences.
 */
struct fan_curve_data {
	u8 temperature[FAN_CURVE_POINTS];
	u8 speed[FAN_CURVE_POINTS];
};

/*
 * The per-device private state. One of these lives in the driver-model
 * drvdata slot for the whole lifetime of the module. Everything the sysfs and
 * hwmon code touches goes through this struct, so it's the single source of
 * truth for "what is the laptop doing right now". Keep it honest.
 */
struct aerox16_laptop_wmi {
	struct platform_device *pdev;   /* our platform device, for cleanup      */
	struct device *hwmon_dev;       /* hwmon device, for cleanup             */
	struct fan_curve_data fan_curve; /* cached fan curve table                */

	int fan_mode;              /* current fan mode (0-5)                */
	int fan_custom_speed;      /* duty saved when in custom/fixed mode   */
	int charge_mode;           /* 0 = normal, 1 = custom charging       */
	int charge_limit;          /* charge ceiling percentage             */
	int gpu_boost;             /* GPU boost state                       */
	int fan_curve_index;       /* which fan-curve point is "selected"   */

	u8 fan_silent_method;      /* which opcode means "silent" on this board */
	u8 debug_method;           /* debug_method node's current target    */
	u8 dual_fan_speed_enabled; /* 1 if this board has a second fan read  */
	u8 light_sensor_method;    /* which of the two light-sensor opcodes */
};

/*
 * Externs for things defined in other .c files. These are the seams between
 * our compilation units. Declaring them here means every file can link against
 * the others without re-declaring them (and without tripping over a mismatched
 * signature, which is how you get subtle breakage at 2am).
 */
extern struct platform_driver platform_driver;
extern const struct attribute_group aerox16_laptop_attr_group;
extern const struct hwmon_chip_info aerox16_laptop_chip_info;
extern u8 fan_modes[];

/*
 * Two DMI-matching helpers. DMI (Desktop Management Interface) tells us which
 * physical machine we're on, and we use it to refuse to run on anything that
 * isn't a Gigabyte AERO. Blunt, but this driver is hardware-specific - shipping
 * it onto the wrong laptop would just corrupt some stranger's EC. Better to be
 * pedantic about it.
 *
 *   FAMILY  : match on DMI_PRODUCT_FAMILY ("GIGABYTE AERO")
 *   LEGACY  : match on DMI_PRODUCT_NAME (older-style matching)
 */
#define DMI_EXACT_MATCH_GIGABYTE_LAPTOP_FAMILY(name) \
	{ .matches = { \
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "GIGABYTE"), \
		DMI_EXACT_MATCH(DMI_PRODUCT_FAMILY, name) \
	} }

#define DMI_EXACT_MATCH_GIGABYTE_LAPTOP_LEGACY_DEVICE(name) \
	{ .matches = { \
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "GIGABYTE"), \
		DMI_EXACT_MATCH(DMI_PRODUCT_NAME, name) \
	} }

/*
 * TOGGLE_DEVICE: a macro that generates a read-only sysfs attribute which just
 * reports whether some EC boolean (USB S3/S4 power, etc.) is set. It writes a
 * show() function and a DEVICE_ATTR_RO for you so we don't have to hand-type
 * the same boring boilerplate forty times. Macros are here to save us from our
 * own tedium - use them where they make life simpler, abuse them where they
 * make it harder. This one's fine.
 */
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

/*
 * The three functions that actually talk to the EC. These are declared here and
 * defined in aerox16-laptop-wmi.c. Everything else in the driver is just a
 * thin, opinionated wrapper on top of these three:
 *   get_devstate2  - read a value (with an optional 2nd arg)
 *   get_devstate   - read a value, no second arg (wraps get_devstate2)
 *   set_devstate   - write a value back
 */
int aerox16_laptop_get_devstate2(u32 method_id, u32 arg2, void *result);
int aerox16_laptop_get_devstate(u32 method_id, void *result);
int aerox16_laptop_set_devstate(u32 method_id, u32 arg2, int *result);

/* Reverse the byte order of a fan-RPM value (EC is big-endian, x86 is little). */
u16 convert_fan_rpm(int val);

/* hwmon callbacks, defined in aerox16-laptop-hwmon.c. */
umode_t aerox16_laptop_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel);
int aerox16_laptop_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val);

/* Driver lifecycle + probe, defined in aerox16-laptop-probe.c. */
int aerox16_laptop_probe(struct device *dev);
void aerox16_laptop_exit(void);
int aerox16_laptop_init(void);

#endif /* AEROX16_LAPTOP_H */
