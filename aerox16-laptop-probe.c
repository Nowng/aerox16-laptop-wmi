// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-probe.c - AERO X16 laptop probe + driver lifecycle
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
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

MODULE_AUTHOR("Hoonowng");
MODULE_DESCRIPTION("Gigabyte laptop AERO X16 WMI driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(GIGABYTE_LAPTOP_VERSION);

static const struct dmi_system_id aerox16_laptop_known_working_platforms[] = {
	DMI_EXACT_MATCH_GIGABYTE_LAPTOP_FAMILY("GIGABYTE AERO"),
	{}
};

struct platform_device *platform_device;

struct platform_driver platform_driver = {
	.driver = {
		.name = AEROX16_LAPTOP_FILE,
		.owner = THIS_MODULE,
	},
};

u8 fan_modes[] = {
	0,
	FAN_SILENT_MODE,
	FAN_GAMING_MODE,
	FAN_CUSTOM_MODE,
	FAN_AUTO_MODE,
	FAN_FIXED_MODE
};

int aerox16_laptop_probe(struct device *dev)
{
	int ret, output;
	u8 result;
	struct aerox16_laptop_wmi *gigabyte = dev_get_drvdata(dev);

	if (!strcmp(dmi_get_system_info(DMI_PRODUCT_FAMILY), "GIGABYTE AERO")) {
		pr_info("Skipping silent fan mode ID check, only AERO X16 is supported\n");
		gigabyte->fan_silent_method = FAN_SILENT_MODE;
		goto obtain_fan_mode;
	}

obtain_fan_mode:
	fan_modes[1] = gigabyte->fan_silent_method;

	ret = aerox16_laptop_get_devstate(gigabyte->fan_silent_method, &output);
	if (ret)
		return ret;
	else if (output) {
		gigabyte->fan_mode = 1;
		goto obtain_custom_fan_speed;
	}
	ret = aerox16_laptop_get_devstate(FAN_GAMING_MODE, &output);
	if (ret)
		return ret;
	else if (output) {
		gigabyte->fan_mode = 2;
		goto obtain_custom_fan_speed;
	}
	ret = aerox16_laptop_get_devstate(FAN_CUSTOM_MODE, &output);
	if (ret)
		return ret;
	else if (output) {
		ret = ec_read(0xD, &result);
		if (ret)
			return AE_ERROR;
		output = (result >> 7) & 0x1;
		if (output) {
			gigabyte->fan_mode = 4;
			goto obtain_custom_fan_speed;
		}
		ret = aerox16_laptop_get_devstate(FAN_FIXED_MODE, &output);
		if (ret)
			return ret;
		else if (output)
			gigabyte->fan_mode = 5;
		else
			gigabyte->fan_mode = 3;
		goto obtain_custom_fan_speed;
	}
	gigabyte->fan_mode = 0;

obtain_custom_fan_speed:
	ret = aerox16_laptop_get_devstate(FAN_CUSTOM_SPEED, &output);
	if (ret)
		return ret;
	else if (output)
		gigabyte->fan_custom_speed = output;

	ret = aerox16_laptop_get_devstate(CPU_FAN_DUTY, &output);
	if (ret)
		return ret;
	else if (output) {
		pr_info("Dual fan speed control required\n");
		gigabyte->dual_fan_speed_enabled = 1;
	}

	ret = aerox16_laptop_get_devstate(CHARGING_MODE, &output);
	if (ret)
		return ret;
	else if (output)
		gigabyte->charge_mode = output >> 2;

	ret = aerox16_laptop_get_devstate(CHARGING_LIMIT, &output);
	if (ret)
		return ret;
	else if (output)
		gigabyte->charge_limit = output;

	for (u8 i = 0; i < FAN_CURVE_POINTS; i++) {
		ret = aerox16_laptop_get_devstate2(FAN_INDEX_VALUE, i, &output);
		if (ret)
			return ret;
		else if (output) {
			gigabyte->fan_curve.temperature[i] = output;
			gigabyte->fan_curve.speed[i] = output >> 8;
		}
	}

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

	return 0;
}

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

int aerox16_laptop_init(void)
{
	struct aerox16_laptop_wmi *gigabyte;
	int result;

	if (!wmi_has_guid(WMI_METHOD_WMBC) || !wmi_has_guid(WMI_METHOD_WMBD)) {
		pr_warn("No known WMI GUID found!\n");
		return -ENODEV;
	}

	if (!dmi_check_system(aerox16_laptop_known_working_platforms)) {
		pr_err("Laptop not supported\n");
		return -ENODEV;
	}

	result = platform_driver_register(&platform_driver);
	if (result) {
		pr_warn("Unable to register platform driver\n");
		return result;
	}

	gigabyte = kzalloc(sizeof(*gigabyte), GFP_KERNEL);
	if (!gigabyte) {
		result = -ENOMEM;
		goto fail_platform_driver;
	}

	platform_device = platform_device_alloc(AEROX16_LAPTOP_FILE, -1);
	if (!platform_device) {
		pr_warn("Unable to allocate platform device\n");
		kfree(gigabyte);
		result = -ENOMEM;
		goto fail_platform_driver;
	}

	gigabyte->pdev = platform_device;
	platform_set_drvdata(gigabyte->pdev, gigabyte);

	result = platform_device_add(gigabyte->pdev);
	if (result) {
		pr_warn("Unable to add platform device\n");
		goto fail_platform_device;
	}

	result = sysfs_create_group(&gigabyte->pdev->dev.kobj, &aerox16_laptop_attr_group);
	if (result)
		goto fail_sysfs;

	gigabyte->hwmon_dev = hwmon_device_register_with_info(&gigabyte->pdev->dev,
							     AEROX16_LAPTOP_FILE, gigabyte,
							     &aerox16_laptop_chip_info, NULL);
	if (IS_ERR(gigabyte->hwmon_dev)) {
		result = PTR_ERR(gigabyte->hwmon_dev);
		pr_err("hwmon registration failed with %d\n", result);
		goto fail_sysfs;
	}

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