// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  aerox16-laptop-wmi.c - AERO X16 laptop WMI EC communication (Layer 3)
 *
 *  Copyright (C) 2023 Albert Tang
 *  Copyright (C) 2026 Hoonowng
 *
 *  This is the bottom-most layer: the only code that actually talks to the
 *  embedded controller over WMI. Everything above it (probe, sysfs, hwmon) is
 *  just polite conversation built on top of these three functions. If something
 *  is broken at the hardware level, the bug lives here.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/wmi.h>

#include "aerox16-laptop.h"

/*
 * aerox16_laptop_get_devstate2 - read a value from the EC via WMBC.
 *
 * This is the real workhorse. WMBC ("Seems to only return values") executes an
 * arbitrary ACPI method and returns whatever the EC reports. The returned object
 * is one of two shapes:
 *
 *   - An INTEGER: a single scalar number. We reinterpret it as an int and stash
 *     it in *result. (Yes, casting a union field to `int *` is a little loose -
 *     but the integer.value field really is a plain int, so it's fine.)
 *   - A BUFFER: an array of bytes. We memcpy the raw bytes into the caller's
 *     result buffer. This is how multi-byte things (light sensor, power-on time)
 *     come back.
 *
 * arg2 is an optional second method argument. Most callers don't need it, which
 * is why get_devstate() exists as the no-arg wrapper - but when you do need to
 * pass a parameter (e.g. the fan-curve index), this is the function that takes
 * it. The parameter is sent on the wire as a 4-byte little-endian integer.
 */
int aerox16_laptop_get_devstate2(u32 method_id, u32 arg2, void *result)
{
    union acpi_object *obj;
    acpi_status status;
    struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    struct acpi_buffer input = { sizeof(arg2), &arg2 };

    /* Ask the EC to run the method and hand us the result in a fresh buffer.
     * ACPI_ALLOCATE_BUFFER tells the kernel to kmalloc the return for us - we
     * own it now and must kfree it, or we leak. */
    status = wmi_evaluate_method(WMI_METHOD_WMBC, 0, method_id, &input, &buffer);
    if (ACPI_FAILURE(status))
        return -1;

    obj = buffer.pointer;
    if (obj && obj->type == ACPI_TYPE_INTEGER)
        *(int *)result = obj->integer.value;
    /*
     * TODO: Fix copying data to pointer.
     *
     * This TODO is real and it's the kind of thing that bites you later: when
     * the EC returns a buffer, we memcpy straight into `result`. If the caller
     * passed a buffer that's too small for what the EC actually sent, we write
     * off the end of it. Nobody has fixed it yet because in practice the callers
     * allocate exactly the right size - but "in practice" is not a guarantee,
     * it's a prayer. Someone should bound-check this before it hurts someone.
     */
    else if (obj && obj->type == ACPI_TYPE_BUFFER) {
        if (obj->buffer.length == 0) {
            kfree(obj);
            return -ENODATA;    /* Nothing came back. Not an error per se, just empty. */
        }

        memcpy(result, obj->buffer.pointer, obj->buffer.length);
    } else {
        /* Neither integer nor buffer? That's not something we know how to
         * handle, so refuse it rather than guessing. */
        kfree(obj);
        return -EINVAL;
    }
    /*
     * We allocated this with the kernel (via ACPI_ALLOCATE_BUFFER), so we have
     * to give it back. Do it on every exit path above, or you leak kernel
     * memory - and kernel memory leaks are the slow, invisible kind that make
     * people hate their jobs.
     */
    kfree(obj);
    return 0;
}

/*
 * aerox16_laptop_get_devstate - read a value with no second argument.
 *
 * Trivial thin wrapper: most reads don't need a method parameter, so just pass
 * arg2 = 0 and get on with your life. This is the version you'll see everywhere
 * except the fan-curve code, which has to pass an index.
 */
int aerox16_laptop_get_devstate(u32 method_id, void *result)
{
    return aerox16_laptop_get_devstate2(method_id, 0, result);
}

/*
 * aerox16_laptop_set_devstate - write a value to the EC via WMBD.
 *
 * WMBD ("Will probably do most of the work") is the write direction. We send
 * method_id + arg2 as the input payload and expect the EC to echo back its
 * confirmed/updated value through *result.
 *
 * The contract here is strict: we only accept an INTEGER echo. If the EC comes
 * back with anything that isn't a plain integer, we treat it as "the method did
 * not behave as expected" and return -EINVAL. Being lenient here would just mask
 * firmware bugs, and masking firmware bugs is how you ship something that works
 * on your machine and not anyone else's.
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
        return -EINVAL;         /* Unexpected return type. Refuse, don't guess. */
    }
    kfree(obj);
    return 0;
}
