/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OLED exposure adjustment using the SDE DSPP PCC block.
 *
 * The coefficient range is tuned for the Cepheus Samsung EA8076 panel.
 */

#ifndef _DSI_EXPOSURE_ADJUSTMENT_H_
#define _DSI_EXPOSURE_ADJUSTMENT_H_

#include <linux/types.h>

struct dsi_panel;

/* Minimum panel backlight level at which the PCC correction is disabled. */
#define ELVSS_OFF_THRESHOLD        1024
#define EXPOSURE_ADJUSTMENT_MIN    580
#define EXPOSURE_ADJUSTMENT_MAX    32768
#define PCC_BACKLIGHT_SCALE \
	((EXPOSURE_ADJUSTMENT_MAX - EXPOSURE_ADJUSTMENT_MIN) / \
	 ELVSS_OFF_THRESHOLD)

int ea_panel_mode_ctrl(struct dsi_panel *panel, bool enable);
bool ea_panel_is_enabled(void);
u32 ea_panel_calc_backlight(struct dsi_panel *panel, u32 bl_lvl);

#endif /* _DSI_EXPOSURE_ADJUSTMENT_H_ */
