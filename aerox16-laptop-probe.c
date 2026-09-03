// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-probe.c - AERO X16 laptop probe + driver lifecycle
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 *
 *  This file owns two jobs:
 *    1. The driver lifecycle - module_init/exit, registering the platform
 *       driver/device, and unwinding everything cleanly on the way out.
 *    2. The probe() function, which discovers the laptop's current EC state so
 *       the sysfs/hwmon layers can report something truthful instead of zeros.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "aerox16-laptop.h"

/* Module metadata. The kernel reads these when it loads the .ko. */
MODULE_AUTHOR("Hoonowng");
MODULE_DESCRIPTION("Gigabyte laptop AERO X16 WMI driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(GIGABYTE_LAPTOP_VERSION);

/*
 * The list of platforms we're willing to run on. Right now that's just
 * "GIGABYTE AERO" (matched by product family). It's intentionally tiny: this
 * driver is hardware-specific, and running it on an unsupported laptop could
 * send garbage to that laptop's EC. An empty-but-correct list is safer than a
 * big-and-hopeful one.
 */
static const struct dmi_system_id aerox16_laptop_known_working_platforms[] = {
    DMI_EXACT_MATCH_GIGABYTE_LAPTOP_FAMILY("GIGABYTE AERO"),
    {}
};

/*
 * The platform device handle. It's a global (not static) because exit() needs
 * to reach it, and passing it around would be busywork. Globals are usually a
 * smell; here it's a pragmatic trade-off in a tiny single-device driver. Just
 * be aware that it's shared state.
 */
struct platform_device *platform_device;

/*
 * The platform driver struct. This is what the kernel matches against when it
 * looks for something to bind to our device. .name is the module/sysfs name,
 * .owner is this module (so modprobe won't unload us while in use).
 */
struct platform_driver platform_driver = {
    .driver = {
               .name = AEROX16_LAPTOP_FILE,
               .owner = THIS_MODULE,
               },
};

/*
 * The fan mode lookup table. Index into this with the fan_mode numbers 0-5:
 *   0 -> 0x00      (a "no-op" opcode; mode 0 means "normal", handled specially)
 *   1 -> FAN_SILENT_MODE
 *   2 -> FAN_GAMING_MODE
 *   3 -> FAN_CUSTOM_MODE
 *   4 -> FAN_AUTO_MODE
 *   5 -> FAN_FIXED_MODE
 * Note fan_modes[1] gets overwritten during probe (see below) because the silent
 * opcode differs between hardware revisions. Don't be surprised when it's not
 * exactly 0x57 after probe runs.
 */
u8 fan_modes[] = {
    0,
    FAN_SILENT_MODE,
    FAN_GAMING_MODE,
    FAN_CUSTOM_MODE,
    FAN_AUTO_MODE,
    FAN_FIXED_MODE
};

/*
 * aerox16_laptop_probe - figure out the laptop's current EC state.
 *
 * The kernel calls this after binding our driver to the device. Its whole job is
 * to ask the EC "what are you set to right now?" for each thing we care about,
 * and stash the answers in the wmi struct so the sysfs layer can report them
 * without re-asking on every read. It's a discovery pass, not a mutation pass -
 * it reads, it doesn't write.
 *
 * The fan-mode detection is the gnarly bit: there's no single "what mode am I in"
 * query, so we probe several mode opcodes in sequence and infer the answer from
 * which one reports enabled. The gotos below look like a crime scene, but they're
 * a legitimate (if ugly) way to fall through the detection chain without nesting
 * twelve levels deep. Ugly, but honest.
 */
int aerox16_laptop_probe(struct device *dev)
{
    int ret, output;
    u8 result;
    struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);

    /*
     * Hardware-specific shortcut: on an actual AERO X16 we know the silent
     * mode opcode, so skip the generic silent-mode probe entirely. This is the
     * only reason we care about the product family at all - it tells us which
     * opcode to trust.
     */
    if (!strcmp(dmi_get_system_info(DMI_PRODUCT_FAMILY), "GIGABYTE AERO")) {
        pr_info("Skipping silent fan mode ID check, only AERO X16 is supported\n");
        gigabyte->fan_silent_method = FAN_SILENT_MODE;
        goto obtain_fan_mode;
    }

 obtain_fan_mode:
    /*
     * Now that we know the silent opcode (for this board), patch it into the
     * fan_modes table so the rest of the code can use the generic lookup.
     */
    fan_modes[1] = gigabyte->fan_silent_method;

    /*
     * Fan-mode detection chain. We test opcodes in order and stop at the first
     * one that reports "enabled". This is why fan_mode can be 1, 2, 3, 4 or 5
     * depending on which flag the EC sets. It's a heuristic, not a guarantee -
     * the firmware doesn't give us a clean "current mode" register.
     */
    ret = aerox16_laptop_get_devstate(gigabyte->fan_silent_method, &output);
    if (ret)
        return ret;
    else if (output) {          /* Silent mode is on */
        gigabyte->fan_mode = 1;
        goto obtain_custom_fan_speed;
    }
    ret = aerox16_laptop_get_devstate(FAN_GAMING_MODE, &output);
    if (ret)
        return ret;
    else if (output) {          /* Gaming mode is on */
        gigabyte->fan_mode = 2;
        goto obtain_custom_fan_speed;
    }
    ret = aerox16_laptop_get_devstate(FAN_CUSTOM_MODE, &output);
    if (ret)
        return ret;
    else if (output) {          /* Custom/fan-curve mode is on */
        /*
         * The custom-mode flag alone doesn't tell us whether we're in the
         * "auto-maximum" sub-mode (4) or plain custom (3). We dig one layer
         * deeper by reading a raw EC byte and checking bit 7. Ugly, but it's
         * what the firmware exposes.
         */
        ret = ec_read(0xD, &result);
        if (ret)
            return AE_ERROR;
        output = (result >> 7) & 0x1;
        if (output) {
            gigabyte->fan_mode = 4;
            goto obtain_custom_fan_speed;
        }
        /* Not the auto-max sub-mode. Distinguish plain custom (3) from fixed
         * (5) by asking the EC directly which one it is. */
        ret = aerox16_laptop_get_devstate(FAN_FIXED_MODE, &output);
        if (ret)
            return ret;
        else if (output)
            gigabyte->fan_mode = 5;
        else
            gigabyte->fan_mode = 3;
        goto obtain_custom_fan_speed;
    }
    /* None of the mode flags were set -> default to "normal" (mode 0). */
    gigabyte->fan_mode = 0;

 obtain_custom_fan_speed:
    /*
     * Grab the saved custom/fan-curve speed so we can echo it back through
     * sysfs without re-asking. If the EC returned nothing, leave the field at
     * its zero initial value - that's a fine default for "we don't know yet".
     */
    ret = aerox16_laptop_get_devstate(FAN_CUSTOM_SPEED, &output);
    if (ret)
        return ret;
    else if (output)
        gigabyte->fan_custom_speed = output;

    /* Dual-fan detection: if the CPU-fan duty read is non-zero, this board
     * apparently has a second fan whose speed we should track too. */
    ret = aerox16_laptop_get_devstate(CPU_FAN_DUTY, &output);
    if (ret)
        return ret;
    else if (output) {
        pr_info("Dual fan speed control required\n");
        gigabyte->dual_fan_speed_enabled = 1;
    }

    /* Charging mode: the raw EC value has the meaningful bits in the high
     * nibble, so shift right by 2 to get the actual 0/1 mode. */
    ret = aerox16_laptop_get_devstate(CHARGING_MODE, &output);
    if (ret)
        return ret;
    else if (output)
        gigabyte->charge_mode = output >> 2;

    /* Charging limit: a straight percentage (60-100), no transformation needed. */
    ret = aerox16_laptop_get_devstate(CHARGING_LIMIT, &output);
    if (ret)
        return ret;
    else if (output)
        gigabyte->charge_limit = output;

    /*
     * Fan-curve table: read all 15 (temperature, speed) points out of the EC.
     * Each point is a 16-bit value packed as (speed << 8 | temperature), so we
     * split it back apart with the inverse operations. If a point reads as zero
     * we skip storing it - an unset point stays zero, which is the correct "off"
     * state.
     */
    for (u8 i = 0; i < FAN_CURVE_POINTS; i++) {
        ret = aerox16_laptop_get_devstate2(FAN_INDEX_VALUE, i, &output);
        if (ret)
            return ret;
        else if (output) {
            gigabyte->fan_curve.temperature[i] = output;
            gigabyte->fan_curve.speed[i] = output >> 8;
        }
    }

    /*
     * Light sensor: there are TWO opcodes for it (an "old" single-int one and
     * a "new" 4-byte-buffer one). We probe the new one first; if it returns the
     * 0xF7 marker we know the firmware speaks "new", otherwise we fall back to
     * the old protocol. Detecting this at probe time means the sysfs read path
     * can commit to one branch forever after.
     */
    {
        u8 light_sensor_result[4];

        ret = aerox16_laptop_get_devstate(LIGHT_SENSOR_NEW, &light_sensor_result);
        if (ret)
            return ret;
        if (light_sensor_result[0] == 0xF7) {
            gigabyte->light_sensor_method = LIGHT_SENSOR_NEW;
            pr_info("Using new light sensor method\n");
        } else {
            gigabyte->light_sensor_method = LIGHT_SENSOR;
            pr_info("Using old light sensor method\n");
        }
    }

    return 0;                   /* Success: the wmi struct is now fully populated. */
}

/*
 * aerox16_laptop_exit - tear everything back down.
 *
 * Called when the module unloads. Order matters: unregister the children (hwmon,
 * sysfs) before we unregister the parent driver/device, then free our private
 * state. It's the reverse of init(), and doing it backwards is how you get use-
 * after-free bugs that only show up under load.
 */
void aerox16_laptop_exit(void)
{
    struct aerox16_laptop_wmi *gigabyte;

    pr_info("Stopping AERO X16 WMI kernel driver\n");
    gigabyte = platform_get_drvdata(platform_device);
    hwmon_device_unregister(gigabyte->hwmon_dev);
    sysfs_remove_group(&gigabyte->pdev->dev.kobj, &aerox16_laptop_attr_group);
    platform_driver_unregister(&platform_driver);
    platform_device_unregister(gigabyte->pdev);
    kfree(gigabyte);
}

/*
 * aerox16_laptop_init - the module entry point (module_init).
 *
 * This is where the driver actually starts. It:
 *   1. Checks that the WMI GUIDs we need exist (else there's no point going on).
 *   2. Checks that we're on supported hardware (else refuse to load).
 *   3. Registers the platform driver and device.
 *   4. Creates the sysfs group and hwmon device.
 *   5. Runs probe() to populate state.
 *
 * Every failure point unwinds via the fail_* labels at the bottom. Those labels
 * look repetitive, but each one only has to undo what the previous step did -
 * that's the whole point of ordered cleanup. Don't "simplify" them into a single
 * goto; they're deliberately layered so nothing is double-freed or leaked.
 */
int aerox16_laptop_init(void)
{
    struct aerox16_laptop_wmi *gigabyte;
    int result;

    /*
     * Sanity check #1: if the EC doesn't even expose our WMI GUIDs, this
     * laptop isn't the hardware we target (or the firmware is different).
     * Bailing now is far better than registering a driver that can never work.
     */
    if (!wmi_has_guid(WMI_METHOD_WMBC) || !wmi_has_guid(WMI_METHOD_WMBD)) {
        pr_warn("No known WMI GUID found!\n");
        return -ENODEV;
    }

    /* Sanity check #2: are we on a Gigabyte AERO? DMI says so, or we refuse. */
    if (!dmi_check_system(aerox16_laptop_known_working_platforms)) {
        pr_err("Laptop not supported\n");
        return -ENODEV;
    }

    /* Register the driver with the kernel. From here on it can get bound. */
    result = platform_driver_register(&platform_driver);
    if (result) {
        pr_warn("Unable to register platform driver\n");
        return result;
    }

    /* Allocate our private state. kzalloc zeroes it, so everything starts at a
     * sane default and we don't have to initialise each field by hand. */
    gigabyte = kzalloc(sizeof(*gigabyte), GFP_KERNEL);
    if (!gigabyte) {
        result = -ENOMEM;
        goto fail_platform_driver;
    }

    /* Allocate the platform device. The -1 means "no id" - there's exactly one
     * of these, so we don't need per-instance numbering. */
    platform_device = platform_device_alloc(AEROX16_LAPTOP_FILE, -1);
    if (!platform_device) {
        pr_warn("Unable to allocate platform device\n");
        kfree(gigabyte);
        result = -ENOMEM;
        goto fail_platform_driver;
    }

    gigabyte->pdev = platform_device;
    platform_set_drvdata(gigabyte->pdev, gigabyte);

    /* Actually add the device to the system. If this fails, unwind. */
    result = platform_device_add(gigabyte->pdev);
    if (result) {
        pr_warn("Unable to add platform device\n");
        goto fail_platform_device;
    }

    /* Expose our sysfs attributes under /sys/devices/platform/<name>. */
    result = sysfs_create_group(&gigabyte->pdev->dev.kobj, &aerox16_laptop_attr_group);
    if (result)
        goto fail_sysfs;

    /* Register with the hwmon subsystem so sensors show up under hwmonN/. */
    gigabyte->hwmon_dev = hwmon_device_register_with_info(&gigabyte->pdev->dev,
                                                          AEROX16_LAPTOP_FILE, gigabyte,
                                                          &aerox16_laptop_chip_info, NULL);
    if (IS_ERR(gigabyte->hwmon_dev)) {
        result = PTR_ERR(gigabyte->hwmon_dev);
        pr_err("hwmon registration failed with %d\n", result);
        goto fail_sysfs;        /* hwmon failed, but sysfs was created first - unwind in order */
    }

    /* Do the discovery pass. If it fails, tear down everything we set up. */
    result = aerox16_laptop_probe(&gigabyte->pdev->dev);
    if (result) {
        pr_err("Probe failed\n");
        goto fail_probe;
    }
    pr_info("AERO X16 WMI kernel driver started\n");
    return 0;

 fail_probe:
    hwmon_device_unregister(gigabyte->hwmon_dev);
    sysfs_remove_group(&gigabyte->pdev->dev.kobj, &aerox16_laptop_attr_group);
 fail_sysfs:
    platform_device_del(gigabyte->pdev);
 fail_platform_device:
    platform_device_put(gigabyte->pdev);
    kfree(gigabyte);
 fail_platform_driver:
    platform_driver_unregister(&platform_driver);
    return result;
}

module_init(aerox16_laptop_init);
module_exit(aerox16_laptop_exit);
