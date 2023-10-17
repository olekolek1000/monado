// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Remote driver builder.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "remote/r_interface.h"

#include <assert.h>


#ifndef XRT_BUILD_DRIVER_REMOTE
#error "Must only be built with XRT_BUILD_DRIVER_REMOTE set"
#endif


/*
 *
 * Helper functions.
 *
 */

static bool
get_settings(cJSON *json, int *port)
{
	struct u_config_json config_json = {0};
	u_config_json_open_or_create_main_file(&config_json);

	bool bret = u_config_json_get_remote_port(&config_json, port);

	u_config_json_close(&config_json);

	return bret;
}

static const char *driver_list[] = {
    "remote",
};


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
remote_estimate_system(struct xrt_builder *xb,
                       cJSON *config,
                       struct xrt_prober *xp,
                       struct xrt_builder_estimate *estimate)
{
	estimate->certain.head = true;
	estimate->certain.left = true;
	estimate->certain.right = true;
	estimate->priority = -50;

	return XRT_SUCCESS;
}

static xrt_result_t
remote_open_system(struct xrt_builder *xb,
                   cJSON *config,
                   struct xrt_prober *xp,
                   struct xrt_system_devices **out_xsysd,
                   struct xrt_space_overseer **out_xso)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);


	int port = 4242;
	if (!get_settings(config, &port)) {
		port = 4242;
	}

	struct xrt_system_devices *xsysd = NULL;
	xrt_result_t xret = r_create_devices(port, &xsysd);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	*out_xsysd = xsysd;
	u_builder_create_space_overseer_legacy( //
	    xsysd->roles.head,                  // head
	    xsysd->roles.left,                  // left
	    xsysd->roles.right,                 // right
	    xsysd->xdevs,                       // xdevs
	    xsysd->xdev_count,                  // xdev_count
	    out_xso);                           // out_xso

	return xret;
}

static void
remote_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_remote_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = remote_estimate_system;
	xb->open_system = remote_open_system;
	xb->destroy = remote_destroy;
	xb->identifier = "remote";
	xb->name = "Remote simulation devices builder";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);
	xb->exclude_from_automatic_discovery = true;

	return xb;
}
