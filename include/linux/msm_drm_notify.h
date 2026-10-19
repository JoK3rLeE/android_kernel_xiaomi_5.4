/*
 * Compatibility notifier API for legacy MSM DRM touchscreen clients.
 *
 * The 5.4 display stack uses drm_panel_notifier; this header preserves the
 * older Qualcomm MSM DRM blank-event ABI for drivers that have not yet been
 * migrated.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef _MSM_DRM_NOTIFY_H_
#define _MSM_DRM_NOTIFY_H_

#include <linux/notifier.h>

/* Legacy MSM DRM blank event values */
#define MSM_DRM_EVENT_BLANK              0x01
#define MSM_DRM_EARLY_EVENT_BLANK         0x02
#define MSM_DRM_R_EARLY_EVENT_BLANK       0x03

enum {
	MSM_DRM_BLANK_UNBLANK,
	MSM_DRM_BLANK_LP1,
	MSM_DRM_BLANK_LP2,
	MSM_DRM_BLANK_STANDBY,
	MSM_DRM_BLANK_SUSPEND,
	MSM_DRM_BLANK_POWERDOWN,
};

enum msm_drm_display_id {
	MSM_DRM_PRIMARY_DISPLAY,
	MSM_DRM_EXTERNAL_DISPLAY,
	MSM_DRM_DISPLAY_MAX
};

struct msm_drm_notifier {
	enum msm_drm_display_id id;
	void *data;
};

int msm_drm_register_client(struct notifier_block *nb);
int msm_drm_unregister_client(struct notifier_block *nb);
int msm_drm_notifier_call_chain(unsigned long val, void *v);

#endif /* _MSM_DRM_NOTIFY_H_ */
