/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform data shared by the CS35L41 codec driver and its bus frontends.
 * Keep this header in include/sound because the driver includes it as
 * <sound/cs35l41.h>.
 */
#ifndef __SOUND_CS35L41_H
#define __SOUND_CS35L41_H

#include <linux/types.h>
#include <linux/of.h>

struct classh_cfg {
	bool classh_algo_enable;
	bool classh_bst_override;
	u32 classh_bst_max_limit;
	u32 classh_mem_depth;
	u32 classh_release_rate;
	u32 classh_headroom;
	u32 classh_wk_fet_delay;
	u32 classh_wk_fet_thld;
};

struct irq_cfg {
	bool is_present;
	bool irq_pol_inv;
	bool irq_out_en;
	u32 irq_src_sel;
};

struct cs35l41_platform_data {
	bool right_channel;
	bool sclk_frc;
	bool lrclk_frc;
	bool amp_gain_zc;
	u32 temp_warn_thld;
	u32 bst_vctrl;
	u32 bst_ipk;
	u32 bst_ind;
	u32 bst_cap;
	int dout_hiz;
	bool ng_enable;
	u32 ng_pcm_thld;
	u32 ng_delay;
	struct device_node *spk_id_gpio_p;
	int mnSpkType;
	struct classh_cfg classh_config;
	struct irq_cfg irq_config1;
	struct irq_cfg irq_config2;
};

#endif /* __SOUND_CS35L41_H */
