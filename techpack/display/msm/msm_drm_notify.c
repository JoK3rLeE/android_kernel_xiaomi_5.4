// SPDX-License-Identifier: GPL-2.0
/*
 * Legacy MSM DRM blank notifier compatibility layer for Linux 5.4.
 *
 * The notifier is dispatched by the SDE KMS transition path, which adapts
 * drm_panel blank events to the legacy event values consumed by older
 * touchscreen drivers.
 */
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/msm_drm_notify.h>

static BLOCKING_NOTIFIER_HEAD(msm_drm_notifier_chain);

int msm_drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&msm_drm_notifier_chain, nb);
}
EXPORT_SYMBOL(msm_drm_register_client);

int msm_drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&msm_drm_notifier_chain, nb);
}
EXPORT_SYMBOL(msm_drm_unregister_client);

int msm_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&msm_drm_notifier_chain, val, v);
}
EXPORT_SYMBOL(msm_drm_notifier_call_chain);
