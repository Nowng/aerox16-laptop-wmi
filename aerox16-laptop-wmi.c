// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-wmi.c - AERO X16 laptop WMI EC communication (Layer 3)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/wmi.h>

#include "aerox16-laptop.h"

/*
 * aerox16_laptop_get_devstate2 - read a value from the EC via WMBC.
 *
 * WMBC ("Seems to only return values") executes the ACPI method for the
 * given method_id and returns whatever the EC reports. The returned object
 * is either an INTEGER (a single scalar) or a BUFFER (an array of bytes).
 * We decode both: integers are copied as ints, buffers are memcpy'd into
 * the caller-provided result buffer.
 *
 * arg2 is an optional second method argument (ACPI method parameters). It is
 * passed through to the EC method along with method_id.
 */
int aerox16_laptop_get_devstate2(u32 method_id, u32 arg2, void *result)
{
    union acpi_object *obj;
    acpi_status status;
    struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    struct acpi_buffer input = { sizeof(arg2), &arg2 };

    status = wmi_evaluate_method(WMI_METHOD_WMBC, 0, method_id, &input, &buffer);
    if (ACPI_FAILURE(status))
        return -1;

    obj = buffer.pointer;
    if (obj && obj->type == ACPI_TYPE_INTEGER)
        *(int *)result = obj->integer.value;
    // TODO: Fix copying data to pointer
    else if (obj && obj->type == ACPI_TYPE_BUFFER) {
        if (obj->buffer.length == 0) {
            kfree(obj);
            return -ENODATA;
        }

        memcpy(result, obj->buffer.pointer, obj->buffer.length);
    } else {
        kfree(obj);
        return -EINVAL;
    }
    kfree(obj);
    return 0;
}

/* aerox16_laptop_get_devstate - read a value with no second argument. */
int aerox16_laptop_get_devstate(u32 method_id, void *result)
{
    return aerox16_laptop_get_devstate2(method_id, 0, result);
}

/*
 * aerox16_laptop_set_devstate - write a value to the EC via WMBD.
 *
 * WMBD ("Will probably do most of the work") is the write direction: we send
 * method_id + arg2 as the input payload and receive the EC's echoed/confirmed
 * value back through *result. We only accept an INTEGER echo; any other object
 * type indicates the method did not behave as expected, so we return -EINVAL.
 */
int aerox16_laptop_set_devstate(u32 method_id, u32 arg2, int *result)
{
    union acpi_object *obj;
    acpi_status status;
    struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    struct acpi_buffer input = { sizeof(arg2), &arg2 };

    status = wmi_evaluate_method(WMI_METHOD_WMBD, 0, method_id, &input, &buffer);
    if (ACPI_FAILURE(status))
        return -1;

    obj = buffer.pointer;
    if (obj && obj->type == ACPI_TYPE_INTEGER)
        *result = obj->integer.value;
    else {
        kfree(obj);
        return -EINVAL;
    }
    kfree(obj);
    return 0;
}
