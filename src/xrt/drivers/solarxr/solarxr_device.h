// Copyright 2024, rcelyte
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SolarXR protocol bridge device
 * @ingroup drv_solarxr
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
struct xrt_tracking_origin;
struct xrt_device;

#ifdef __cplusplus
extern "C" {
#endif

uint32_t
solarxr_device_create_xdevs(struct xrt_device *hmd, struct xrt_device **out_xdevs, uint32_t out_xdevs_cap);

static inline struct xrt_device *
solarxr_device_create(struct xrt_device *const hmd)
{
	struct xrt_device *out = NULL;
	const uint32_t result = solarxr_device_create_xdevs(hmd, &out, 1);
	return result ? out : NULL;
}


#ifdef __cplusplus
}
#endif
