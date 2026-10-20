// SPDX-License-Identifier: GPL-2.0-only
/*
 * OLED exposure adjustment using the SDE DSPP PCC block.
 *
 * Adapted for the 5.4 DSI display driver. Keep the panel context explicit
 * and validate the DRM objects before accessing the PCC property.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <drm/drm_crtc.h>
#include <drm/drm_property.h>
#include <drm/msm_drm.h>

#include "dsi_display.h"
#include "dsi_panel.h"
#include "../msm_drv.h"
#include "../sde/sde_color_processing.h"
#include "exposure_adjustment.h"

static bool pcc_backlight_enable;
static bool pcc_correction_active;
static u32 last_level = ELVSS_OFF_THRESHOLD;

static int ea_panel_send_pcc(struct dsi_panel *panel, u32 bl_lvl)
{
	struct drm_msm_pcc pcc_blk = { 0 };
	struct drm_crtc *crtc;
	struct drm_property *prop;
	struct drm_property_blob *blob;
	struct dsi_display *display;
	struct msm_drm_private *priv;
	u32 ea_coeff;
	int rc;

	if (!panel || !panel->parent)
		return -EINVAL;

	display = dev_get_drvdata(panel->parent);
	if (!display || !display->drm_conn ||
			!display->drm_conn->state ||
			!display->drm_conn->state->crtc)
		return -ENODEV;

	crtc = display->drm_conn->state->crtc;
	if (!crtc->state || !crtc->state->active ||
			!crtc->dev || !crtc->dev->dev_private)
		return -ENODEV;

	priv = crtc->dev->dev_private;
	/* SDE_CP_CRTC_DSPP_PCC follows IGC in the feature enum (index 1). */
	if (!priv->cp_property || !priv->cp_property[1])
		return -EOPNOTSUPP;
	prop = priv->cp_property[1];

	if (bl_lvl < ELVSS_OFF_THRESHOLD)
		ea_coeff = bl_lvl * PCC_BACKLIGHT_SCALE +
			EXPOSURE_ADJUSTMENT_MIN;
	else
		ea_coeff = EXPOSURE_ADJUSTMENT_MAX;

	pcc_blk.r.r = ea_coeff;
	pcc_blk.g.g = ea_coeff;
	pcc_blk.b.b = ea_coeff;

	blob = drm_property_create_blob(crtc->dev, sizeof(pcc_blk), &pcc_blk);
	if (IS_ERR(blob))
		return PTR_ERR(blob);
	if (!blob)
		return -ENOMEM;

	rc = sde_cp_crtc_set_property(crtc, prop, blob->base.id);
	/* sde_cp_crtc_set_property takes its own reference to the blob. */
	drm_property_blob_put(blob);

	if (rc)
		pr_debug("exposure adjustment: PCC update failed, rc=%d\n", rc);

	return rc;
}

bool ea_panel_is_enabled(void)
{
	return READ_ONCE(pcc_backlight_enable);
}

int ea_panel_mode_ctrl(struct dsi_panel *panel, bool enable)
{
	int rc;

	if (!panel)
		return -EINVAL;

	/* A disabled panel may still retain the active CRTC PCC state. */
	if (!dsi_panel_initialized(panel)) {
		if (!enable && READ_ONCE(pcc_correction_active)) {
			rc = ea_panel_send_pcc(panel, ELVSS_OFF_THRESHOLD);
			if (rc && rc != -ENODEV)
				return rc;
			WRITE_ONCE(pcc_correction_active, false);
		}
		WRITE_ONCE(pcc_backlight_enable, enable);
		return 0;
	}

	if (READ_ONCE(pcc_backlight_enable) == enable) {
		if (!READ_ONCE(last_level) && !enable) {
			rc = ea_panel_send_pcc(panel, ELVSS_OFF_THRESHOLD);
			if (!rc)
				WRITE_ONCE(pcc_correction_active, false);
			return rc;
		}
		return 0;
	}

	/* Deactivate PCC correction before lowering the physical backlight. */
	if (!enable && READ_ONCE(pcc_correction_active)) {
		rc = ea_panel_send_pcc(panel, ELVSS_OFF_THRESHOLD);
		if (rc)
			return rc;
		WRITE_ONCE(pcc_correction_active, false);
	}

	WRITE_ONCE(pcc_backlight_enable, enable);
	rc = dsi_panel_set_backlight(panel, READ_ONCE(last_level));
	if (rc)
		pr_err("exposure adjustment: backlight update failed, rc=%d\n", rc);

	return 0;
}

u32 ea_panel_calc_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	u32 requested = bl_lvl;
	u32 bl_dc_min;
	int rc;

	if (!panel)
		return bl_lvl;

	WRITE_ONCE(last_level, bl_lvl);
	if (!READ_ONCE(pcc_backlight_enable) || !bl_lvl)
		return bl_lvl;

	bl_dc_min = panel->bl_config.bl_min_level * 2;
	if (bl_lvl < bl_dc_min)
		bl_lvl = bl_dc_min;

	if (bl_lvl < ELVSS_OFF_THRESHOLD) {
		rc = ea_panel_send_pcc(panel, bl_lvl);
		if (rc) {
			pr_debug("exposure adjustment: low-brightness PCC update failed\n");
			/* Keep the previous corrected backlight if PCC is stale. */
			return READ_ONCE(pcc_correction_active) ?
				ELVSS_OFF_THRESHOLD : requested;
		}
		WRITE_ONCE(pcc_correction_active, true);
		return ELVSS_OFF_THRESHOLD;
	}

	/* Restore neutral PCC only when leaving the corrected range. */
	if (READ_ONCE(pcc_correction_active)) {
		rc = ea_panel_send_pcc(panel, ELVSS_OFF_THRESHOLD);
		if (rc) {
			pr_debug("exposure adjustment: PCC reset failed, rc=%d\n", rc);
			/* Don't pair a stale low PCC coefficient with high backlight. */
			return ELVSS_OFF_THRESHOLD;
		}
		WRITE_ONCE(pcc_correction_active, false);
	}

	return bl_lvl;
}
