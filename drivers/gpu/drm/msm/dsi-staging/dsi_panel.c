/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"msm-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_ctrl_hw.h"
#if defined(CONFIG_DISPLAY_SAMSUNG)
#include "ss_dsi_panel_common.h"
#endif

/**
 * topology is currently defined by a set of following 3 values:
 * 1. num of layer mixers
 * 2. num of compression encoders
 * 3. num of interfaces
 */
#define TOPOLOGY_SET_LEN 3
#define MAX_TOPOLOGY 5

#define DSI_PANEL_DEFAULT_LABEL  "Default dsi panel"

#define DEFAULT_MDP_TRANSFER_TIME 14000

#define DEFAULT_PANEL_JITTER_NUMERATOR		2
#define DEFAULT_PANEL_JITTER_DENOMINATOR	1
#define DEFAULT_PANEL_JITTER_ARRAY_SIZE		2
#define MAX_PANEL_JITTER		10
#define DEFAULT_PANEL_PREFILL_LINES	25

enum dsi_dsc_ratio_type {
	DSC_8BPC_8BPP,
	DSC_10BPC_8BPP,
	DSC_12BPC_8BPP,
	DSC_RATIO_TYPE_MAX
};

static u32 dsi_dsc_rc_buf_thresh[] = {0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54,
		0x62, 0x69, 0x70, 0x77, 0x79, 0x7b, 0x7d, 0x7e};

/*
 * DSC 1.1
 * Rate control - Min QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_min_qp_1_1[][15] = {
	{0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 13},
	{0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 17},
	{0, 4, 9, 9, 11, 11, 11, 11, 11, 11, 13, 13, 13, 15, 21},
	};

/*
 * DSC 1.1 SCR
 * Rate control - Min QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_min_qp_1_1_scr1[][15] = {
	{0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12},
	{0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16},
	{0, 4, 9, 9, 11, 11, 11, 11, 11, 11, 13, 13, 13, 17, 20},
	};

/*
 * DSC 1.1
 * Rate control - Max QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_max_qp_1_1[][15] = {
	{4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 11, 12, 13, 13, 15},
	{8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 15, 16, 17, 17, 19},
	{12, 12, 13, 14, 15, 15, 15, 16, 17, 18, 19, 20, 21, 21, 23},
	};

/*
 * DSC 1.1 SCR
 * Rate control - Max QP values for each ratio type in dsi_dsc_ratio_type
 */
static char dsi_dsc_rc_range_max_qp_1_1_scr1[][15] = {
	{4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13},
	{8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17},
	{12, 12, 13, 14, 15, 15, 15, 16, 17, 18, 18, 19, 19, 20, 21},
	};

/*
 * DSC 1.1 and DSC 1.1 SCR
 * Rate control - bpg offset values
 */
static char dsi_dsc_rc_range_bpg_offset[] = {2, 0, 0, -2, -4, -6, -8, -8,
		-8, -10, -10, -12, -12, -12, -12};

int dsi_dsc_create_pps_buf_cmd(struct msm_display_dsc_info *dsc, char *buf,
				int pps_id)
{
	char *bp;
	char data;
	int i, bpp;
	char *dbgbp;

	dbgbp = buf;
	bp = buf;
	/* First 7 bytes are cmd header */
	*bp++ = 0x0A;
	*bp++ = 1;
	*bp++ = 0;
	*bp++ = 0;
	*bp++ = 10;
	*bp++ = 0;
	*bp++ = 128;

	*bp++ = (dsc->version & 0xff);		/* pps0 */
	*bp++ = (pps_id & 0xff);		/* pps1 */
	bp++;					/* pps2, reserved */

	data = dsc->line_buf_depth & 0x0f;
	data |= ((dsc->bpc & 0xf) << 4);
	*bp++ = data;				/* pps3 */

	bpp = dsc->bpp;
	bpp <<= 4;				/* 4 fraction bits */
	data = (bpp >> 8);
	data &= 0x03;				/* upper two bits */
	data |= ((dsc->block_pred_enable & 0x1) << 5);
	data |= ((dsc->convert_rgb & 0x1) << 4);
	data |= ((dsc->enable_422 & 0x1) << 3);
	data |= ((dsc->vbr_enable & 0x1) << 2);
	*bp++ = data;				/* pps4 */
	*bp++ = (bpp & 0xff);			/* pps5 */

	*bp++ = ((dsc->pic_height >> 8) & 0xff); /* pps6 */
	*bp++ = (dsc->pic_height & 0x0ff);	/* pps7 */
	*bp++ = ((dsc->pic_width >> 8) & 0xff);	/* pps8 */
	*bp++ = (dsc->pic_width & 0x0ff);	/* pps9 */

	*bp++ = ((dsc->slice_height >> 8) & 0xff);/* pps10 */
	*bp++ = (dsc->slice_height & 0x0ff);	/* pps11 */
	*bp++ = ((dsc->slice_width >> 8) & 0xff); /* pps12 */
	*bp++ = (dsc->slice_width & 0x0ff);	/* pps13 */

	*bp++ = ((dsc->chunk_size >> 8) & 0xff);/* pps14 */
	*bp++ = (dsc->chunk_size & 0x0ff);	/* pps15 */

	*bp++ = (dsc->initial_xmit_delay >> 8) & 0x3; /* pps16, bit 0, 1 */
	*bp++ = (dsc->initial_xmit_delay & 0xff);/* pps17 */

	*bp++ = ((dsc->initial_dec_delay >> 8) & 0xff); /* pps18 */
	*bp++ = (dsc->initial_dec_delay & 0xff);/* pps19 */

	bp++;					/* pps20, reserved */

	*bp++ = (dsc->initial_scale_value & 0x3f); /* pps21 */

	*bp++ = ((dsc->scale_increment_interval >> 8) & 0xff); /* pps22 */
	*bp++ = (dsc->scale_increment_interval & 0xff); /* pps23 */

	*bp++ = ((dsc->scale_decrement_interval >> 8) & 0xf); /* pps24 */
	*bp++ = (dsc->scale_decrement_interval & 0x0ff);/* pps25 */

	bp++;					/* pps26, reserved */

	*bp++ = (dsc->first_line_bpg_offset & 0x1f);/* pps27 */

	*bp++ = ((dsc->nfl_bpg_offset >> 8) & 0xff);/* pps28 */
	*bp++ = (dsc->nfl_bpg_offset & 0x0ff);	/* pps29 */
	*bp++ = ((dsc->slice_bpg_offset >> 8) & 0xff);/* pps30 */
	*bp++ = (dsc->slice_bpg_offset & 0x0ff);/* pps31 */

	*bp++ = ((dsc->initial_offset >> 8) & 0xff);/* pps32 */
	*bp++ = (dsc->initial_offset & 0x0ff);	/* pps33 */

	*bp++ = ((dsc->final_offset >> 8) & 0xff);/* pps34 */
	*bp++ = (dsc->final_offset & 0x0ff);	/* pps35 */

	*bp++ = (dsc->min_qp_flatness & 0x1f);	/* pps36 */
	*bp++ = (dsc->max_qp_flatness & 0x1f);	/* pps37 */

	*bp++ = ((dsc->rc_model_size >> 8) & 0xff);/* pps38 */
	*bp++ = (dsc->rc_model_size & 0x0ff);	/* pps39 */

	*bp++ = (dsc->edge_factor & 0x0f);	/* pps40 */

	*bp++ = (dsc->quant_incr_limit0 & 0x1f);	/* pps41 */
	*bp++ = (dsc->quant_incr_limit1 & 0x1f);	/* pps42 */

	data = ((dsc->tgt_offset_hi & 0xf) << 4);
	data |= (dsc->tgt_offset_lo & 0x0f);
	*bp++ = data;				/* pps43 */

	for (i = 0; i < 14; i++)
		*bp++ = (dsc->buf_thresh[i] & 0xff); /* pps44 - pps57 */

	for (i = 0; i < 15; i++) {		/* pps58 - pps87 */
		data = (dsc->range_min_qp[i] & 0x1f);
		data <<= 3;
		data |= ((dsc->range_max_qp[i] >> 2) & 0x07);
		*bp++ = data;
		data = (dsc->range_max_qp[i] & 0x03);
		data <<= 6;
		data |= (dsc->range_bpg_offset[i] & 0x3f);
		*bp++ = data;
	}

	return 128;
}

static int dsi_panel_vreg_get(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	struct regulator *vreg = NULL;

	for (i = 0; i < panel->power_info.count; i++) {
		vreg = devm_regulator_get(panel->parent,
					  panel->power_info.vregs[i].vreg_name);
		rc = PTR_RET(vreg);
		if (rc) {
			pr_err("failed to get %s regulator\n",
			       panel->power_info.vregs[i].vreg_name);
			goto error_put;
		}
		panel->power_info.vregs[i].vreg = vreg;
	}

	return rc;
error_put:
	for (i = i - 1; i >= 0; i--) {
		devm_regulator_put(panel->power_info.vregs[i].vreg);
		panel->power_info.vregs[i].vreg = NULL;
	}
	return rc;
}

static int dsi_panel_vreg_put(struct dsi_panel *panel)
{
	int rc = 0;
	int i;

	for (i = panel->power_info.count - 1; i >= 0; i--)
		devm_regulator_put(panel->power_info.vregs[i].vreg);

	return rc;
}

static int dsi_panel_gpio_request(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;

	if (gpio_is_valid(r_config->reset_gpio)) {
		rc = gpio_request(r_config->reset_gpio, "reset_gpio");
		if (rc) {
			pr_err("request for reset_gpio failed, rc=%d\n", rc);
			goto error;
		}
	}

	if (gpio_is_valid(r_config->disp_en_gpio)) {
		rc = gpio_request(r_config->disp_en_gpio, "disp_en_gpio");
		if (rc) {
			pr_err("request for disp_en_gpio failed, rc=%d\n", rc);
			goto error_release_reset;
		}
	}

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpio_request(panel->bl_config.en_gpio, "bklt_en_gpio");
		if (rc) {
			pr_err("request for bklt_en_gpio failed, rc=%d\n", rc);
			goto error_release_disp_en;
		}
	}

	if (gpio_is_valid(r_config->lcd_mode_sel_gpio)) {
		rc = gpio_request(r_config->lcd_mode_sel_gpio, "mode_gpio");
		if (rc) {
			pr_err("request for mode_gpio failed, rc=%d\n", rc);
			goto error_release_mode_sel;
		}
	}

	goto error;
error_release_mode_sel:
	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_free(panel->bl_config.en_gpio);
error_release_disp_en:
	if (gpio_is_valid(r_config->disp_en_gpio))
		gpio_free(r_config->disp_en_gpio);
error_release_reset:
	if (gpio_is_valid(r_config->reset_gpio))
		gpio_free(r_config->reset_gpio);
error:
	return rc;
}

static int dsi_panel_gpio_release(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;

	if (gpio_is_valid(r_config->reset_gpio))
		gpio_free(r_config->reset_gpio);

	if (gpio_is_valid(r_config->disp_en_gpio))
		gpio_free(r_config->disp_en_gpio);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_free(panel->bl_config.en_gpio);

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		gpio_free(panel->reset_config.lcd_mode_sel_gpio);

	return rc;
}

static int dsi_panel_reset(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int i;

	if (gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		rc = gpio_direction_output(panel->reset_config.disp_en_gpio, 1);
		if (rc) {
			pr_err("unable to set dir for disp gpio rc=%d\n", rc);
			goto exit;
		}
	}

	if (r_config->count) {
		rc = gpio_direction_output(r_config->reset_gpio,
			r_config->sequence[0].level);
		if (rc) {
			pr_err("unable to set dir for rst gpio rc=%d\n", rc);
			goto exit;
		}
	}

	for (i = 0; i < r_config->count; i++) {
		gpio_set_value(r_config->reset_gpio,
			       r_config->sequence[i].level);


		if (r_config->sequence[i].sleep_ms)
			usleep_range(r_config->sequence[i].sleep_ms * 1000,
				     r_config->sequence[i].sleep_ms * 1000);
	}

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpio_direction_output(panel->bl_config.en_gpio, 1);
		if (rc)
			pr_err("unable to set dir for bklt gpio rc=%d\n", rc);
	}

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio)) {
		bool out = true;

		if ((panel->reset_config.mode_sel_state == MODE_SEL_DUAL_PORT)
				|| (panel->reset_config.mode_sel_state
					== MODE_GPIO_LOW))
			out = false;
		else if ((panel->reset_config.mode_sel_state
				== MODE_SEL_SINGLE_PORT) ||
				(panel->reset_config.mode_sel_state
				 == MODE_GPIO_HIGH))
			out = true;

		rc = gpio_direction_output(
			panel->reset_config.lcd_mode_sel_gpio, out);
		if (rc)
			pr_err("unable to set dir for mode gpio rc=%d\n", rc);
	}
exit:
	return rc;
}

static int dsi_panel_set_pinctrl_state(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct pinctrl_state *state;

	if (enable)
		state = panel->pinctrl.active;
	else
		state = panel->pinctrl.suspend;

	rc = pinctrl_select_state(panel->pinctrl.pinctrl, state);
	if (rc)
		pr_err("[%s] failed to set pin state, rc=%d\n", panel->name,
		       rc);

	return rc;
}


#if defined(CONFIG_DISPLAY_SAMSUNG)
int dsi_panel_power_on(struct dsi_panel *panel)
#else
static int dsi_panel_power_on(struct dsi_panel *panel)
#endif
{
	int rc = 0;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	if (!ss_panel_attach_get(panel->panel_private)) {
		pr_info("PBA booting, skip to power on panel\n");
		return 0;
	}
#endif
	rc = dsi_pwr_enable_regulator(&panel->power_info, true);
	if (rc) {
		pr_err("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
		goto exit;
	}

	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		pr_err("[%s] failed to set pinctrl, rc=%d\n", panel->name, rc);
		goto error_disable_vregs;
	}

	rc = dsi_panel_reset(panel);
	if (rc) {
		pr_err("[%s] failed to reset panel, rc=%d\n", panel->name, rc);
		goto error_disable_gpio;
	}

	goto exit;

error_disable_gpio:
	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_set_value(panel->bl_config.en_gpio, 0);

	(void)dsi_panel_set_pinctrl_state(panel, false);

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);

exit:
	return rc;
}

#if defined(CONFIG_DISPLAY_SAMSUNG)
int dsi_panel_power_off(struct dsi_panel *panel)
#else
static int dsi_panel_power_off(struct dsi_panel *panel)
#endif
{
	int rc = 0;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	if (!ss_panel_attach_get(panel->panel_private)) {
		pr_info("PBA booting, skip to power off panel\n");
		return 0;
	}

	/* In case of lp11_init false, it should LP00 then reset low.
	 * For margin between LP00 and reset, delay 5ms.
	 */
	mdelay(5);
#endif

	if (gpio_is_valid(panel->reset_config.disp_en_gpio))
		gpio_set_value(panel->reset_config.disp_en_gpio, 0);

	if (gpio_is_valid(panel->reset_config.reset_gpio))
		gpio_set_value(panel->reset_config.reset_gpio, 0);

	if (gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		gpio_set_value(panel->reset_config.lcd_mode_sel_gpio, 0);

	rc = dsi_panel_set_pinctrl_state(panel, false);
	if (rc) {
		pr_err("[%s] failed set pinctrl state, rc=%d\n", panel->name,
		       rc);
	}

	rc = dsi_pwr_enable_regulator(&panel->power_info, false);
	if (rc)
		pr_err("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);

	return rc;
}

#if defined(CONFIG_DISPLAY_SAMSUNG)
int dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum dsi_cmd_set_type type)
#else
static int dsi_panel_tx_cmd_set(struct dsi_panel *panel,
				enum dsi_cmd_set_type type)
#endif
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
#if !defined(CONFIG_DISPLAY_SAMSUNG)
	struct dsi_display_mode *mode;
#endif
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	struct samsung_display_driver_data *vdd = panel->panel_private;
	struct dsi_panel_cmd_set *set;
	struct dsi_display *display = container_of(panel->host, struct dsi_display, host);
	size_t tot_tx_len = 0;

	/* ss_get_cmds() gets proper QCT cmds or SS cmds for panel revision. */
	set = ss_get_cmds(vdd, type);

	cmds = set->cmds;
	count = set->count;
	state = set->state;

	/* Block to send mipi packet in case of that exclusive mode
	 * (exclusive_tx.enable) is enabled and
	 * the packet has no token (exclusive_pass).
	 * After exclusive mode released, send mipi packets.
	 */
	if (unlikely(vdd->exclusive_tx.enable &&
			!set->exclusive_pass)) {
		pr_info("[SDE] %s: wait.. cmd[%d]=%s\n", __func__,
				type, ss_get_cmd_name(type));
		wait_event(vdd->exclusive_tx.ex_tx_waitq,
				!vdd->exclusive_tx.enable);
		pr_info("[SDE] %s: pass, cmd[%d]=%s\n", __func__,
				type, ss_get_cmd_name(type));
	}
	mutex_lock(&vdd->cmd_lock);
#else

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mode = panel->cur_mode;

	cmds = mode->priv_info->cmd_sets[type].cmds;
	count = mode->priv_info->cmd_sets[type].count;
	state = mode->priv_info->cmd_sets[type].state;
#endif

#if defined(CONFIG_DISPLAY_SAMSUNG)
	if (cmds && (display->ctrl[cmds->msg.ctrl].ctrl->secure_mode)) {
		for (i = 0 ; i < count ; i++) {
			if (cmds->msg.tx_len > DSI_CTRL_MAX_CMD_FIFO_STORE_SIZE) {
				pr_err("Over DSI_CTRL_MAX_CMD_FIFO_STORE_SIZE at secure_mode type = %d\n", type);
				if (type != TX_MDNIE_TUNE)
					WARN(1, "unexpected cmd type = %d\n", type);
				goto error;
			}
			cmds++;
		}
		for (i = 0 ; i < count ; i++)
			cmds--;
	}
#endif
	if (count == 0) {
		pr_debug("[%s] No commands to be sent for state(%d)\n",
			 panel->name, type);
		goto error;
	}

	for (i = 0; i < count; i++) {

		if (tot_tx_len == 0)
  			tot_tx_len = ALIGN((cmds->msg.tx_len + 4), 4);

		if (i < count - 1)
			tot_tx_len += ALIGN(((cmds + 1)->msg.tx_len + 4), 4);

		if ((tot_tx_len > DSI_CTRL_MAX_CMD_FET_MEMORY_SIZE) || (i == count-1) || (cmds->post_wait_ms)) {
			pr_debug("tot %zd is over than max || last cmd set, set last_command",
					tot_tx_len);
			cmds->last_command = true;
			tot_tx_len = 0;
		}
		else cmds->last_command = false;

		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		else
			cmds->msg.flags &= ~MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds(%d), rc=%d\n", type, rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000, cmds->post_wait_ms * 1000 + 10);
		cmds++;
	}
error:
#if defined(CONFIG_DISPLAY_SAMSUNG)
	mutex_unlock(&vdd->cmd_lock);
#endif
	return rc;
}

static int dsi_panel_pinctrl_deinit(struct dsi_panel *panel)
{
	int rc = 0;

	devm_pinctrl_put(panel->pinctrl.pinctrl);

	return rc;
}

static int dsi_panel_pinctrl_init(struct dsi_panel *panel)
{
	int rc = 0;

	/* TODO:  pinctrl is defined in dsi dt node */
	panel->pinctrl.pinctrl = devm_pinctrl_get(panel->parent);
	if (IS_ERR_OR_NULL(panel->pinctrl.pinctrl)) {
		rc = PTR_ERR(panel->pinctrl.pinctrl);
		pr_err("failed to get pinctrl, rc=%d\n", rc);
		goto error;
	}

	panel->pinctrl.active = pinctrl_lookup_state(panel->pinctrl.pinctrl,
						       "panel_active");
	if (IS_ERR_OR_NULL(panel->pinctrl.active)) {
		rc = PTR_ERR(panel->pinctrl.active);
		pr_err("failed to get pinctrl active state, rc=%d\n", rc);
		goto error;
	}

	panel->pinctrl.suspend =
		pinctrl_lookup_state(panel->pinctrl.pinctrl, "panel_suspend");

	if (IS_ERR_OR_NULL(panel->pinctrl.suspend)) {
		rc = PTR_ERR(panel->pinctrl.suspend);
		pr_err("failed to get pinctrl suspend state, rc=%d\n", rc);
		goto error;
	}

error:
	return rc;
}

#ifdef CONFIG_LEDS_TRIGGERS
static int dsi_panel_led_bl_register(struct dsi_panel *panel,
				struct dsi_backlight_config *bl)
{
	int rc = 0;

	led_trigger_register_simple("bkl-trigger", &bl->wled);

	/* LED APIs don't tell us directly whether a classdev has yet
	 * been registered to service this trigger. Until classdev is
	 * registered, calling led_trigger has no effect, and doesn't
	 * fail. Classdevs are associated with any registered triggers
	 * when they do register, but that is too late for FBCon.
	 * Check the cdev list directly and defer if appropriate.
	 */
	if (!bl->wled) {
		pr_err("[%s] backlight registration failed\n", panel->name);
		rc = -EINVAL;
	} else {
		read_lock(&bl->wled->leddev_list_lock);
		if (list_empty(&bl->wled->led_cdevs))
			rc = -EPROBE_DEFER;
		read_unlock(&bl->wled->leddev_list_lock);

		if (rc) {
			pr_info("[%s] backlight %s not ready, defer probe\n",
				panel->name, bl->wled->name);
			led_trigger_unregister_simple(bl->wled);
		}
	}

	return rc;
}
#else
static int dsi_panel_led_bl_register(struct dsi_panel *panel,
				struct dsi_backlight_config *bl)
{
	return 0;
}
#endif

static int dsi_panel_update_backlight(struct dsi_panel *panel,
	u32 bl_lvl)
{
	int rc = 0;
	struct mipi_dsi_device *dsi;

	if (!panel || (bl_lvl > 0xffff)) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	dsi = &panel->mipi_device;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	rc = ss_brightness_dcs(panel->panel_private, bl_lvl, true);
	if (rc < 0)
		pr_err("failed to update dcs backlight:%d\n", bl_lvl);
#else
	rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);
	if (rc < 0)
		pr_err("failed to update dcs backlight:%d\n", bl_lvl);
#endif

	return rc;
}

int dsi_panel_set_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	pr_debug("backlight type:%d lvl:%d\n", bl->type, bl_lvl);
	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		led_trigger_event(bl->wled, bl_lvl);
		break;
	case DSI_BACKLIGHT_DCS:
		rc = dsi_panel_update_backlight(panel, bl_lvl);
		break;
	default:
		pr_err("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
	}

	return rc;
}

static int dsi_panel_bl_register(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		rc = dsi_panel_led_bl_register(panel, bl);
		break;
	case DSI_BACKLIGHT_DCS:
		break;
	default:
		pr_err("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_bl_unregister(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		led_trigger_unregister_simple(bl->wled);
		break;
	case DSI_BACKLIGHT_DCS:
		break;
	default:
		pr_err("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
		goto error;
	}

error:
	return rc;
}
static int dsi_panel_parse_timing(struct dsi_mode_info *mode,
				  struct device_node *of_node)
{
	int rc = 0;
	u64 tmp64;
	struct dsi_display_mode *display_mode;

	display_mode = container_of(mode, struct dsi_display_mode, timing);

	rc = of_property_read_u64(of_node,
			"qcom,mdss-dsi-panel-clockrate", &tmp64);
	if (rc == -EOVERFLOW) {
		tmp64 = 0;
		rc = of_property_read_u32(of_node,
			"qcom,mdss-dsi-panel-clockrate", (u32 *)&tmp64);
	}

	mode->clk_rate_hz = !rc ? tmp64 : 0;
	display_mode->priv_info->clk_rate_hz = mode->clk_rate_hz;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-panel-framerate",
				  &mode->refresh_rate);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-panel-framerate, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-panel-width",
				  &mode->h_active);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-panel-width, rc=%d\n", rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-h-front-porch",
				  &mode->h_front_porch);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-h-front-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-h-back-porch",
				  &mode->h_back_porch);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-h-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-h-pulse-width",
				  &mode->h_sync_width);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-h-pulse-width, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-h-sync-skew",
				  &mode->h_skew);
	if (rc)
		pr_err("qcom,mdss-dsi-h-sync-skew is not defined, rc=%d\n", rc);

	pr_debug("panel horz active:%d front_portch:%d back_porch:%d sync_skew:%d\n",
		mode->h_active, mode->h_front_porch, mode->h_back_porch,
		mode->h_sync_width);

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-panel-height",
				  &mode->v_active);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-panel-height, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-v-back-porch",
				  &mode->v_back_porch);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-v-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-v-front-porch",
				  &mode->v_front_porch);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-v-back-porch, rc=%d\n",
		       rc);
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-v-pulse-width",
				  &mode->v_sync_width);
	if (rc) {
		pr_err("failed to read qcom,mdss-dsi-v-pulse-width, rc=%d\n",
		       rc);
		goto error;
	}
	pr_debug("panel vert active:%d front_portch:%d back_porch:%d pulse_width:%d\n",
		mode->v_active, mode->v_front_porch, mode->v_back_porch,
		mode->v_sync_width);

error:
	return rc;
}

static int dsi_panel_parse_pixel_format(struct dsi_host_common_cfg *host,
					struct device_node *of_node,
					const char *name)
{
	int rc = 0;
	u32 bpp = 0;
	enum dsi_pixel_format fmt;
	const char *packing;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-bpp", &bpp);
	if (rc) {
		pr_err("[%s] failed to read qcom,mdss-dsi-bpp, rc=%d\n",
		       name, rc);
		return rc;
	}

	switch (bpp) {
	case 3:
		fmt = DSI_PIXEL_FORMAT_RGB111;
		break;
	case 8:
		fmt = DSI_PIXEL_FORMAT_RGB332;
		break;
	case 12:
		fmt = DSI_PIXEL_FORMAT_RGB444;
		break;
	case 16:
		fmt = DSI_PIXEL_FORMAT_RGB565;
		break;
	case 18:
		fmt = DSI_PIXEL_FORMAT_RGB666;
		break;
	case 24:
	default:
		fmt = DSI_PIXEL_FORMAT_RGB888;
		break;
	}

	if (fmt == DSI_PIXEL_FORMAT_RGB666) {
		packing = of_get_property(of_node,
					  "qcom,mdss-dsi-pixel-packing",
					  NULL);
		if (packing && !strcmp(packing, "loose"))
			fmt = DSI_PIXEL_FORMAT_RGB666_LOOSE;
	}

	host->dst_format = fmt;
	return rc;
}

static int dsi_panel_parse_lane_states(struct dsi_host_common_cfg *host,
				       struct device_node *of_node,
				       const char *name)
{
	int rc = 0;
	bool lane_enabled;

	lane_enabled = of_property_read_bool(of_node,
					    "qcom,mdss-dsi-lane-0-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_0 : 0);

	lane_enabled = of_property_read_bool(of_node,
					     "qcom,mdss-dsi-lane-1-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_1 : 0);

	lane_enabled = of_property_read_bool(of_node,
					    "qcom,mdss-dsi-lane-2-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_2 : 0);

	lane_enabled = of_property_read_bool(of_node,
					     "qcom,mdss-dsi-lane-3-state");
	host->data_lanes |= (lane_enabled ? DSI_DATA_LANE_3 : 0);

	if (host->data_lanes == 0) {
		pr_err("[%s] No data lanes are enabled, rc=%d\n", name, rc);
		rc = -EINVAL;
	}

	return rc;
}

static int dsi_panel_parse_color_swap(struct dsi_host_common_cfg *host,
				      struct device_node *of_node,
				      const char *name)
{
	int rc = 0;
	const char *swap_mode;

	swap_mode = of_get_property(of_node, "qcom,mdss-dsi-color-order", NULL);
	if (swap_mode) {
		if (!strcmp(swap_mode, "rgb_swap_rgb")) {
			host->swap_mode = DSI_COLOR_SWAP_RGB;
		} else if (!strcmp(swap_mode, "rgb_swap_rbg")) {
			host->swap_mode = DSI_COLOR_SWAP_RBG;
		} else if (!strcmp(swap_mode, "rgb_swap_brg")) {
			host->swap_mode = DSI_COLOR_SWAP_BRG;
		} else if (!strcmp(swap_mode, "rgb_swap_grb")) {
			host->swap_mode = DSI_COLOR_SWAP_GRB;
		} else if (!strcmp(swap_mode, "rgb_swap_gbr")) {
			host->swap_mode = DSI_COLOR_SWAP_GBR;
		} else {
			pr_err("[%s] Unrecognized color order-%s\n",
			       name, swap_mode);
			rc = -EINVAL;
		}
	} else {
		pr_debug("[%s] Falling back to default color order\n", name);
		host->swap_mode = DSI_COLOR_SWAP_RGB;
	}

	/* bit swap on color channel is not defined in dt */
	host->bit_swap_red = false;
	host->bit_swap_green = false;
	host->bit_swap_blue = false;
	return rc;
}

static int dsi_panel_parse_triggers(struct dsi_host_common_cfg *host,
				    struct device_node *of_node,
				    const char *name)
{
	const char *trig;
	int rc = 0;

	trig = of_get_property(of_node, "qcom,mdss-dsi-mdp-trigger", NULL);
	if (trig) {
		if (!strcmp(trig, "none")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_NONE;
		} else if (!strcmp(trig, "trigger_te")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_TE;
		} else if (!strcmp(trig, "trigger_sw")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_SW;
		} else if (!strcmp(trig, "trigger_sw_te")) {
			host->mdp_cmd_trigger = DSI_TRIGGER_SW_TE;
		} else {
			pr_err("[%s] Unrecognized mdp trigger type (%s)\n",
			       name, trig);
			rc = -EINVAL;
		}

	} else {
		pr_debug("[%s] Falling back to default MDP trigger\n",
			 name);
		host->mdp_cmd_trigger = DSI_TRIGGER_SW;
	}

	trig = of_get_property(of_node, "qcom,mdss-dsi-dma-trigger", NULL);
	if (trig) {
		if (!strcmp(trig, "none")) {
			host->dma_cmd_trigger = DSI_TRIGGER_NONE;
		} else if (!strcmp(trig, "trigger_te")) {
			host->dma_cmd_trigger = DSI_TRIGGER_TE;
		} else if (!strcmp(trig, "trigger_sw")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW;
		} else if (!strcmp(trig, "trigger_sw_seof")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW_SEOF;
		} else if (!strcmp(trig, "trigger_sw_te")) {
			host->dma_cmd_trigger = DSI_TRIGGER_SW_TE;
		} else {
			pr_err("[%s] Unrecognized mdp trigger type (%s)\n",
			       name, trig);
			rc = -EINVAL;
		}

	} else {
		pr_debug("[%s] Falling back to default MDP trigger\n", name);
		host->dma_cmd_trigger = DSI_TRIGGER_SW;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-te-pin-select",
			&host->te_mode);
	if (rc) {
		pr_warn("[%s] fallback to default te-pin-select\n", name);
		host->te_mode = 1;
		rc = 0;
	}

	return rc;
}

static int dsi_panel_parse_misc_host_config(struct dsi_host_common_cfg *host,
					    struct device_node *of_node,
					    const char *name)
{
	u32 val = 0;
	int rc = 0;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-t-clk-post", &val);
	if (rc) {
		pr_debug("[%s] Fallback to default t_clk_post value\n", name);
		host->t_clk_post = 0x03;
	} else {
		host->t_clk_post = val;
		pr_debug("[%s] t_clk_post = %d\n", name, val);
	}

	val = 0;
	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-t-clk-pre", &val);
	if (rc) {
		pr_debug("[%s] Fallback to default t_clk_pre value\n", name);
		host->t_clk_pre = 0x24;
	} else {
		host->t_clk_pre = val;
		pr_debug("[%s] t_clk_pre = %d\n", name, val);
	}

	host->ignore_rx_eot = of_property_read_bool(of_node,
						"qcom,mdss-dsi-rx-eot-ignore");

	host->append_tx_eot = of_property_read_bool(of_node,
						"qcom,mdss-dsi-tx-eot-append");

	return 0;
}

static int dsi_panel_parse_host_config(struct dsi_panel *panel,
				       struct device_node *of_node)
{
	int rc = 0;

	rc = dsi_panel_parse_pixel_format(&panel->host_config, of_node,
					  panel->name);
	if (rc) {
		pr_err("[%s] failed to get pixel format, rc=%d\n",
		panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_lane_states(&panel->host_config, of_node,
					 panel->name);
	if (rc) {
		pr_err("[%s] failed to parse lane states, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_color_swap(&panel->host_config, of_node,
					panel->name);
	if (rc) {
		pr_err("[%s] failed to parse color swap config, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_triggers(&panel->host_config, of_node,
				      panel->name);
	if (rc) {
		pr_err("[%s] failed to parse triggers, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	rc = dsi_panel_parse_misc_host_config(&panel->host_config, of_node,
					      panel->name);
	if (rc) {
		pr_err("[%s] failed to parse misc host config, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_dfps_caps(struct dsi_dfps_capabilities *dfps_caps,
				     struct device_node *of_node,
				     const char *name)
{
	int rc = 0;
	bool supported = false;
	const char *type;
	u32 val = 0;

	supported = of_property_read_bool(of_node,
					"qcom,mdss-dsi-pan-enable-dynamic-fps");

	if (!supported) {
		pr_debug("[%s] DFPS is not supported\n", name);
		dfps_caps->dfps_support = false;
	} else {

		type = of_get_property(of_node,
				       "qcom,mdss-dsi-pan-fps-update",
				       NULL);
		if (!type) {
			pr_err("[%s] dfps type not defined\n", name);
			rc = -EINVAL;
			goto error;
		} else if (!strcmp(type, "dfps_suspend_resume_mode")) {
			dfps_caps->type = DSI_DFPS_SUSPEND_RESUME;
		} else if (!strcmp(type, "dfps_immediate_clk_mode")) {
			dfps_caps->type = DSI_DFPS_IMMEDIATE_CLK;
		} else if (!strcmp(type, "dfps_immediate_porch_mode_hfp")) {
			dfps_caps->type = DSI_DFPS_IMMEDIATE_HFP;
		} else if (!strcmp(type, "dfps_immediate_porch_mode_vfp")) {
			dfps_caps->type = DSI_DFPS_IMMEDIATE_VFP;
		} else {
			pr_err("[%s] dfps type is not recognized\n", name);
			rc = -EINVAL;
			goto error;
		}

		rc = of_property_read_u32(of_node,
					  "qcom,mdss-dsi-min-refresh-rate",
					  &val);
		if (rc) {
			pr_err("[%s] Min refresh rate is not defined\n", name);
			rc = -EINVAL;
			goto error;
		}
		dfps_caps->min_refresh_rate = val;

		rc = of_property_read_u32(of_node,
					  "qcom,mdss-dsi-max-refresh-rate",
					  &val);
		if (rc) {
			pr_debug("[%s] Using default refresh rate\n", name);
			rc = of_property_read_u32(of_node,
						"qcom,mdss-dsi-panel-framerate",
						&val);
			if (rc) {
				pr_err("[%s] max refresh rate is not defined\n",
				       name);
				rc = -EINVAL;
				goto error;
			}
		}
		dfps_caps->max_refresh_rate = val;

		if (dfps_caps->min_refresh_rate > dfps_caps->max_refresh_rate) {
			pr_err("[%s] min rate > max rate\n", name);
			rc = -EINVAL;
		}

		pr_debug("[%s] DFPS is supported %d-%d, mode %d\n", name,
				dfps_caps->min_refresh_rate,
				dfps_caps->max_refresh_rate,
				dfps_caps->type);
		dfps_caps->dfps_support = true;
	}

error:
	return rc;
}

static int dsi_panel_parse_video_host_config(struct dsi_video_engine_cfg *cfg,
					     struct device_node *of_node,
					     const char *name)
{
	int rc = 0;
	const char *traffic_mode;
	u32 vc_id = 0;
	u32 val = 0;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-h-sync-pulse", &val);
	if (rc) {
		pr_debug("[%s] fallback to default h-sync-pulse\n", name);
		cfg->pulse_mode_hsa_he = false;
	} else if (val == 1) {
		cfg->pulse_mode_hsa_he = true;
	} else if (val == 0) {
		cfg->pulse_mode_hsa_he = false;
	} else {
		pr_err("[%s] Unrecognized value for mdss-dsi-h-sync-pulse\n",
		       name);
		rc = -EINVAL;
		goto error;
	}

	cfg->hfp_lp11_en = of_property_read_bool(of_node,
						"qcom,mdss-dsi-hfp-power-mode");

	cfg->hbp_lp11_en = of_property_read_bool(of_node,
						"qcom,mdss-dsi-hbp-power-mode");

	cfg->hsa_lp11_en = of_property_read_bool(of_node,
						"qcom,mdss-dsi-hsa-power-mode");

	cfg->last_line_interleave_en = of_property_read_bool(of_node,
					"qcom,mdss-dsi-last-line-interleave");

	cfg->eof_bllp_lp11_en = of_property_read_bool(of_node,
					"qcom,mdss-dsi-bllp-eof-power-mode");

	cfg->bllp_lp11_en = of_property_read_bool(of_node,
					"qcom,mdss-dsi-bllp-power-mode");

	traffic_mode = of_get_property(of_node,
				       "qcom,mdss-dsi-traffic-mode",
				       NULL);
	if (!traffic_mode) {
		pr_debug("[%s] Falling back to default traffic mode\n", name);
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_PULSES;
	} else if (!strcmp(traffic_mode, "non_burst_sync_pulse")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_PULSES;
	} else if (!strcmp(traffic_mode, "non_burst_sync_event")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_SYNC_START_EVENTS;
	} else if (!strcmp(traffic_mode, "burst_mode")) {
		cfg->traffic_mode = DSI_VIDEO_TRAFFIC_BURST_MODE;
	} else {
		pr_err("[%s] Unrecognized traffic mode-%s\n", name,
		       traffic_mode);
		rc = -EINVAL;
		goto error;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-virtual-channel-id",
				  &vc_id);
	if (rc) {
		pr_debug("[%s] Fallback to default vc id\n", name);
		cfg->vc_id = 0;
	} else {
		cfg->vc_id = vc_id;
	}

error:
	return rc;
}

static int dsi_panel_parse_cmd_host_config(struct dsi_cmd_engine_cfg *cfg,
					   struct device_node *of_node,
					   const char *name)
{
	u32 val = 0;
	int rc = 0;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-wr-mem-start", &val);
	if (rc) {
		pr_debug("[%s] Fallback to default wr-mem-start\n", name);
		cfg->wr_mem_start = 0x2C;
	} else {
		cfg->wr_mem_start = val;
	}

	val = 0;
	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-wr-mem-continue",
				  &val);
	if (rc) {
		pr_debug("[%s] Fallback to default wr-mem-continue\n", name);
		cfg->wr_mem_continue = 0x3C;
	} else {
		cfg->wr_mem_continue = val;
	}

	/* TODO:  fix following */
	cfg->max_cmd_packets_interleave = 0;

	val = 0;
	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-te-dcs-command",
				  &val);
	if (rc) {
		pr_debug("[%s] fallback to default te-dcs-cmd\n", name);
		cfg->insert_dcs_command = true;
	} else if (val == 1) {
		cfg->insert_dcs_command = true;
	} else if (val == 0) {
		cfg->insert_dcs_command = false;
	} else {
		pr_err("[%s] Unrecognized value for mdss-dsi-te-dcs-command\n",
		       name);
		rc = -EINVAL;
		goto error;
	}

	if (of_property_read_u32(of_node, "qcom,mdss-mdp-transfer-time-us",
				&val)) {
		pr_debug("[%s] Fallback to default transfer-time-us\n", name);
		cfg->mdp_transfer_time_us = DEFAULT_MDP_TRANSFER_TIME;
	} else {
		cfg->mdp_transfer_time_us = val;
	}

error:
	return rc;
}

static int dsi_panel_parse_panel_mode(struct dsi_panel *panel,
				      struct device_node *of_node)
{
	int rc = 0;
	enum dsi_op_mode panel_mode;
	const char *mode;

	mode = of_get_property(of_node, "qcom,mdss-dsi-panel-type", NULL);
	if (!mode) {
		pr_debug("[%s] Fallback to default panel mode\n", panel->name);
		panel_mode = DSI_OP_VIDEO_MODE;
	} else if (!strcmp(mode, "dsi_video_mode")) {
		panel_mode = DSI_OP_VIDEO_MODE;
	} else if (!strcmp(mode, "dsi_cmd_mode")) {
		panel_mode = DSI_OP_CMD_MODE;
	} else {
		pr_err("[%s] Unrecognized panel type-%s\n", panel->name, mode);
		rc = -EINVAL;
		goto error;
	}

	if (panel_mode == DSI_OP_VIDEO_MODE) {
		rc = dsi_panel_parse_video_host_config(&panel->video_config,
						       of_node,
						       panel->name);
		if (rc) {
			pr_err("[%s] Failed to parse video host cfg, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	if (panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_panel_parse_cmd_host_config(&panel->cmd_config,
						     of_node,
						     panel->name);
		if (rc) {
			pr_err("[%s] Failed to parse cmd host config, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	panel->panel_mode = panel_mode;
error:
	return rc;
}

static int dsi_panel_parse_phy_props(struct dsi_panel_phy_props *props,
				     struct device_node *of_node,
				     const char *name)
{
	int rc = 0;
	u32 val = 0;
	const char *str;

	rc = of_property_read_u32(of_node,
				  "qcom,mdss-pan-physical-width-dimension",
				  &val);
	if (rc) {
		pr_debug("[%s] Physical panel width is not defined\n", name);
		props->panel_width_mm = 0;
		rc = 0;
	} else {
		props->panel_width_mm = val;
	}

	rc = of_property_read_u32(of_node,
				  "qcom,mdss-pan-physical-height-dimension",
				  &val);
	if (rc) {
		pr_debug("[%s] Physical panel height is not defined\n", name);
		props->panel_height_mm = 0;
		rc = 0;
	} else {
		props->panel_height_mm = val;
	}

	str = of_get_property(of_node, "qcom,mdss-dsi-panel-orientation", NULL);
	if (!str) {
		props->rotation = DSI_PANEL_ROTATE_NONE;
	} else if (!strcmp(str, "180")) {
		props->rotation = DSI_PANEL_ROTATE_HV_FLIP;
	} else if (!strcmp(str, "hflip")) {
		props->rotation = DSI_PANEL_ROTATE_H_FLIP;
	} else if (!strcmp(str, "vflip")) {
		props->rotation = DSI_PANEL_ROTATE_V_FLIP;
	} else {
		pr_err("[%s] Unrecognized panel rotation-%s\n", name, str);
		rc = -EINVAL;
		goto error;
	}
error:
	return rc;
}
#if defined(CONFIG_DISPLAY_SAMSUNG)
char *cmd_set_prop_map[SS_DSI_CMD_SET_MAX] = {
	"qcom,mdss-dsi-pre-on-command",
	"qcom,mdss-dsi-on-command",
	"qcom,mdss-dsi-post-panel-on-command",
	"qcom,mdss-dsi-pre-off-command",
	"qcom,mdss-dsi-off-command",
	"qcom,mdss-dsi-post-off-command",
	"qcom,mdss-dsi-pre-res-switch",
	"qcom,mdss-dsi-res-switch",
	"qcom,mdss-dsi-post-res-switch",
	"qcom,cmd-to-video-mode-switch-commands",
	"qcom,cmd-to-video-mode-post-switch-commands",
	"qcom,video-to-cmd-mode-switch-commands",
	"qcom,video-to-cmd-mode-post-switch-commands",
	"qcom,mdss-dsi-panel-status-command",
	"qcom,mdss-dsi-lp1-command",
	"qcom,mdss-dsi-lp2-command",
	"qcom,mdss-dsi-nolp-command",
	"PPS not parsed from DTSI, generated dynamically",
	"ROI not parsed from DTSI, generated dynamically",
	"qcom,mdss-dsi-timing-switch-command",
	"qcom,mdss-dsi-post-mode-switch-on-command",
	"DSI_CMD_SET_MAX not parsed from DTSI",

	/* prop map for samsung display driver
	 * Please property in proper boundary (TX, RX)
	 */

	/* samsung feature */
	"SS_DSI_CMD_SET_START not parsed from DTSI",

	/* TX */
	"TX_CMD_START not parsed from DTSI",
	"samsung,temp_dsc_tx_cmds_revA",
	"samsung,display_on_tx_cmds_revA",
	"samsung,display_off_tx_cmds_revA",
	"samsung,brightness_tx_cmds_revA",
	"samsung,manufacture_read_pre_tx_cmds_revA",
	"samsung,level0_key_enable_tx_cmds_revA",
	"samsung,level0_key_disable_tx_cmds_revA",
	"samsung,level1_key_enable_tx_cmds_revA",
	"samsung,level1_key_disable_tx_cmds_revA",
	"samsung,level2_key_enable_tx_cmds_revA",
	"samsung,level2_key_disable_tx_cmds_revA",
	"TX_MDNIE_ADB_TEST not parsed from DTSI",
	"samsung,lpm_on_tx_cmds_revA",
	"samsung,lpm_off_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_aod_on_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_aod_off_tx_cmds_revA",
	"samsung,lpm_2nit_tx_cmds_revA",
	"samsung,lpm_10nit_tx_cmds_revA",
	"samsung,lpm_30nit_tx_cmds_revA",
	"samsung,lpm_40nit_tx_cmds_revA",
	"samsung,lpm_60nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_2nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_10nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_30nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_40nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_60nit_tx_cmds_revA",
	"samsung,lpm_ctrl_alpm_off_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_2nit_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_10nit_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_30nit_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_40nit_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_60nit_tx_cmds_revA",
	"samsung,lpm_ctrl_hlpm_off_tx_cmds_revA",
	"samsung,lpm_brightnes_tx_cmds_revA",
	"samsung,packet_size_tx_cmds_revA",
	"samsung,reg_read_pos_tx_cmds_revA",
	"samsung,mdnie_tx_cmds_revA",
	"samsung,osc_te_fitting_tx_cmds_revA",
	"samsung,avc_on_revA",
	"samsung,ldi_fps_change_tx_cmds_revA",
	"samsung,hmt_enable_tx_cmds_revA",
	"samsung,hmt_disable_tx_cmds_revA",
	"TX_HMT_LOW_PERSISTENCE_OFF_BRIGHT not parsed from DTSI",
	"samsung,hmt_reverse_tx_cmds_revA",
	"samsung,hmt_forward_tx_cmds_revA",
	"samsung,ffc_tx_cmds_revA",
	"samsung,dyn_mipi_clk_ffc_cmds_revA",
	"samsung,cabc_on_tx_cmds_revA",
	"samsung,cabc_off_tx_cmds_revA",
	"samsung,tft_pwm_tx_cmds_revA",
	"samsung,blic_dimming_cmds_revA",
	"samsung,panel_ldi_vdd_offset_write_cmds_revA",
	"samsung,panel_ldi_vddm_offset_write_cmds_revA",
	"samsung,hsync_on_tx_cmds_revA",
	"samsung,cabc_on_duty_tx_cmds_revA",
	"samsung,cabc_off_duty_tx_cmds_revA",
	"samsung,copr_enable_tx_cmds_revA",
	"samsung,copr_disable_tx_cmds_revA",
	"samsung,ccb_on_tx_cmds_revA",
	"samsung,ccb_off_tx_cmds_revA",
	"samsung,esd_recovery_1_tx_cmds_revA",
	"samsung,esd_recovery_2_tx_cmds_revA",
	"samsung,mcd_on_tx_cmds_revA",
	"samsung,mcd_off_tx_cmds_revA",
	"samsung,gradual_acl_tx_cmds_revA",
	"samsung,hw_cursor_tx_cmds_revA",
	"samsung,panel_multires_fhd_to_wqhd_revA",
	"samsung,panel_multires_hd_to_wqhd_revA",
	"samsung,panel_multires_fhd_revA",
	"samsung,panel_multires_hd_revA",
	"samsung,panel_cover_control_enable_cmds_revA",
	"samsung,panel_cover_control_disable_cmds_revA",
	"samsung,hbm_gamma_tx_cmds_revA",
	"samsung,hbm_etc_tx_cmds_revA",
	"samsung,hbm_irc_tx_cmds_revA",
	"samsung,hbm_off_tx_cmds_revA",
	"samsung,aid_tx_cmds_revA",
	"samsung,aid_subdivision_tx_cmds_revA",
	"samsung,pac_aid_subdivision_tx_cmds_revA",
	"samsung,acl_on_tx_cmds_revA",
	"samsung,acl_off_tx_cmds_revA",
	"samsung,elvss_tx_cmds_revA",
	"samsung,elvss_pre_tx_cmds_revA",
	"samsung,gamma_tx_cmds_revA",
	"samsung,hmt_elvss_tx_cmds_revA",
	"samsung,hmt_vint_tx_cmds_revA",
	"samsung,hmt_gamma_tx_cmds_revA",
	"samsung,hmt_aid_tx_cmds_revA",
	"samsung,elvss_lowtemp_tx_cmds_revA",
	"samsung,elvss_lowtemp2_tx_cmds_revA",
	"samsung,smart_acl_elvss_tx_cmds_revA",
	"samsung,smart_acl_elvss_lowtemp_tx_cmds_revA",
	"samsung,vint_tx_cmds_revA",
	"samsung,irc_tx_cmds_revA",
	"samsung,irc_subdivision_tx_cmds_revA",
	"samsung,pac_irc_subdivision_tx_cmds_revA",
	"samsung,irc_off_tx_cmds_revA",
	"samsung,micro_short_test_on_tx_cmds_revA",
	"samsung,micro_short_test_off_tx_cmds_revA",
	"TX_POC_CMD_START not parsed from DTSI",
	"samsung,poc_write_1byte_tx_cmds_revA",
	"samsung,poc_erase_tx_cmds_revA",
	"samsung,poc_erase1_tx_cmds_revA",
	"samsung,poc_pre_write_tx_cmds_revA",
	"samsung,poc_write_continue_tx_cmds_revA",
	"samsung,poc_write_continue2_tx_cmds_revA",
	"samsung,poc_write_continue3_tx_cmds_revA",
	"samsung,poc_write_end_tx_cmds_revA",
	"samsung,poc_post_write_tx_cmds_revA",
	"samsung,poc_pre_read_tx_cmds_revA",
	"samsung,poc_read_tx_cmds_revA",
	"samsung,poc_post_read_tx_cmds_revA",
	"samsung,reg_poc_read_pos_tx_cmds_revA",
	"TX_POC_CMD_END not parsed from DTSI",
	"samsung,gct_enter_tx_cmds_revA",
	"samsung,gct_mid_tx_cmds_revA",
	"samsung,gct_exit_tx_cmds_revA",
	"TX_DDI_RAM_IMG_DATA not parsed from DTSI",
	"samsung,gray_spot_test_on_tx_cmds_revA",
	"samsung,gray_spot_test_off_tx_cmds_revA",

	/* self display */
	"TX_SELF_DISP_CMD_START not parsed from DTSI",
	"samsung,self_dispaly_on",
	"samsung,self_dispaly_off",
	"samsung,self_time_set",
	"samsung,self_move_on_100",
	"samsung,self_move_on_200",
	"samsung,self_move_on_500",
	"samsung,self_move_on_1000",
	"samsung,self_move_on_debug",
	"samsung,self_move_reset",
	"samsung,self_move_2c_sync_off",
	"samsung,self_mask_mem_setting",
	"samsung,self_mask_on",
	"samsung,self_mask_off",
	"TX_SELF_MASK_IMAGE not parsed from DTSI",
	"samsung,self_icon_mem_setting",
	"samsung,self_icon_grid",
	"samsung,self_icon_on_grid_on",
	"samsung,self_icon_on_grid_off",
	"samsung,self_icon_off_grid_on",
	"samsung,self_icon_off_grid_off",
	"samsung,self_icon_grid_2c_sync_off",
	"TX_SELF_ICON_IMAGE not parsed from DTSI",
	"samsung,self_brightness_icon_on",
	"samsung,self_brightness_icon_off",
	"samsung,self_aclock_sidemem_setting",
	"samsung,self_aclock_on",
	"samsung,self_aclock_time_update",
	"samsung,self_aclock_rotation",
	"samsung,self_aclock_off",
	"samsung,self_aclock_hide",
	"TX_SELF_ACLOCK_IMAGE not parsed from DTSI",
	"samsung,self_dclock_sidemem_setting",
	"samsung,self_dclock_on",
	"samsung,self_dclock_blinking_on",
	"samsung,self_dclock_blinking_off",
	"samsung,self_dclock_time_update",
	"samsung,self_dclock_off",
	"samsung,self_dclock_hide",
	"TX_SELF_DCLOCK_IMAGE not parsed from DTSI",
	"samsung,self_clock_2c_sync_off",
	"TX_SELF_VIDEO_IMAGE not parsed from DTSI",
	"samsung,self_video_mem_setting",
	"samsung,self_video_on",
	"samsung,self_video_of",
	"samsung,self_disp_debug_rx_cmds",
	"TX_SELF_DISP_CMD_END not parsed from DTSI",
	"TX_CMD_END not parsed from DTSI",

	/* RX */
	"RX_CMD_START not parsed from DTSI",
	"samsung,smart_dimming_mtp_rx_cmds_revA",
	"samsung,manufacture_id_rx_cmds_revA",
	"samsung,manufacture_id0_rx_cmds_revA",
	"samsung,manufacture_id1_rx_cmds_revA",
	"samsung,manufacture_id2_rx_cmds_revA",
	"samsung,manufacture_date_rx_cmds_revA",
	"samsung,ddi_id_rx_cmds_revA",
	"samsung,cell_id_rx_cmds_revA",
	"samsung,octa_id_rx_cmds_revA",
	"samsung,rddpm_rx_cmds_revA",
	"samsung,mtp_read_sysfs_rx_cmds_revA",
	"samsung,elvss_rx_cmds_revA",
	"samsung,irc_rx_cmds_revA",
	"samsung,hbm_rx_cmds_revA",
	"samsung,hbm2_rx_cmds_revA",
	"samsung,mdnie_read_rx_cmds_revA",
	"samsung,ldi_debug0_rx_cmds_revA",
	"samsung,ldi_debug1_rx_cmds_revA",
	"samsung,ldi_debug2_rx_cmds_revA",
	"samsung,ldi_debug3_rx_cmds_revA",
	"samsung,ldi_debug4_rx_cmds_revA",
	"samsung,ldi_debug5_rx_cmds_revA",
	"samsung,ldi_loading_det_rx_cmds_revA",
	"samsung,ldi_fps_rx_cmds_revA",
	"samsung,poc_read_rx_cmds_revA",
	"samsung,poc_status_rx_cmds_revA",
	"samsung,poc_checksum_rx_cmds_revA",
	"samsung,gct_checksum_rx_cmds_revA",
	"RX_CMD_END not parsed from DTSI",
};
#else	/* #if defined(CONFIG_DISPLAY_SAMSUNG) */
const char *cmd_set_prop_map[DSI_CMD_SET_MAX] = {
	"qcom,mdss-dsi-pre-on-command",
	"qcom,mdss-dsi-on-command",
	"qcom,mdss-dsi-post-panel-on-command",
	"qcom,mdss-dsi-pre-off-command",
	"qcom,mdss-dsi-off-command",
	"qcom,mdss-dsi-post-off-command",
	"qcom,mdss-dsi-pre-res-switch",
	"qcom,mdss-dsi-res-switch",
	"qcom,mdss-dsi-post-res-switch",
	"qcom,cmd-to-video-mode-switch-commands",
	"qcom,cmd-to-video-mode-post-switch-commands",
	"qcom,video-to-cmd-mode-switch-commands",
	"qcom,video-to-cmd-mode-post-switch-commands",
	"qcom,mdss-dsi-panel-status-command",
	"qcom,mdss-dsi-lp1-command",
	"qcom,mdss-dsi-lp2-command",
	"qcom,mdss-dsi-nolp-command",
	"PPS not parsed from DTSI, generated dynamically",
	"ROI not parsed from DTSI, generated dynamically",
	"qcom,mdss-dsi-timing-switch-command",
	"qcom,mdss-dsi-post-mode-switch-on-command",
};
#endif

const char *cmd_set_state_map[DSI_CMD_SET_MAX] = {
	"qcom,mdss-dsi-pre-on-command-state",
	"qcom,mdss-dsi-on-command-state",
	"qcom,mdss-dsi-post-on-command-state",
	"qcom,mdss-dsi-pre-off-command-state",
	"qcom,mdss-dsi-off-command-state",
	"qcom,mdss-dsi-post-off-command-state",
	"qcom,mdss-dsi-pre-res-switch-state",
	"qcom,mdss-dsi-res-switch-state",
	"qcom,mdss-dsi-post-res-switch-state",
	"qcom,cmd-to-video-mode-switch-commands-state",
	"qcom,cmd-to-video-mode-post-switch-commands-state",
	"qcom,video-to-cmd-mode-switch-commands-state",
	"qcom,video-to-cmd-mode-post-switch-commands-state",
	"qcom,mdss-dsi-panel-status-command-state",
	"qcom,mdss-dsi-lp1-command-state",
	"qcom,mdss-dsi-lp2-command-state",
	"qcom,mdss-dsi-nolp-command-state",
	"PPS not parsed from DTSI, generated dynamically",
	"ROI not parsed from DTSI, generated dynamically",
	"qcom,mdss-dsi-timing-switch-command-state",
	"qcom,mdss-dsi-post-mode-switch-on-command-state",
};

static int dsi_panel_get_cmd_pkt_count(const char *data, u32 length, u32 *cnt)
{
	const u32 cmd_set_min_size = 7;
	u32 count = 0;
	u32 packet_length;
	u32 tmp;

	while (length >= cmd_set_min_size) {
		packet_length = cmd_set_min_size;
		tmp = ((data[5] << 8) | (data[6]));
		packet_length += tmp;
		if (packet_length > length) {
			pr_err("format error\n");
			return -EINVAL;
		}
		length -= packet_length;
		data += packet_length;
		count++;
	};

	*cnt = count;
	return 0;
}

static int dsi_panel_create_cmd_packets(const char *data,
					u32 length,
					u32 count,
					struct dsi_cmd_desc *cmd)
{
	int rc = 0;
	int i, j;
	u8 *payload;

	for (i = 0; i < count; i++) {
		u32 size;

		cmd[i].msg.type = data[0];
		cmd[i].last_command = (data[1] == 1 ? true : false);
		cmd[i].msg.channel = data[2];
		cmd[i].msg.flags |= (data[3] == 1 ? MIPI_DSI_MSG_REQ_ACK : 0);
		cmd[i].msg.ctrl = 0;
		cmd[i].post_wait_ms = data[4];
		cmd[i].msg.tx_len = ((data[5] << 8) | (data[6]));

		size = cmd[i].msg.tx_len * sizeof(u8);

		payload = kzalloc(size, GFP_KERNEL);
		if (!payload) {
			rc = -ENOMEM;
			goto error_free_payloads;
		}

		for (j = 0; j < cmd[i].msg.tx_len; j++)
			payload[j] = data[7 + j];

		cmd[i].msg.tx_buf = payload;
		data += (7 + cmd[i].msg.tx_len);
	}

	return rc;
error_free_payloads:
	for (i = i - 1; i >= 0; i--) {
		cmd--;
		kfree(cmd->msg.tx_buf);
	}

	return rc;
}

void dsi_panel_destroy_cmd_packets(struct dsi_panel_cmd_set *set)
{
	u32 i = 0;
	struct dsi_cmd_desc *cmd;

	for (i = 0; i < set->count; i++) {
		cmd = &set->cmds[i];
		kfree(cmd->msg.tx_buf);
	}

	kfree(set->cmds);
}

static int dsi_panel_alloc_cmd_packets(struct dsi_panel_cmd_set *cmd,
					u32 packet_count)
{
	u32 size;

	size = packet_count * sizeof(*cmd->cmds);
	cmd->cmds = kzalloc(size, GFP_KERNEL);
	if (!cmd->cmds)
		return -ENOMEM;

	cmd->count = packet_count;
	return 0;
}

static int dsi_panel_parse_cmd_sets_sub(struct dsi_panel_cmd_set *cmd,
					enum dsi_cmd_set_type type,
					struct device_node *of_node)
{
	int rc = 0;
	u32 length = 0;
	const char *data;
	const char *state;
	u32 packet_count = 0;

	data = of_get_property(of_node, cmd_set_prop_map[type], &length);
	if (!data) {
		pr_debug("%s commands not defined\n", cmd_set_prop_map[type]);
		rc = -ENOTSUPP;
		goto error;
	}

	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc) {
		pr_err("commands failed, rc=%d\n", rc);
		goto error;
	}
	pr_debug("[%s] packet-count=%d, %d\n", cmd_set_prop_map[type],
		packet_count, length);

	rc = dsi_panel_alloc_cmd_packets(cmd, packet_count);
	if (rc) {
		pr_err("failed to allocate cmd packets, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_create_cmd_packets(data, length, packet_count,
					  cmd->cmds);
	if (rc) {
		pr_err("failed to create cmd packets, rc=%d\n", rc);
		goto error_free_mem;
	}

#if defined(CONFIG_DISPLAY_SAMSUNG)
	/* samsung rx */
	if (ss_is_read_cmd(type)) {
		cmd->state = DSI_CMD_SET_STATE_LP;
		cmd->cmds[0].msg.rx_len = data[8];
		cmd->read_startoffset = data[9];
	}

	/* samsung commands are set HS mode as default
	 * without cmd_set_state_map
	 */
	if (type >= SS_DSI_CMD_SET_START) {
		cmd->state = DSI_CMD_SET_STATE_HS;
		return rc;
	}
#endif

	state = of_get_property(of_node, cmd_set_state_map[type], NULL);
	if (!state || !strcmp(state, "dsi_lp_mode")) {
		cmd->state = DSI_CMD_SET_STATE_LP;
	} else if (!strcmp(state, "dsi_hs_mode")) {
		cmd->state = DSI_CMD_SET_STATE_HS;
	} else {
		pr_err("[%s] command state unrecognized-%s\n",
		       cmd_set_state_map[type], state);
		goto error_free_mem;
	}

	return rc;
error_free_mem:
	kfree(cmd->cmds);
	cmd->cmds = NULL;
error:
	return rc;

}

#if defined(CONFIG_DISPLAY_SAMSUNG)
/* ss_dsi_panel_parse_cmd_sets_sub:
 * parse and save samsung mipi commands for each panel revision.
 * If the panel revision has no dtsi data,
 * set pointer to previous panel revision dtsi data.
 */
static int ss_dsi_panel_parse_cmd_sets_sub(struct dsi_panel_cmd_set *cmd,
					enum dsi_cmd_set_type type,
					struct device_node *of_node)
{
	int rc = 0;
	int rev;
	char *map;
	struct dsi_panel_cmd_set *set_rev[SUPPORT_PANEL_REVISION];
	int len;
	char org_val;

	/* For revA */
	cmd->cmd_set_rev[0] = cmd;

	/* For revB ~ revT */
	map = cmd_set_prop_map[type];
	len = strlen(cmd_set_prop_map[type]);

	org_val = map[len - 1];

	for (rev = 1; rev < SUPPORT_PANEL_REVISION; rev++) {
		set_rev[rev] = kzalloc(sizeof(struct dsi_panel_cmd_set),
				GFP_KERNEL);
		if (map[len - 1] >= 'A' && map[len - 1] <= 'Z')
			map[len - 1] = 'A' + rev;

		rc = dsi_panel_parse_cmd_sets_sub(set_rev[rev], type, of_node);
		if (rc) {
			/* If there is no data for the panel rev,
			 * copy previous panel rev data pointer.
			 */
			kfree(set_rev[rev]);
			cmd->cmd_set_rev[rev] = cmd->cmd_set_rev[rev - 1];
		} else {
			cmd->cmd_set_rev[rev] = set_rev[rev];
		}
	}

	/* cmd_set_prop_map will be used only for debugging log.
	 * cmd_set_prop_map variable is located in ro. area.
	 * PMK feature checks ro data between vmlinux and runtime kernel ram,
	 * and reports error if thoes have different value.
	 * Reset revision value to original value to make same value in vmlinux.
	 */
	if (map[len - 1] >= 'A' && map[len - 1] <= 'Z')
		map[len - 1] = org_val;

	return rc;
}

int ss_dsi_panel_parse_cmd_sets(struct dsi_panel_cmd_set *cmd_sets,
		struct device_node *of_node)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set;
	u32 i;
	struct device_node *node = NULL;
	struct device_node *self_display_node = of_parse_phandle(of_node,
					   "ss,self_display", 0);

	for (i = SS_DSI_CMD_SET_START; i < SS_DSI_CMD_SET_MAX; i++) {
		set = &cmd_sets[i];
		set->type = i;
		set->count = 0;

		/* Self display has different device node */
		if (i >= TX_SELF_DISP_CMD_START && i <= TX_SELF_DISP_CMD_END)
			node = self_display_node;
		else
			node = of_node;

		rc = dsi_panel_parse_cmd_sets_sub(set, i, node);
		if (rc)
			pr_debug("failed to parse set %d\n", i);

		ss_dsi_panel_parse_cmd_sets_sub(set, i, node);
	}

	rc = 0;
	return rc;
}

#endif  /* #if defined(CONFIG_DISPLAY_SAMSUNG)*/

static int dsi_panel_parse_cmd_sets(
		struct dsi_display_mode_priv_info *priv_info,
		struct device_node *of_node)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set;
	u32 i;

	if (!priv_info) {
		pr_err("invalid mode priv info\n");
		return -EINVAL;
	}

	for (i = DSI_CMD_SET_PRE_ON; i < DSI_CMD_SET_MAX; i++) {
		set = &priv_info->cmd_sets[i];
		set->type = i;
		set->count = 0;

		if (i == DSI_CMD_SET_PPS) {
			rc = dsi_panel_alloc_cmd_packets(set, 1);
			if (rc)
				pr_err("failed to allocate cmd set %d, rc = %d\n",
					i, rc);
			set->state = DSI_CMD_SET_STATE_LP;
		} else {
			rc = dsi_panel_parse_cmd_sets_sub(set, i, of_node);
			if (rc)
				pr_debug("failed to parse set %d\n", i);
		}
	}

	rc = 0;
	return rc;
}

static int dsi_panel_parse_reset_sequence(struct dsi_panel *panel,
				      struct device_node *of_node)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_reset_seq *seq;

	arr = of_get_property(of_node, "qcom,mdss-dsi-reset-sequence", &length);
	if (!arr) {
		pr_err("[%s] dsi-reset-sequence not found\n", panel->name);
		rc = -EINVAL;
		goto error;
	}
	if (length & 0x1) {
		pr_err("[%s] syntax error for dsi-reset-sequence\n",
		       panel->name);
		rc = -EINVAL;
		goto error;
	}

	pr_err("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);

	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = of_property_read_u32_array(of_node, "qcom,mdss-dsi-reset-sequence",
					arr_32, length);
	if (rc) {
		pr_err("[%s] cannot read dso-reset-seqience\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->reset_config.sequence = seq;
	panel->reset_config.count = count;

	for (i = 0; i < length; i += 2) {
		seq->level = arr_32[i];
		seq->sleep_ms = arr_32[i + 1];
		seq++;
	}


error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

static int dsi_panel_parse_misc_features(struct dsi_panel *panel,
				     struct device_node *of_node)
{
	panel->ulps_enabled =
		of_property_read_bool(of_node, "qcom,ulps-enabled");

	if (panel->ulps_enabled)
		pr_debug("ulps_enabled:%d\n", panel->ulps_enabled);

	panel->te_using_watchdog_timer = of_property_read_bool(of_node,
					"qcom,mdss-dsi-te-using-wd");

	panel->sync_broadcast_en = of_property_read_bool(of_node,
			"qcom,cmd-sync-wait-broadcast");
	return 0;
}

static int dsi_panel_parse_jitter_config(
				struct dsi_display_mode *mode,
				struct device_node *of_node)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	u32 jitter[DEFAULT_PANEL_JITTER_ARRAY_SIZE] = {0, 0};
	u64 jitter_val = 0;

	priv_info = mode->priv_info;

	rc = of_property_read_u32_array(of_node, "qcom,mdss-dsi-panel-jitter",
				jitter, DEFAULT_PANEL_JITTER_ARRAY_SIZE);
	if (rc) {
		pr_debug("panel jitter not defined rc=%d\n", rc);
	} else {
		jitter_val = jitter[0];
		jitter_val = div_u64(jitter_val, jitter[1]);
	}

	if (rc || !jitter_val || (jitter_val > MAX_PANEL_JITTER)) {
		priv_info->panel_jitter_numer = DEFAULT_PANEL_JITTER_NUMERATOR;
		priv_info->panel_jitter_denom =
					DEFAULT_PANEL_JITTER_DENOMINATOR;
	} else {
		priv_info->panel_jitter_numer = jitter[0];
		priv_info->panel_jitter_denom = jitter[1];
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-panel-prefill-lines",
				  &priv_info->panel_prefill_lines);
	if (rc) {
		pr_debug("panel prefill lines are not defined rc=%d\n", rc);
		priv_info->panel_prefill_lines = DEFAULT_PANEL_PREFILL_LINES;
	} else if (priv_info->panel_prefill_lines >=
					DSI_V_TOTAL(&mode->timing)) {
		pr_debug("invalid prefill lines config=%d setting to:%d\n",
		priv_info->panel_prefill_lines, DEFAULT_PANEL_PREFILL_LINES);

		priv_info->panel_prefill_lines = DEFAULT_PANEL_PREFILL_LINES;
	}

	return 0;
}

static int dsi_panel_parse_power_cfg(struct device *parent,
				     struct dsi_panel *panel,
				     struct device_node *of_node)
{
	int rc = 0;

	rc = dsi_pwr_of_get_vreg_data(of_node,
					  &panel->power_info,
					  "qcom,panel-supply-entries");
	if (rc) {
		pr_err("[%s] failed to parse vregs\n", panel->name);
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_gpios(struct dsi_panel *panel,
				 struct device_node *of_node)
{
	int rc = 0;
	const char *data;

	panel->reset_config.reset_gpio = of_get_named_gpio(of_node,
					      "qcom,platform-reset-gpio",
					      0);
	if (!gpio_is_valid(panel->reset_config.reset_gpio)) {
		pr_err("[%s] failed get reset gpio, rc=%d\n", panel->name, rc);
		rc = -EINVAL;
		goto error;
	}

	panel->reset_config.disp_en_gpio = of_get_named_gpio(of_node,
						"qcom,5v-boost-gpio",
						0);
	if (!gpio_is_valid(panel->reset_config.disp_en_gpio)) {
		pr_debug("[%s] 5v-boot-gpio is not set, rc=%d\n",
			 panel->name, rc);
		panel->reset_config.disp_en_gpio = of_get_named_gpio(of_node,
							"qcom,platform-en-gpio",
							0);
		if (!gpio_is_valid(panel->reset_config.disp_en_gpio)) {
			pr_debug("[%s] platform-en-gpio is not set, rc=%d\n",
				 panel->name, rc);
		}
	}

	panel->reset_config.lcd_mode_sel_gpio = of_get_named_gpio(of_node,
		"qcom,panel-mode-gpio", 0);
	if (!gpio_is_valid(panel->reset_config.lcd_mode_sel_gpio))
		pr_debug("%s:%d mode gpio not specified\n", __func__, __LINE__);

	data = of_get_property(of_node,
		"qcom,mdss-dsi-mode-sel-gpio-state", NULL);
	if (data) {
		if (!strcmp(data, "single_port"))
			panel->reset_config.mode_sel_state =
				MODE_SEL_SINGLE_PORT;
		else if (!strcmp(data, "dual_port"))
			panel->reset_config.mode_sel_state =
				MODE_SEL_DUAL_PORT;
		else if (!strcmp(data, "high"))
			panel->reset_config.mode_sel_state =
				MODE_GPIO_HIGH;
		else if (!strcmp(data, "low"))
			panel->reset_config.mode_sel_state =
				MODE_GPIO_LOW;
	} else {
		/* Set default mode as SPLIT mode */
		panel->reset_config.mode_sel_state = MODE_SEL_DUAL_PORT;
	}

	/* TODO:  release memory */
	rc = dsi_panel_parse_reset_sequence(panel, of_node);
	if (rc) {
		pr_err("[%s] failed to parse reset sequence, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_bl_pwm_config(struct dsi_backlight_config *config,
					 struct device_node *of_node)
{
	int rc = 0;
	u32 val;

	rc = of_property_read_u32(of_node, "qcom,dsi-bl-pmic-bank-select",
				  &val);
	if (rc) {
		pr_err("bl-pmic-bank-select is not defined, rc=%d\n", rc);
		goto error;
	}
	config->pwm_pmic_bank = val;

	rc = of_property_read_u32(of_node, "qcom,dsi-bl-pmic-pwm-frequency",
				  &val);
	if (rc) {
		pr_err("bl-pmic-bank-select is not defined, rc=%d\n", rc);
		goto error;
	}
	config->pwm_period_usecs = val;

	config->pwm_pmi_control = of_property_read_bool(of_node,
						"qcom,mdss-dsi-bl-pwm-pmi");

	config->pwm_gpio = of_get_named_gpio(of_node,
					     "qcom,mdss-dsi-pwm-gpio",
					     0);
	if (!gpio_is_valid(config->pwm_gpio)) {
		pr_err("pwm gpio is invalid\n");
		rc = -EINVAL;
		goto error;
	}

error:
	return rc;
}

static int dsi_panel_parse_bl_config(struct dsi_panel *panel,
				     struct device_node *of_node)
{
	int rc = 0;
	const char *bl_type;
	u32 val = 0;

	bl_type = of_get_property(of_node,
				  "qcom,mdss-dsi-bl-pmic-control-type",
				  NULL);
	if (!bl_type) {
		panel->bl_config.type = DSI_BACKLIGHT_UNKNOWN;
	} else if (!strcmp(bl_type, "bl_ctrl_pwm")) {
		panel->bl_config.type = DSI_BACKLIGHT_PWM;
	} else if (!strcmp(bl_type, "bl_ctrl_wled")) {
		panel->bl_config.type = DSI_BACKLIGHT_WLED;
	} else if (!strcmp(bl_type, "bl_ctrl_dcs")) {
		panel->bl_config.type = DSI_BACKLIGHT_DCS;
	} else {
		pr_debug("[%s] bl-pmic-control-type unknown-%s\n",
			 panel->name, bl_type);
		panel->bl_config.type = DSI_BACKLIGHT_UNKNOWN;
	}

	panel->bl_config.bl_scale = MAX_BL_SCALE_LEVEL;
	panel->bl_config.bl_scale_ad = MAX_AD_BL_SCALE_LEVEL;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-bl-min-level", &val);
	if (rc) {
		pr_debug("[%s] bl-min-level unspecified, defaulting to zero\n",
			 panel->name);
		panel->bl_config.bl_min_level = 0;
	} else {
		panel->bl_config.bl_min_level = val;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsi-bl-max-level", &val);
	if (rc) {
		pr_debug("[%s] bl-max-level unspecified, defaulting to max level\n",
			 panel->name);
		panel->bl_config.bl_max_level = MAX_BL_LEVEL;
	} else {
		panel->bl_config.bl_max_level = val;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-brightness-max-level",
		&val);
	if (rc) {
		pr_debug("[%s] brigheness-max-level unspecified, defaulting to 255\n",
			 panel->name);
		panel->bl_config.brightness_max_level = 255;
	} else {
		panel->bl_config.brightness_max_level = val;
	}

#if defined(CONFIG_DISPLAY_SAMSUNG)
	rc = of_property_read_u32(of_node, "qcom,mdss-brightness-default-level",
		&val);
	if (rc) {
		pr_debug("[%s] brigheness-default-level unspecified, defaulting to 25500\n",
			 panel->name);
		panel->bl_config.bl_level = 25500;
	} else {
		panel->bl_config.bl_level = val;
	}
#endif

	if (panel->bl_config.type == DSI_BACKLIGHT_PWM) {
		rc = dsi_panel_parse_bl_pwm_config(&panel->bl_config, of_node);
		if (rc) {
			pr_err("[%s] failed to parse pwm config, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}

	panel->bl_config.en_gpio = of_get_named_gpio(of_node,
					      "qcom,platform-bklight-en-gpio",
					      0);
	if (!gpio_is_valid(panel->bl_config.en_gpio)) {
		pr_debug("[%s] failed get bklt gpio, rc=%d\n", panel->name, rc);
		rc = 0;
		goto error;
	}

error:
	return rc;
}

void dsi_dsc_pclk_param_calc(struct msm_display_dsc_info *dsc, int intf_width)
{
	int slice_per_pkt, slice_per_intf;
	int bytes_in_slice, total_bytes_per_intf;

	if (!dsc || !dsc->slice_width || !dsc->slice_per_pkt ||
	    (intf_width < dsc->slice_width)) {
		pr_err("invalid input, intf_width=%d slice_width=%d\n",
			intf_width, dsc ? dsc->slice_width : -1);
		return;
	}

	slice_per_pkt = dsc->slice_per_pkt;
	slice_per_intf = DIV_ROUND_UP(intf_width, dsc->slice_width);

	/*
	 * If slice_per_pkt is greater than slice_per_intf then default to 1.
	 * This can happen during partial update.
	 */
	if (slice_per_pkt > slice_per_intf)
		slice_per_pkt = 1;

	bytes_in_slice = DIV_ROUND_UP(dsc->slice_width * dsc->bpp, 8);
	total_bytes_per_intf = bytes_in_slice * slice_per_intf;

	dsc->eol_byte_num = total_bytes_per_intf % 3;
	dsc->pclk_per_line =  DIV_ROUND_UP(total_bytes_per_intf, 3);
	dsc->bytes_in_slice = bytes_in_slice;
	dsc->bytes_per_pkt = bytes_in_slice * slice_per_pkt;
	dsc->pkt_per_line = slice_per_intf / slice_per_pkt;
}


int dsi_dsc_populate_static_param(struct msm_display_dsc_info *dsc)
{
	int bpp, bpc;
	int mux_words_size;
	int groups_per_line, groups_total;
	int min_rate_buffer_size;
	int hrd_delay;
	int pre_num_extra_mux_bits, num_extra_mux_bits;
	int slice_bits;
	int target_bpp_x16;
	int data;
	int final_value, final_scale;
	int ratio_index;

	dsc->version = 0x11;
	dsc->scr_rev = 0;
	dsc->rc_model_size = 8192;
	if (dsc->version == 0x11 && dsc->scr_rev == 0x1)
		dsc->first_line_bpg_offset = 15;
	else
		dsc->first_line_bpg_offset = 12;

	dsc->edge_factor = 6;
	dsc->tgt_offset_hi = 3;
	dsc->tgt_offset_lo = 3;
	dsc->enable_422 = 0;
	dsc->convert_rgb = 1;
	dsc->vbr_enable = 0;

	dsc->buf_thresh = dsi_dsc_rc_buf_thresh;

	bpp = dsc->bpp;
	bpc = dsc->bpc;

	if (bpc == 12)
		ratio_index = DSC_12BPC_8BPP;
	else if (bpc == 10)
		ratio_index = DSC_10BPC_8BPP;
	else
		ratio_index = DSC_8BPC_8BPP;

	if (dsc->version == 0x11 && dsc->scr_rev == 0x1) {
		dsc->range_min_qp =
			dsi_dsc_rc_range_min_qp_1_1_scr1[ratio_index];
		dsc->range_max_qp =
			dsi_dsc_rc_range_max_qp_1_1_scr1[ratio_index];
	} else {
		dsc->range_min_qp = dsi_dsc_rc_range_min_qp_1_1[ratio_index];
		dsc->range_max_qp = dsi_dsc_rc_range_max_qp_1_1[ratio_index];
	}
	dsc->range_bpg_offset = dsi_dsc_rc_range_bpg_offset;

	if (bpp == 8)
		dsc->initial_offset = 6144;
	else
		dsc->initial_offset = 2048;	/* bpp = 12 */

	if (bpc == 12)
		mux_words_size = 64;
	else
		mux_words_size = 48;		/* bpc == 8/10 */

	if (bpc == 8) {
		dsc->line_buf_depth = 9;
		dsc->input_10_bits = 0;
		dsc->min_qp_flatness = 3;
		dsc->max_qp_flatness = 12;
		dsc->quant_incr_limit0 = 11;
		dsc->quant_incr_limit1 = 11;
	} else if (bpc == 10) { /* 10bpc */
		dsc->line_buf_depth = 11;
		dsc->input_10_bits = 1;
		dsc->min_qp_flatness = 7;
		dsc->max_qp_flatness = 16;
		dsc->quant_incr_limit0 = 15;
		dsc->quant_incr_limit1 = 15;
	} else { /* 12 bpc */
		dsc->line_buf_depth = 9;
		dsc->input_10_bits = 0;
		dsc->min_qp_flatness = 11;
		dsc->max_qp_flatness = 20;
		dsc->quant_incr_limit0 = 19;
		dsc->quant_incr_limit1 = 19;
	}

	dsc->slice_last_group_size = 3 - (dsc->slice_width % 3);

	dsc->det_thresh_flatness = 7 + 2*(bpc - 8);

	dsc->initial_xmit_delay = dsc->rc_model_size / (2 * bpp);

	groups_per_line = DIV_ROUND_UP(dsc->slice_width, 3);

	dsc->chunk_size = dsc->slice_width * bpp / 8;
	if ((dsc->slice_width * bpp) % 8)
		dsc->chunk_size++;

	/* rbs-min */
	min_rate_buffer_size =  dsc->rc_model_size - dsc->initial_offset +
			dsc->initial_xmit_delay * bpp +
			groups_per_line * dsc->first_line_bpg_offset;

	hrd_delay = DIV_ROUND_UP(min_rate_buffer_size, bpp);

	dsc->initial_dec_delay = hrd_delay - dsc->initial_xmit_delay;

	dsc->initial_scale_value = 8 * dsc->rc_model_size /
			(dsc->rc_model_size - dsc->initial_offset);

	slice_bits = 8 * dsc->chunk_size * dsc->slice_height;

	groups_total = groups_per_line * dsc->slice_height;

	data = dsc->first_line_bpg_offset * 2048;

	dsc->nfl_bpg_offset = DIV_ROUND_UP(data, (dsc->slice_height - 1));

	pre_num_extra_mux_bits = 3 * (mux_words_size + (4 * bpc + 4) - 2);

	num_extra_mux_bits = pre_num_extra_mux_bits - (mux_words_size -
		((slice_bits - pre_num_extra_mux_bits) % mux_words_size));

	data = 2048 * (dsc->rc_model_size - dsc->initial_offset
		+ num_extra_mux_bits);
	dsc->slice_bpg_offset = DIV_ROUND_UP(data, groups_total);

	/* bpp * 16 + 0.5 */
	data = bpp * 16;
	data *= 2;
	data++;
	data /= 2;
	target_bpp_x16 = data;

	data = (dsc->initial_xmit_delay * target_bpp_x16) / 16;
	final_value =  dsc->rc_model_size - data + num_extra_mux_bits;

	final_scale = 8 * dsc->rc_model_size /
		(dsc->rc_model_size - final_value);

	dsc->final_offset = final_value;

	data = (final_scale - 9) * (dsc->nfl_bpg_offset +
		dsc->slice_bpg_offset);
	dsc->scale_increment_interval = (2048 * dsc->final_offset) / data;

	dsc->scale_decrement_interval = groups_per_line /
		(dsc->initial_scale_value - 8);

	return 0;
}


static int dsi_panel_parse_phy_timing(struct dsi_display_mode *mode,
				struct device_node *of_node)
{
	const char *data;
	u32 len, i;
	int rc = 0;
	struct dsi_display_mode_priv_info *priv_info;

	priv_info = mode->priv_info;

	data = of_get_property(of_node,
			"qcom,mdss-dsi-panel-phy-timings", &len);
	if (!data) {
		pr_debug("Unable to read Phy timing settings");
	} else {
		priv_info->phy_timing_val =
			kzalloc((sizeof(u32) * len), GFP_KERNEL);
		if (!priv_info->phy_timing_val)
			return -EINVAL;

		for (i = 0; i < len; i++)
			priv_info->phy_timing_val[i] = data[i];

		priv_info->phy_timing_len = len;
	};

	mode->pixel_clk_khz = (mode->timing.h_active *
			DSI_V_TOTAL(&mode->timing) *
			mode->timing.refresh_rate) / 1000;
	return rc;
}

static int dsi_panel_parse_dsc_params(struct dsi_display_mode *mode,
				struct device_node *of_node)
{
	u32 data;
	int rc = -EINVAL;
	int intf_width;
	const char *compression;
	struct dsi_display_mode_priv_info *priv_info;

	if (!mode || !mode->priv_info)
		return -EINVAL;

	priv_info = mode->priv_info;

	priv_info->dsc_enabled = false;
	compression = of_get_property(of_node, "qcom,compression-mode", NULL);
	if (compression && !strcmp(compression, "dsc"))
		priv_info->dsc_enabled = true;

	if (!priv_info->dsc_enabled) {
		pr_debug("dsc compression is not enabled for the mode");
		return 0;
	}

	rc = of_property_read_u32(of_node, "qcom,mdss-dsc-slice-height", &data);
	if (rc) {
		pr_err("failed to parse qcom,mdss-dsc-slice-height\n");
		goto error;
	}
	priv_info->dsc.slice_height = data;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsc-slice-width", &data);
	if (rc) {
		pr_err("failed to parse qcom,mdss-dsc-slice-width\n");
		goto error;
	}
	priv_info->dsc.slice_width = data;

	intf_width = mode->timing.h_active;
	if (intf_width % priv_info->dsc.slice_width) {
		pr_err("invalid slice width for the intf width:%d slice width:%d\n",
			intf_width, priv_info->dsc.slice_width);
		rc = -EINVAL;
		goto error;
	}

	priv_info->dsc.pic_width = mode->timing.h_active;
	priv_info->dsc.pic_height = mode->timing.v_active;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsc-slice-per-pkt",
			&data);
	if (rc) {
		pr_err("failed to parse qcom,mdss-dsc-slice-per-pkt\n");
		goto error;
	}
	priv_info->dsc.slice_per_pkt = data;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsc-bit-per-component",
		&data);
	if (rc) {
		pr_err("failed to parse qcom,mdss-dsc-bit-per-component\n");
		goto error;
	}
	priv_info->dsc.bpc = data;

	rc = of_property_read_u32(of_node, "qcom,mdss-dsc-bit-per-pixel",
			&data);
	if (rc) {
		pr_err("failed to parse qcom,mdss-dsc-bit-per-pixel\n");
		goto error;
	}
	priv_info->dsc.bpp = data;

	priv_info->dsc.block_pred_enable = of_property_read_bool(of_node,
		"qcom,mdss-dsc-block-prediction-enable");

	priv_info->dsc.full_frame_slices = DIV_ROUND_UP(intf_width,
		priv_info->dsc.slice_width);

	dsi_dsc_populate_static_param(&priv_info->dsc);
	dsi_dsc_pclk_param_calc(&priv_info->dsc, intf_width);

error:
	return rc;
}

static int dsi_panel_parse_hdr_config(struct dsi_panel *panel,
				     struct device_node *of_node)
{

	int rc = 0;
	struct drm_panel_hdr_properties *hdr_prop;

	hdr_prop = &panel->hdr_props;
	hdr_prop->hdr_enabled = of_property_read_bool(of_node,
		"qcom,mdss-dsi-panel-hdr-enabled");

	if (hdr_prop->hdr_enabled) {
		rc = of_property_read_u32_array(of_node,
				"qcom,mdss-dsi-panel-hdr-color-primaries",
				hdr_prop->display_primaries,
				DISPLAY_PRIMARIES_MAX);
		if (rc) {
			pr_err("%s:%d, Unable to read color primaries,rc:%u",
					__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}

		rc = of_property_read_u32(of_node,
			"qcom,mdss-dsi-panel-peak-brightness",
			&(hdr_prop->peak_brightness));
		if (rc) {
			pr_err("%s:%d, Unable to read hdr brightness, rc:%u",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}

		rc = of_property_read_u32(of_node,
			"qcom,mdss-dsi-panel-blackness-level",
			&(hdr_prop->blackness_level));
		if (rc) {
			pr_err("%s:%d, Unable to read hdr brightness, rc:%u",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
			return rc;
		}
	}
	return 0;
}

static int dsi_panel_parse_topology(
		struct dsi_display_mode_priv_info *priv_info,
		struct device_node *of_node, int topology_override)
{
	struct msm_display_topology *topology;
	u32 top_count, top_sel, *array = NULL;
	int i, len = 0;
	int rc = -EINVAL;

	len = of_property_count_u32_elems(of_node, "qcom,display-topology");
	if (len <= 0 || len % TOPOLOGY_SET_LEN ||
			len > (TOPOLOGY_SET_LEN * MAX_TOPOLOGY)) {
		pr_err("invalid topology list for the panel, rc = %d\n", rc);
		return rc;
	}

	top_count = len / TOPOLOGY_SET_LEN;

	array = kcalloc(len, sizeof(u32), GFP_KERNEL);
	if (!array)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node,
			"qcom,display-topology", array, len);
	if (rc) {
		pr_err("unable to read the display topologies, rc = %d\n", rc);
		goto read_fail;
	}

	topology = kcalloc(top_count, sizeof(*topology), GFP_KERNEL);
	if (!topology) {
		rc = -ENOMEM;
		goto read_fail;
	}

	for (i = 0; i < top_count; i++) {
		struct msm_display_topology *top = &topology[i];

		top->num_lm = array[i * TOPOLOGY_SET_LEN];
		top->num_enc = array[i * TOPOLOGY_SET_LEN + 1];
		top->num_intf = array[i * TOPOLOGY_SET_LEN + 2];
	};

	if (topology_override >= 0 && topology_override < top_count) {
		pr_info("override topology: cfg:%d lm:%d comp_enc:%d intf:%d\n",
			topology_override,
			topology[topology_override].num_lm,
			topology[topology_override].num_enc,
			topology[topology_override].num_intf);
		top_sel = topology_override;
		goto parse_done;
	}

	rc = of_property_read_u32(of_node,
			"qcom,default-topology-index", &top_sel);
	if (rc) {
		pr_err("no default topology selected, rc = %d\n", rc);
		goto parse_fail;
	}

	if (top_sel >= top_count) {
		rc = -EINVAL;
		pr_err("default topology is specified is not valid, rc = %d\n",
			rc);
		goto parse_fail;
	}

	pr_info("default topology: lm: %d comp_enc:%d intf: %d\n",
		topology[top_sel].num_lm,
		topology[top_sel].num_enc,
		topology[top_sel].num_intf);

parse_done:
	memcpy(&priv_info->topology, &topology[top_sel],
		sizeof(struct msm_display_topology));
parse_fail:
	kfree(topology);
read_fail:
	kfree(array);

	return rc;
}

static int dsi_panel_parse_roi_alignment(struct device_node *of_node,
					 struct msm_roi_alignment *align)
{
	int len = 0, rc = 0;
	u32 value[6];
	struct property *data;

	if (!align || !of_node)
		return -EINVAL;

	memset(align, 0, sizeof(*align));

	data = of_find_property(of_node, "qcom,panel-roi-alignment", &len);
	len /= sizeof(u32);
	if (!data) {
		pr_err("panel roi alignment not found\n");
		rc = -EINVAL;
	} else if (len != 6) {
		pr_err("incorrect roi alignment len %d\n", len);
		rc = -EINVAL;
	} else {
		rc = of_property_read_u32_array(of_node,
				"qcom,panel-roi-alignment", value, len);
		if (rc)
			pr_debug("error reading panel roi alignment values\n");
		else {
			align->xstart_pix_align = value[0];
			align->ystart_pix_align = value[1];
			align->width_pix_align = value[2];
			align->height_pix_align = value[3];
			align->min_width = value[4];
			align->min_height = value[5];
		}

		pr_info("roi alignment: [%d, %d, %d, %d, %d, %d]\n",
			align->xstart_pix_align,
			align->width_pix_align,
			align->ystart_pix_align,
			align->height_pix_align,
			align->min_width,
			align->min_height);
	}

	return rc;
}

static int dsi_panel_parse_partial_update_caps(struct dsi_display_mode *mode,
				struct device_node *of_node)
{
	struct msm_roi_caps *roi_caps = NULL;
	const char *data;
	int rc = 0;

	if (!mode || !mode->priv_info) {
		pr_err("invalid arguments\n");
		return -EINVAL;
	}

	roi_caps = &mode->priv_info->roi_caps;

	memset(roi_caps, 0, sizeof(*roi_caps));

	data = of_get_property(of_node, "qcom,partial-update-enabled", NULL);
	if (data) {
		if (!strcmp(data, "dual_roi"))
			roi_caps->num_roi = 2;
		else if (!strcmp(data, "single_roi"))
			roi_caps->num_roi = 1;
		else {
			pr_info(
			"invalid value for qcom,partial-update-enabled: %s\n",
			data);
			return 0;
		}
	} else {
		pr_info("partial update disabled as the property is not set\n");
		return 0;
	}

	roi_caps->merge_rois = of_property_read_bool(of_node,
			"qcom,partial-update-roi-merge");

	roi_caps->enabled = roi_caps->num_roi > 0;

	pr_info("partial update num_rois=%d enabled=%d\n", roi_caps->num_roi,
			roi_caps->enabled);

	if (roi_caps->enabled)
		rc = dsi_panel_parse_roi_alignment(of_node,
				&roi_caps->align);

	if (rc)
		memset(roi_caps, 0, sizeof(*roi_caps));

	return rc;
}

static int dsi_panel_parse_dms_info(struct dsi_panel *panel,
	struct device_node *of_node)
{
	int dms_enabled;
	const char *data;

	if (!of_node || !panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	panel->dms_mode = DSI_DMS_MODE_DISABLED;
	dms_enabled = of_property_read_bool(of_node,
		"qcom,dynamic-mode-switch-enabled");
	if (!dms_enabled)
		return 0;

	data = of_get_property(of_node, "qcom,dynamic-mode-switch-type", NULL);
	if (data && !strcmp(data, "dynamic-resolution-switch-immediate")) {
		panel->dms_mode = DSI_DMS_MODE_RES_SWITCH_IMMEDIATE;
	} else {
		pr_err("[%s] unsupported dynamic switch mode: %s\n",
							panel->name, data);
		return -EINVAL;
	}

	return 0;
};

/*
 * The length of all the valid values to be checked should not be greater
 * than the length of returned data from read command.
 */
static bool
dsi_panel_parse_esd_check_valid_params(struct dsi_panel *panel, u32 count)
{
	int i;
	struct drm_panel_esd_config *config = &panel->esd_config;

	for (i = 0; i < count; ++i) {
		if (config->status_valid_params[i] >
				config->status_cmds_rlen[i]) {
			pr_debug("ignore valid params\n");
			return false;
		}
	}

	return true;
}

static bool dsi_panel_parse_esd_status_len(struct device_node *np,
	char *prop_key, u32 **target, u32 cmd_cnt)
{
	int tmp;

	if (!of_find_property(np, prop_key, &tmp))
		return false;

	tmp /= sizeof(u32);
	if (tmp != cmd_cnt) {
		pr_err("request property(%d) do not match cmd count(%d)\n",
				tmp, cmd_cnt);
		return false;
	}

	*target = kcalloc(tmp, sizeof(u32), GFP_KERNEL);
	if (IS_ERR_OR_NULL(*target)) {
		pr_err("Error allocating memory for property\n");
		return false;
	}

	if (of_property_read_u32_array(np, prop_key, *target, tmp)) {
		pr_err("cannot get values from dts\n");
		kfree(*target);
		*target = NULL;
		return false;
	}

	return true;
}

static void dsi_panel_esd_config_deinit(struct drm_panel_esd_config *esd_config)
{
	kfree(esd_config->status_buf);
	kfree(esd_config->return_buf);
	kfree(esd_config->status_value);
	kfree(esd_config->status_valid_params);
	kfree(esd_config->status_cmds_rlen);
	kfree(esd_config->status_cmd.cmds);
}

static int dsi_panel_parse_esd_config(struct dsi_panel *panel,
				     struct device_node *of_node)
{
	int rc = 0;
	u32 tmp;
	u32 i, status_len, *lenp;
	struct property *data;
	const char *string;
	struct drm_panel_esd_config *esd_config;
	u8 *esd_mode = NULL;

	esd_config = &panel->esd_config;
	esd_config->status_mode = ESD_MODE_MAX;
	esd_config->esd_enabled = of_property_read_bool(of_node,
		"qcom,esd-check-enabled");

	if (!esd_config->esd_enabled)
		return 0;

	rc = of_property_read_string(of_node,
			"qcom,mdss-dsi-panel-status-check-mode", &string);
	if (!rc) {
		if (!strcmp(string, "bta_check")) {
			esd_config->status_mode = ESD_MODE_SW_BTA;
		} else if (!strcmp(string, "reg_read")) {
			esd_config->status_mode = ESD_MODE_REG_READ;
#if defined(CONFIG_DISPLAY_SAMSUNG)
		} else if (!strcmp(string, "irq_check")) {
			esd_config->status_mode = ESD_MODE_PANEL_IRQ;
#endif
		} else if (!strcmp(string, "te_signal_check")) {
			if (panel->panel_mode == DSI_OP_CMD_MODE) {
				esd_config->status_mode = ESD_MODE_PANEL_TE;
			} else {
				pr_err("TE-ESD not valid for video mode\n");
				rc = -EINVAL;
				goto error;
			}
		} else {
			pr_err("No valid panel-status-check-mode string\n");
			rc = -EINVAL;
			goto error;
		}
	} else {
		pr_debug("status check method not defined!\n");
		rc = -EINVAL;
		goto error;
	}

	if ((esd_config->status_mode == ESD_MODE_SW_BTA) ||
#if defined(CONFIG_DISPLAY_SAMSUNG)
		(esd_config->status_mode == ESD_MODE_PANEL_IRQ) ||
#endif
		(esd_config->status_mode == ESD_MODE_PANEL_TE))
		return 0;

	dsi_panel_parse_cmd_sets_sub(&esd_config->status_cmd,
				DSI_CMD_SET_PANEL_STATUS, of_node);
	if (!esd_config->status_cmd.count) {
		pr_err("panel status command parsing failed\n");
		rc = -EINVAL;
		goto error;
	}

	if (!dsi_panel_parse_esd_status_len(of_node,
		"qcom,mdss-dsi-panel-status-read-length",
			&panel->esd_config.status_cmds_rlen,
				esd_config->status_cmd.count)) {
		pr_err("Invalid status read length\n");
		rc = -EINVAL;
		goto error1;
	}

	if (dsi_panel_parse_esd_status_len(of_node,
		"qcom,mdss-dsi-panel-status-valid-params",
			&panel->esd_config.status_valid_params,
				esd_config->status_cmd.count)) {
		if (!dsi_panel_parse_esd_check_valid_params(panel,
					esd_config->status_cmd.count)) {
			rc = -EINVAL;
			goto error2;
		}
	}

	status_len = 0;
	lenp = esd_config->status_valid_params ?: esd_config->status_cmds_rlen;
	for (i = 0; i < esd_config->status_cmd.count; ++i)
		status_len += lenp[i];

	if (!status_len) {
		rc = -EINVAL;
		goto error2;
	}

	/*
	 * Some panel may need multiple read commands to properly
	 * check panel status. Do a sanity check for proper status
	 * value which will be compared with the value read by dsi
	 * controller during ESD check. Also check if multiple read
	 * commands are there then, there should be corresponding
	 * status check values for each read command.
	 */
	data = of_find_property(of_node,
			"qcom,mdss-dsi-panel-status-value", &tmp);
	tmp /= sizeof(u32);
	if (!IS_ERR_OR_NULL(data) && tmp != 0 && (tmp % status_len) == 0) {
		esd_config->groups = tmp / status_len;
	} else {
		pr_err("error parse panel-status-value\n");
		rc = -EINVAL;
		goto error2;
	}

	esd_config->status_value =
		kzalloc(sizeof(u32) * status_len * esd_config->groups,
			GFP_KERNEL);
	if (!esd_config->status_value) {
		rc = -ENOMEM;
		goto error2;
	}

	esd_config->return_buf = kcalloc(status_len * esd_config->groups,
			sizeof(unsigned char), GFP_KERNEL);
	if (!esd_config->return_buf) {
		rc = -ENOMEM;
		goto error3;
	}

	esd_config->status_buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (!esd_config->status_buf)
		goto error4;

	rc = of_property_read_u32_array(of_node,
		"qcom,mdss-dsi-panel-status-value",
		esd_config->status_value, esd_config->groups * status_len);
	if (rc) {
		pr_debug("error reading panel status values\n");
		memset(esd_config->status_value, 0,
				esd_config->groups * status_len);
	}

	if (panel->esd_config.status_mode == ESD_MODE_REG_READ)
		esd_mode = "register_read";
	else if (panel->esd_config.status_mode == ESD_MODE_SW_BTA)
		esd_mode = "bta_trigger";
	else if (panel->esd_config.status_mode ==  ESD_MODE_PANEL_TE)
		esd_mode = "te_check";

	pr_info("ESD enabled with mode: %s\n", esd_mode);

	return 0;

error4:
	kfree(esd_config->return_buf);
error3:
	kfree(esd_config->status_value);
error2:
	kfree(esd_config->status_valid_params);
	kfree(esd_config->status_cmds_rlen);
error1:
	kfree(esd_config->status_cmd.cmds);
error:
	panel->esd_config.esd_enabled = false;
	return rc;
}

struct dsi_panel *dsi_panel_get(struct device *parent,
				struct device_node *of_node,
				int topology_override)
{
	struct dsi_panel *panel;
	int rc = 0;

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	panel->name = of_get_property(of_node, "qcom,mdss-dsi-panel-name",
				      NULL);
	if (!panel->name)
		panel->name = DSI_PANEL_DEFAULT_LABEL;

	rc = dsi_panel_parse_host_config(panel, of_node);
	if (rc) {
		pr_err("failed to parse host configuration, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_parse_panel_mode(panel, of_node);
	if (rc) {
		pr_err("failed to parse panel mode configuration, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_parse_dfps_caps(&panel->dfps_caps, of_node, panel->name);
	if (rc)
		pr_err("failed to parse dfps configuration, rc=%d\n", rc);

	rc = dsi_panel_parse_phy_props(&panel->phy_props, of_node, panel->name);
	if (rc) {
		pr_err("failed to parse panel physical dimension, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_parse_power_cfg(parent, panel, of_node);
	if (rc)
		pr_err("failed to parse power config, rc=%d\n", rc);

	rc = dsi_panel_parse_gpios(panel, of_node);
	if (rc)
		pr_err("failed to parse panel gpios, rc=%d\n", rc);

	rc = dsi_panel_parse_bl_config(panel, of_node);
	if (rc)
		pr_err("failed to parse backlight config, rc=%d\n", rc);


	rc = dsi_panel_parse_misc_features(panel, of_node);
	if (rc)
		pr_err("failed to parse misc features, rc=%d\n", rc);

	rc = dsi_panel_parse_hdr_config(panel, of_node);
	if (rc)
		pr_err("failed to parse hdr config, rc=%d\n", rc);

	rc = dsi_panel_get_mode_count(panel, of_node);
	if (rc) {
		pr_err("failed to get mode count, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_parse_dms_info(panel, of_node);
	if (rc)
		pr_debug("failed to get dms info, rc=%d\n", rc);

	rc = dsi_panel_parse_esd_config(panel, of_node);
	if (rc) {
		pr_debug("failed to parse esd config, rc=%d\n", rc);
	} else {
		u8 *esd_mode = NULL;

#if defined(CONFIG_DISPLAY_SAMSUNG)
		if (panel->esd_config.status_mode == ESD_MODE_PANEL_IRQ)
			esd_mode = "irq_check";
#endif

		pr_info("ESD enabled with mode: %s\n", esd_mode);
	}

	panel->panel_of_node = of_node;
	drm_panel_init(&panel->drm_panel);
	mutex_init(&panel->panel_lock);
	panel->parent = parent;
	return panel;
error:
	kfree(panel);
	return ERR_PTR(rc);
}

void dsi_panel_put(struct dsi_panel *panel)
{
	/* free resources allocated for ESD check */
	dsi_panel_esd_config_deinit(&panel->esd_config);

	kfree(panel);
}

int dsi_panel_drv_init(struct dsi_panel *panel,
		       struct mipi_dsi_host *host)
{
	int rc = 0;
	struct mipi_dsi_device *dev;

	if (!panel || !host) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	dev = &panel->mipi_device;

	dev->host = host;
	/*
	 * We dont have device structure since panel is not a device node.
	 * When using drm panel framework, the device is probed when the host is
	 * create.
	 */
	dev->channel = 0;
	dev->lanes = 4;

	panel->host = host;
#if defined(CONFIG_DISPLAY_SAMSUNG)
	/* In this point, vdd->panel_attach_status has invalid data.
	 * So, use panel name to verify PBA booting,
	 * intead of ss_panel_attach_get().
	 */
	if (!strcmp(panel->name, "ss_dsi_panel_PBA_BOOTING_FHD")) {
		pr_info("PBA booting, skip to get vreg, gpios\n");
		goto pba_booting;
	}
#endif
	rc = dsi_panel_vreg_get(panel);
	if (rc) {
		pr_err("[%s] failed to get panel regulators, rc=%d\n",
		       panel->name, rc);
		goto exit;
	}

	rc = dsi_panel_pinctrl_init(panel);
	if (rc) {
		pr_err("[%s] failed to init pinctrl, rc=%d\n", panel->name, rc);
		goto error_vreg_put;
	}

	rc = dsi_panel_gpio_request(panel);
	if (rc) {
		pr_err("[%s] failed to request gpios, rc=%d\n", panel->name,
		       rc);
		goto error_pinctrl_deinit;
	}

	rc = dsi_panel_bl_register(panel);
	if (rc) {
		if (rc != -EPROBE_DEFER)
			pr_err("[%s] failed to register backlight, rc=%d\n",
			       panel->name, rc);
		goto error_gpio_release;
	}

#if defined(CONFIG_DISPLAY_SAMSUNG)
pba_booting:
	ss_panel_init(panel);
#endif
	goto exit;

error_gpio_release:
	(void)dsi_panel_gpio_release(panel);
error_pinctrl_deinit:
	(void)dsi_panel_pinctrl_deinit(panel);
error_vreg_put:
	(void)dsi_panel_vreg_put(panel);
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_drv_deinit(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_bl_unregister(panel);
	if (rc)
		pr_err("[%s] failed to unregister backlight, rc=%d\n",
		       panel->name, rc);

	rc = dsi_panel_gpio_release(panel);
	if (rc)
		pr_err("[%s] failed to release gpios, rc=%d\n", panel->name,
		       rc);

	rc = dsi_panel_pinctrl_deinit(panel);
	if (rc)
		pr_err("[%s] failed to deinit gpios, rc=%d\n", panel->name,
		       rc);

	rc = dsi_panel_vreg_put(panel);
	if (rc)
		pr_err("[%s] failed to put regs, rc=%d\n", panel->name, rc);

	panel->host = NULL;
	memset(&panel->mipi_device, 0x0, sizeof(panel->mipi_device));

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_validate_mode(struct dsi_panel *panel,
			    struct dsi_display_mode *mode)
{
	return 0;
}

int dsi_panel_get_mode_count(struct dsi_panel *panel,
	struct device_node *of_node)
{
	const u32 SINGLE_MODE_SUPPORT = 1;
	struct device_node *timings_np;
	int count, rc = 0;

	if (!of_node || !panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	panel->num_timing_nodes = 0;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	if (!strcmp(panel->name, "ss_dsi_panel_PBA_BOOTING_FHD")) {
		pr_info("PBA booting, force to set num_timing_nodes 1\n");
		panel->num_timing_nodes = 1;
		return 0;
	}
#endif

	timings_np = of_get_child_by_name(of_node,
			"qcom,mdss-dsi-display-timings");
	if (!timings_np) {
		pr_err("no display timing nodes defined\n");
		rc = -EINVAL;
		goto error;
	}

	count = of_get_child_count(timings_np);
	if (!count || count > DSI_MODE_MAX) {
		pr_err("invalid count of timing nodes: %d\n", count);
		rc = -EINVAL;
		goto error;
	}

	/* No multiresolution support is available for video mode panels */
	if (panel->panel_mode != DSI_OP_CMD_MODE)
		count = SINGLE_MODE_SUPPORT;

	panel->num_timing_nodes = count;

error:
	return rc;
}

int dsi_panel_get_phy_props(struct dsi_panel *panel,
			    struct dsi_panel_phy_props *phy_props)
{
	int rc = 0;

	if (!panel || !phy_props) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	memcpy(phy_props, &panel->phy_props, sizeof(*phy_props));

	return rc;
}

int dsi_panel_get_dfps_caps(struct dsi_panel *panel,
			    struct dsi_dfps_capabilities *dfps_caps)
{
	int rc = 0;

	if (!panel || !dfps_caps) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	memcpy(dfps_caps, &panel->dfps_caps, sizeof(*dfps_caps));

	return rc;
}

void dsi_panel_put_mode(struct dsi_display_mode *mode)
{
	int i;

	if (!mode->priv_info)
		return;

	for (i = 0; i < DSI_CMD_SET_MAX; i++)
		dsi_panel_destroy_cmd_packets(&mode->priv_info->cmd_sets[i]);

	kfree(mode->priv_info);
}

int dsi_panel_get_mode(struct dsi_panel *panel,
			u32 index, struct dsi_display_mode *mode,
			int topology_override)
{
	struct device_node *timings_np, *child_np;
	struct dsi_display_mode_priv_info *prv_info;
	u32 child_idx = 0;
	int rc = 0, num_timings;

	if (!panel || !mode) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mode->priv_info = kzalloc(sizeof(*mode->priv_info), GFP_KERNEL);
	if (!mode->priv_info) {
		rc = -ENOMEM;
		goto done;
	}

	pr_info("%s, new dsi_display_mode_priv_info at %p, CPU%d\n",
			__func__, mode->priv_info, raw_smp_processor_id());
	prv_info = mode->priv_info;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	if (!strcmp(panel->name, "ss_dsi_panel_PBA_BOOTING_FHD")) {
		pr_info("PBA booting, skip DMS\n");

		rc = dsi_panel_parse_timing(&mode->timing, panel->panel_of_node);
		if (rc) {
			pr_err("failed to parse panel timing, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_dsc_params(mode, panel->panel_of_node);
		if (rc) {
			pr_err("failed to parse dsc params, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_topology(prv_info, panel->panel_of_node,
				topology_override);
		if (rc) {
			pr_err("failed to parse panel topology, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_cmd_sets(prv_info, panel->panel_of_node);
		if (rc) {
			pr_err("failed to parse command sets, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_jitter_config(mode, panel->panel_of_node);
		if (rc)
			pr_err(
			"failed to parse panel jitter config, rc=%d\n", rc);

		rc = dsi_panel_parse_phy_timing(mode, panel->panel_of_node);
		if (rc) {
			pr_err(
			"failed to parse panel phy timings, rc=%d\n", rc);
			goto parse_fail;
		}

		goto done;
	}
#endif

	timings_np = of_get_child_by_name(panel->panel_of_node,
		"qcom,mdss-dsi-display-timings");
	if (!timings_np) {
		pr_err("no display timing nodes defined\n");
		rc = -EINVAL;
		goto parse_fail;
	}

	num_timings = of_get_child_count(timings_np);
	if (!num_timings || num_timings > DSI_MODE_MAX) {
		pr_err("invalid count of timing nodes: %d\n", num_timings);
		rc = -EINVAL;
		goto parse_fail;
	}

	for_each_child_of_node(timings_np, child_np) {
		if (index != child_idx++)
			continue;

		rc = dsi_panel_parse_timing(&mode->timing, child_np);
		if (rc) {
			pr_err("failed to parse panel timing, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_dsc_params(mode, child_np);
		if (rc) {
			pr_err("failed to parse dsc params, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_topology(prv_info, child_np,
				topology_override);
		if (rc) {
			pr_err("failed to parse panel topology, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_cmd_sets(prv_info, child_np);
		if (rc) {
			pr_err("failed to parse command sets, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_jitter_config(mode, child_np);
		if (rc)
			pr_err(
			"failed to parse panel jitter config, rc=%d\n", rc);

		rc = dsi_panel_parse_phy_timing(mode, child_np);
		if (rc) {
			pr_err(
			"failed to parse panel phy timings, rc=%d\n", rc);
			goto parse_fail;
		}

		rc = dsi_panel_parse_partial_update_caps(mode, child_np);
		if (rc)
			pr_err("failed to partial update caps, rc=%d\n", rc);
	}
	goto done;

parse_fail:
	kfree(mode->priv_info);
	mode->priv_info = NULL;
done:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_get_host_cfg_for_mode(struct dsi_panel *panel,
				    struct dsi_display_mode *mode,
				    struct dsi_host_config *config)
{
	int rc = 0;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	struct samsung_display_driver_data *vdd;
#endif

	if (!panel || !mode || !config) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	config->panel_mode = panel->panel_mode;
	memcpy(&config->common_config, &panel->host_config,
	       sizeof(config->common_config));

	if (panel->panel_mode == DSI_OP_VIDEO_MODE) {
		memcpy(&config->u.video_engine, &panel->video_config,
		       sizeof(config->u.video_engine));
	} else {
		memcpy(&config->u.cmd_engine, &panel->cmd_config,
		       sizeof(config->u.cmd_engine));
	}

	memcpy(&config->video_timing, &mode->timing,
	       sizeof(config->video_timing));
	config->video_timing.dsc_enabled = mode->priv_info->dsc_enabled;
	config->video_timing.dsc = &mode->priv_info->dsc;

	config->bit_clk_rate_hz = mode->priv_info->clk_rate_hz;
	config->esc_clk_rate_hz = 19200000;

#if defined(CONFIG_DISPLAY_SAMSUNG)
	vdd = panel->panel_private;
	if (vdd->dtsi_data.samsung_esc_clk_128M)
		config->esc_clk_rate_hz = 12800000;
#endif

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_prepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	/* If LP11_INIT is set, panel will be powered up during prepare() */
	if (panel->lp11_init)
		goto error;

	rc = dsi_panel_power_on(panel);
	if (rc) {
		pr_err("[%s] panel power on failed, rc=%d\n", panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

#if defined(CONFIG_DISPLAY_SAMSUNG)
/* update pps without panel_lock in GCT to avoid deadlock.
 * GCT blocks all mipi cmd trasmission and frame update,
 * so it doesn't require panel_lock in GCT process.
 */
int dsi_panel_update_pps_nolock(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set = NULL;
	struct dsi_display_mode_priv_info *priv_info = NULL;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	ss_send_cmd(panel->panel_private, TX_LEVEL0_KEY_ENABLE);


	priv_info = panel->cur_mode->priv_info;

	set = &priv_info->cmd_sets[DSI_CMD_SET_PPS];

	dsi_dsc_create_pps_buf_cmd(&priv_info->dsc, panel->dsc_pps_cmd, 0);
	rc = dsi_panel_create_cmd_packets(panel->dsc_pps_cmd,
					  DSI_CMD_PPS_SIZE, 1, set->cmds);
	if (rc) {
		pr_err("failed to create cmd packets, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PPS);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_PPS cmds, rc=%d\n",
			panel->name, rc);
		goto error;
	}

error:
	ss_send_cmd(panel->panel_private, TX_LEVEL0_KEY_DISABLE);
	return rc;
}
#endif

int dsi_panel_update_pps(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set = NULL;
	struct dsi_display_mode_priv_info *priv_info = NULL;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_send_cmd(panel->panel_private, TX_LEVEL0_KEY_ENABLE);
#endif


	priv_info = panel->cur_mode->priv_info;

	set = &priv_info->cmd_sets[DSI_CMD_SET_PPS];

	dsi_dsc_create_pps_buf_cmd(&priv_info->dsc, panel->dsc_pps_cmd, 0);
	rc = dsi_panel_create_cmd_packets(panel->dsc_pps_cmd,
					  DSI_CMD_PPS_SIZE, 1, set->cmds);
	if (rc) {
		pr_err("failed to create cmd packets, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PPS);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_PPS cmds, rc=%d\n",
			panel->name, rc);
		goto error;
	}

error:
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_send_cmd(panel->panel_private, TX_LEVEL0_KEY_DISABLE);
#endif
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_lp1(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP1);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_LP1 cmd, rc=%d\n",
		       panel->name, rc);
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_low_power_config(panel->panel_private, true);
#endif
	mutex_unlock(&panel->panel_lock);

	return rc;
}

int dsi_panel_set_lp2(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP2);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_LP2 cmd, rc=%d\n",
		       panel->name, rc);
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_low_power_config(panel->panel_private, true);
#endif
	mutex_unlock(&panel->panel_lock);

	return rc;
}

int dsi_panel_set_nolp(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_NOLP cmd, rc=%d\n",
		       panel->name, rc);
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_low_power_config(panel->panel_private, false);
#endif
	mutex_unlock(&panel->panel_lock);

	return rc;
}

int dsi_panel_prepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (panel->lp11_init) {
		rc = dsi_panel_power_on(panel);
		if (rc) {
			pr_err("[%s] panel power on failed, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}
#if defined(CONFIG_DISPLAY_SAMSUNG)
	/* In case of lp11_init false, it should put 5ms delay
	 * between LP11 and dcs tx.
	 * TODO: get delay value from panel dtsi.
	 */
	if (!panel->lp11_init)
		mdelay(5);
#endif

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_PRE_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_panel_roi_prepare_dcs_cmds(struct dsi_panel_cmd_set *set,
		struct dsi_rect *roi, int ctrl_idx, int unicast)
{
	static const int ROI_CMD_LEN = 5;

	int rc = 0;

	/* DTYPE_DCS_LWRITE */
	static char *caset, *paset;

	set->cmds = NULL;

	caset = kzalloc(ROI_CMD_LEN, GFP_KERNEL);
	if (!caset) {
		rc = -ENOMEM;
		goto exit;
	}
	caset[0] = 0x2a;
	caset[1] = (roi->x & 0xFF00) >> 8;
	caset[2] = roi->x & 0xFF;
	caset[3] = ((roi->x - 1 + roi->w) & 0xFF00) >> 8;
	caset[4] = (roi->x - 1 + roi->w) & 0xFF;

	paset = kzalloc(ROI_CMD_LEN, GFP_KERNEL);
	if (!paset) {
		rc = -ENOMEM;
		goto error_free_mem;
	}
	paset[0] = 0x2b;
	paset[1] = (roi->y & 0xFF00) >> 8;
	paset[2] = roi->y & 0xFF;
	paset[3] = ((roi->y - 1 + roi->h) & 0xFF00) >> 8;
	paset[4] = (roi->y - 1 + roi->h) & 0xFF;

	set->type = DSI_CMD_SET_ROI;
	set->state = DSI_CMD_SET_STATE_LP;
	set->count = 2; /* send caset + paset together */
	set->cmds = kcalloc(set->count, sizeof(*set->cmds), GFP_KERNEL);
	if (!set->cmds) {
		rc = -ENOMEM;
		goto error_free_mem;
	}
	set->cmds[0].msg.channel = 0;
	set->cmds[0].msg.type = MIPI_DSI_DCS_LONG_WRITE;
	set->cmds[0].msg.flags = unicast ? MIPI_DSI_MSG_UNICAST : 0;
	set->cmds[0].msg.ctrl = unicast ? ctrl_idx : 0;
	set->cmds[0].msg.tx_len = ROI_CMD_LEN;
	set->cmds[0].msg.tx_buf = caset;
	set->cmds[0].msg.rx_len = 0;
	set->cmds[0].msg.rx_buf = 0;
	set->cmds[0].last_command = 0;
	set->cmds[0].post_wait_ms = 0;

	set->cmds[1].msg.channel = 0;
	set->cmds[1].msg.type = MIPI_DSI_DCS_LONG_WRITE;
	set->cmds[1].msg.flags = unicast ? MIPI_DSI_MSG_UNICAST : 0;
	set->cmds[1].msg.ctrl = unicast ? ctrl_idx : 0;
	set->cmds[1].msg.tx_len = ROI_CMD_LEN;
	set->cmds[1].msg.tx_buf = paset;
	set->cmds[1].msg.rx_len = 0;
	set->cmds[1].msg.rx_buf = 0;
	set->cmds[1].last_command = 1;
	set->cmds[1].post_wait_ms = 0;

	goto exit;

error_free_mem:
	kfree(caset);
	kfree(paset);
	kfree(set->cmds);

exit:
	return rc;
}

int dsi_panel_send_roi_dcs(struct dsi_panel *panel, int ctrl_idx,
		struct dsi_rect *roi)
{
	int rc = 0;
	struct dsi_panel_cmd_set *set;
	struct dsi_display_mode_priv_info *priv_info;

	if (!panel || !panel->cur_mode) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;
	set = &priv_info->cmd_sets[DSI_CMD_SET_ROI];

	rc = dsi_panel_roi_prepare_dcs_cmds(set, roi, ctrl_idx, true);
	if (rc) {
		pr_err("[%s] failed to prepare DSI_CMD_SET_ROI cmds, rc=%d\n",
				panel->name, rc);
		return rc;
	}
	pr_debug("[%s] send roi x %d y %d w %d h %d\n", panel->name,
			roi->x, roi->y, roi->w, roi->h);

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ROI);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_ROI cmds, rc=%d\n",
				panel->name, rc);

	mutex_unlock(&panel->panel_lock);

	dsi_panel_destroy_cmd_packets(set);

	return rc;
}

int dsi_panel_switch(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}
#if defined(CONFIG_DISPLAY_SAMSUNG)
	pr_info("DMS : send switch cmd\n");
#endif

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_TIMING_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_switch(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_TIMING_SWITCH);
	if (rc)
		pr_err("[%s] failed to send DSI_CMD_SET_POST_TIMING_SWITCH cmds, rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_enable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	pr_err("++\n");
	mutex_lock(&panel->panel_lock);
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_on_pre(panel->panel_private);
#endif
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_ON cmds, rc=%d\n",
		       panel->name, rc);
	}
	panel->panel_initialized = true;
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_on_post(panel->panel_private);
#endif
	mutex_unlock(&panel->panel_lock);

	pr_err("--\n");
	return rc;
}

int dsi_panel_post_enable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_ON);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_POST_ON cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_disable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_off_pre(panel->panel_private);
#endif
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_OFF);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_PRE_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_disable(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	pr_err("++\n");
	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OFF);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}
#if defined(CONFIG_DISPLAY_SAMSUNG)
	ss_panel_off_post(panel->panel_private);
#endif
	panel->panel_initialized = false;

error:
	mutex_unlock(&panel->panel_lock);

	pr_err("--\n");
	return rc;
}

int dsi_panel_unprepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_OFF);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_SET_POST_OFF cmds, rc=%d\n",
		       panel->name, rc);
		goto error;
	}

	if (panel->lp11_init) {
		rc = dsi_panel_power_off(panel);
		if (rc) {
			pr_err("[%s] panel power_Off failed, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_unprepare(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!panel->lp11_init) {
		rc = dsi_panel_power_off(panel);
		if (rc) {
			pr_err("[%s] panel power_Off failed, rc=%d\n",
			       panel->name, rc);
			goto error;
		}
	}
error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}
