// SPDX-License-Identifier: GPL-2.0-only
/* Sophgo CV181x MIPI CSI-2 receiver */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#include <media/mipi-csi2.h>

#include "cv181x-camera.h"

#define CV181X_CSI2_MAC_SENSOR_00	0x000
#define CV181X_CSI2_MAC_CSI_00		0x400
#define CV181X_CSI2_MAC_CSI_18		0x418
#define CV181X_CSI2_MAC_CSI_70		0x470

#define CV181X_CSI2_WRAP_PHY4L		0x300
#define CV181X_CSI2_WRAP_PHY2L		0x600
#define CV181X_CSI2_WRAP_TOP_00		0x000
#define CV181X_CSI2_WRAP_ANALOG_CTRL	0x004
#define CV181X_CSI2_WRAP_PHY_04		0x04
#define CV181X_CSI2_WRAP_PHY_08		0x08
#define CV181X_CSI2_WRAP_PHY_0C		0x0c
#define CV181X_CSI2_WRAP_PHY_10		0x10
#define CV181X_CSI2_WRAP_PHY_80		0x80
#define CV181X_CSI2_WRAP_PHY_84		0x84
#define CV181X_CSI2_WRAP_PHY_88		0x88

#define CV181X_VIP_SYS_RESET		0x000
#define CV181X_VIP_SYS_RESET_CSI_MAC0	BIT(12)

#define CV181X_CSI2_SENSOR_MODE_CSI	1
#define CV181X_CSI2_SENSOR_ENABLE	BIT(4)
#define CV181X_CSI2_SENSOR_VS_INV	BIT(5)
#define CV181X_CSI2_SENSOR_HS_INV	BIT(6)
#define CV181X_CSI2_SENSOR_SW_UPDATE	BIT(18)
#define CV181X_CSI2_LANE_MODE_MASK	GENMASK(2, 0)
#define CV181X_CSI2_VS_GEN_MODE_MASK	GENMASK(1, 0)
#define CV181X_CSI2_VC_MAP_IDENTITY	0x3210

#define CV181X_CSI2_WRAP_AUTO_IGNORE	BIT(16)
#define CV181X_CSI2_WRAP_AUTO_SYNC	BIT(17)
#define CV181X_CSI2_WRAP_CLK_P0_TO_P1	BIT(6)
#define CV181X_CSI2_WRAP_CLK_P1_TO_P0	BIT(7)
#define CV181X_CSI2_WRAP_CLK_LANE_MASK	GENMASK(21, 16)
#define CV181X_CSI2_WRAP_PHY_CLK_MASK	GENMASK(2, 0)
#define CV181X_CSI2_WRAP_PHY_CLK_SWAP	BIT(4)
#define CV181X_CSI2_WRAP_DATA0_MASK	GENMASK(2, 0)
#define CV181X_CSI2_WRAP_DATA1_MASK	GENMASK(6, 4)
#define CV181X_CSI2_WRAP_DATA2_MASK	GENMASK(10, 8)
#define CV181X_CSI2_WRAP_DATA3_MASK	GENMASK(14, 12)
#define CV181X_CSI2_WRAP_DATA_SWAP(n)	BIT(8 + (n))
#define CV181X_CSI2_WRAP_PD_IBIAS	BIT(14)
#define CV181X_CSI2_WRAP_PD_RXLP_MASK	GENMASK(21, 16)
#define CV181X_CSI2_WRAP_CLK_INV_MASK	GENMASK(5, 0)
#define CV181X_CSI2_WRAP_HS_SETTLE_MASK	GENMASK(7, 0)
#define CV181X_CSI2_WRAP_HS_SETTLE	8
#define CV181X_CSI2_MAX_PHYSICAL_LANE	5
#define CV181X_CSI2_CLK_PHASE		0
#define CV181X_CSI2_DATA_NEAR_PHASE	3
#define CV181X_CSI2_DATA_FAR_PHASE	8

static inline struct cv181x_camera_dev *
sd_to_cv181x_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cv181x_camera_dev, csi2_subdev);
}

static int cv181x_csi2_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *fmt;

	if (code->pad == CV181X_CSI2_PAD_SOURCE) {
		if (code->index)
			return -EINVAL;

		fmt = v4l2_subdev_state_get_format(state, code->pad);
		code->code = fmt->code;
		return 0;
	}

	if (code->pad != CV181X_CSI2_PAD_SINK ||
	    code->index >= cv181x_num_camera_formats)
		return -EINVAL;

	code->code = cv181x_camera_formats[code->index].mbus_code;
	return 0;
}

int cv181x_csi2_set_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct cv181x_camera_dev *cam = sd_to_cv181x_csi2(sd);
	const struct cv181x_camera_format *csi2_fmt;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *source_fmt;

	if (format->pad == CV181X_CSI2_PAD_SOURCE)
		return v4l2_subdev_get_fmt(sd, state, format);
	if (format->pad != CV181X_CSI2_PAD_SINK)
		return -EINVAL;

	csi2_fmt = cv181x_camera_find_mbus(format->format.code);
	format->format.code = csi2_fmt->mbus_code;
	format->format.width = clamp(format->format.width, 1U,
				     CV181X_CSI2_MAX_WIDTH);
	format->format.height = clamp(format->format.height, 1U,
				      CV181X_CSI2_MAX_HEIGHT);
	format->format.field = V4L2_FIELD_NONE;
	format->format.colorspace =
		csi2_fmt->csi_dt == MIPI_CSI2_DT_YUV422_8B ?
		V4L2_COLORSPACE_SRGB : V4L2_COLORSPACE_RAW;
	format->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	format->format.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	sink_fmt = v4l2_subdev_state_get_format(state,
						CV181X_CSI2_PAD_SINK);
	*sink_fmt = format->format;
	source_fmt = v4l2_subdev_state_get_format(state,
						  CV181X_CSI2_PAD_SOURCE);
	*source_fmt = format->format;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		cam->fmt = csi2_fmt;
		cv181x_camera_fill_pix(&cam->pix, csi2_fmt,
				       format->format.width,
				       format->format.height);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cv181x_csi2_set_format);

static int cv181x_csi2_init_state(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format format = {
		.pad = CV181X_CSI2_PAD_SINK,
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = CV181X_CSI2_DEFAULT_WIDTH,
			.height = CV181X_CSI2_DEFAULT_HEIGHT,
			.code = MEDIA_BUS_FMT_UYVY8_1X16,
			.field = V4L2_FIELD_NONE,
		},
	};

	return cv181x_csi2_set_format(sd, state, &format);
}

static int cv181x_csi2_enable_streams(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      u32 pad, u64 streams_mask)
{
	struct cv181x_camera_dev *cam = sd_to_cv181x_csi2(sd);
	int ret;

	if (pad != CV181X_CSI2_PAD_SOURCE || streams_mask != BIT(0))
		return -EINVAL;
	if (!cam->source)
		return -ENOLINK;

	ret = cv181x_csi2_start(cam);
	if (ret)
		return dev_err_probe(cam->dev, ret,
				     "failed to start CSI-2 receiver\n");

	ret = v4l2_subdev_enable_streams(cam->source, 0, BIT(0));
	if (ret && ret != -ENOIOCTLCMD) {
		cv181x_csi2_stop(cam);
		return ret;
	}

	return 0;
}

static int cv181x_csi2_disable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       u32 pad, u64 streams_mask)
{
	struct cv181x_camera_dev *cam = sd_to_cv181x_csi2(sd);

	if (pad != CV181X_CSI2_PAD_SOURCE || streams_mask != BIT(0))
		return -EINVAL;

	cv181x_csi2_stop(cam);
	if (cam->source)
		v4l2_subdev_disable_streams(cam->source, 0, BIT(0));

	return 0;
}

static const struct v4l2_subdev_pad_ops cv181x_csi2_pad_ops = {
	.enum_mbus_code = cv181x_csi2_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = cv181x_csi2_set_format,
	.enable_streams = cv181x_csi2_enable_streams,
	.disable_streams = cv181x_csi2_disable_streams,
};

const struct v4l2_subdev_ops cv181x_csi2_subdev_ops = {
	.pad = &cv181x_csi2_pad_ops,
};
EXPORT_SYMBOL_GPL(cv181x_csi2_subdev_ops);

const struct v4l2_subdev_internal_ops cv181x_csi2_internal_ops = {
	.init_state = cv181x_csi2_init_state,
};
EXPORT_SYMBOL_GPL(cv181x_csi2_internal_ops);

static void cv181x_csi2_update_bits(void __iomem *base, u32 offset,
				    u32 mask, u32 value)
{
	u32 reg = readl(base + offset);

	reg &= ~mask;
	reg |= value & mask;
	writel(reg, base + offset);
}

static u32 cv181x_csi2_phy_offset(const struct cv181x_camera_dev *cam)
{
	return cam->bus.num_data_lanes > 2 ? CV181X_CSI2_WRAP_PHY4L :
					       CV181X_CSI2_WRAP_PHY2L;
}

static void cv181x_csi2_configure_mac(struct cv181x_camera_dev *cam)
{
	u32 lanes = cam->bus.num_data_lanes;
	u32 phy = cv181x_csi2_phy_offset(cam);

	cv181x_csi2_update_bits(cam->csi_mac, CV181X_CSI2_MAC_SENSOR_00,
				GENMASK(2, 0) | CV181X_CSI2_SENSOR_ENABLE |
				CV181X_CSI2_SENSOR_VS_INV |
				CV181X_CSI2_SENSOR_HS_INV,
				CV181X_CSI2_SENSOR_MODE_CSI |
				CV181X_CSI2_SENSOR_ENABLE |
				CV181X_CSI2_SENSOR_VS_INV |
				CV181X_CSI2_SENSOR_HS_INV);
	cv181x_csi2_update_bits(cam->csi_mac, CV181X_CSI2_MAC_CSI_00,
				CV181X_CSI2_LANE_MODE_MASK,
				FIELD_PREP(CV181X_CSI2_LANE_MODE_MASK,
					   lanes - 1));
	writel(CV181X_CSI2_VC_MAP_IDENTITY,
	       cam->csi_mac + CV181X_CSI2_MAC_CSI_18);
	cv181x_csi2_update_bits(cam->csi_mac, CV181X_CSI2_MAC_CSI_70,
				CV181X_CSI2_VS_GEN_MODE_MASK, 0);
	cv181x_csi2_update_bits(cam->csi_wrap,
				phy + CV181X_CSI2_WRAP_PHY_10,
				CV181X_CSI2_WRAP_AUTO_IGNORE |
				CV181X_CSI2_WRAP_AUTO_SYNC |
				CV181X_CSI2_WRAP_HS_SETTLE_MASK,
				CV181X_CSI2_WRAP_HS_SETTLE);
	cv181x_csi2_update_bits(cam->csi_mac, CV181X_CSI2_MAC_SENSOR_00,
				CV181X_CSI2_SENSOR_SW_UPDATE,
				CV181X_CSI2_SENSOR_SW_UPDATE);
}

static void cv181x_csi2_enable_lanes(struct cv181x_camera_dev *cam,
				     bool enable)
{
	u32 lanes = cam->bus.num_data_lanes;
	u32 mask = lanes > 2 ? GENMASK(3, 0) : GENMASK(1, 0);
	u32 value = enable ? GENMASK(lanes - 1, 0) : 0;

	cv181x_csi2_update_bits(cam->csi_wrap,
				cv181x_csi2_phy_offset(cam) +
				CV181X_CSI2_WRAP_PHY_0C, mask, value);
}

static void cv181x_csi2_power(struct cv181x_camera_dev *cam, bool enable)
{
	u32 rxlp = enable ? 0 :
		FIELD_PREP(CV181X_CSI2_WRAP_PD_RXLP_MASK, 0x3f);
	u32 value = enable ? 0 : CV181X_CSI2_WRAP_PD_IBIAS;

	cv181x_csi2_update_bits(cam->csi_wrap, CV181X_CSI2_WRAP_TOP_00,
				CV181X_CSI2_WRAP_PD_IBIAS, value);
	cv181x_csi2_update_bits(cam->csi_wrap, CV181X_CSI2_WRAP_TOP_00,
				CV181X_CSI2_WRAP_PD_RXLP_MASK, rxlp);
	if (enable)
		usleep_range(20, 40);
}

static bool cv181x_csi2_lane_is_port1(u32 lane)
{
	return lane > 2;
}

void cv181x_csi2_init_lane_deskew(struct cv181x_camera_dev *cam)
{
	u32 clock_lane = cam->bus.clock_lane;
	u32 i;

	memset(cam->lane_phase, 0, sizeof(cam->lane_phase));
	cam->lane_phase[0] = CV181X_CSI2_CLK_PHASE;

	for (i = 0; i < cam->bus.num_data_lanes; i++)
		cam->lane_phase[i + 1] =
			cv181x_csi2_lane_is_port1(cam->bus.data_lanes[i]) ==
			cv181x_csi2_lane_is_port1(clock_lane) ?
			CV181X_CSI2_DATA_NEAR_PHASE :
			CV181X_CSI2_DATA_FAR_PHASE;
}
EXPORT_SYMBOL_GPL(cv181x_csi2_init_lane_deskew);

static int cv181x_csi2_configure_lanes(struct cv181x_camera_dev *cam)
{
	static const u32 lane_masks[] = {
		CV181X_CSI2_WRAP_DATA0_MASK,
		CV181X_CSI2_WRAP_DATA1_MASK,
		CV181X_CSI2_WRAP_DATA2_MASK,
		CV181X_CSI2_WRAP_DATA3_MASK,
	};
	static const u32 lane_shifts[] = { 0, 4, 8, 12 };
	u32 lanes = cam->bus.num_data_lanes;
	u32 clock_lane = cam->bus.clock_lane;
	u32 phy = cv181x_csi2_phy_offset(cam);
	u32 i;

	if (!lanes || lanes > ARRAY_SIZE(lane_masks))
		return dev_err_probe(cam->dev, -EINVAL,
				     "invalid number of CSI-2 data lanes: %u\n",
				     lanes);
	if (clock_lane > CV181X_CSI2_MAX_PHYSICAL_LANE)
		return dev_err_probe(cam->dev, -EINVAL,
				     "invalid CSI-2 clock lane: %u\n",
				     clock_lane);

	for (i = 0; i < lanes; i++) {
		if (cam->bus.data_lanes[i] > CV181X_CSI2_MAX_PHYSICAL_LANE)
			return dev_err_probe(cam->dev, -EINVAL,
					     "invalid CSI-2 data lane %u: %u\n",
					     i, cam->bus.data_lanes[i]);
	}

	cv181x_csi2_update_bits(cam->csi_wrap,
				CV181X_CSI2_WRAP_ANALOG_CTRL,
				CV181X_CSI2_WRAP_CLK_LANE_MASK,
				FIELD_PREP(CV181X_CSI2_WRAP_CLK_LANE_MASK,
					   BIT(clock_lane)));
	cv181x_csi2_update_bits(cam->csi_wrap,
				CV181X_CSI2_WRAP_ANALOG_CTRL,
				CV181X_CSI2_WRAP_CLK_P0_TO_P1 |
				CV181X_CSI2_WRAP_CLK_P1_TO_P0,
				lanes <= 2 ? 0 :
				cv181x_csi2_lane_is_port1(clock_lane) ?
				CV181X_CSI2_WRAP_CLK_P1_TO_P0 :
				CV181X_CSI2_WRAP_CLK_P0_TO_P1);
	cv181x_csi2_update_bits(cam->csi_wrap,
				phy + CV181X_CSI2_WRAP_PHY_08,
				CV181X_CSI2_WRAP_PHY_CLK_MASK,
				FIELD_PREP(CV181X_CSI2_WRAP_PHY_CLK_MASK,
					   clock_lane));
	cv181x_csi2_update_bits(cam->csi_wrap,
				phy + CV181X_CSI2_WRAP_PHY_08,
				CV181X_CSI2_WRAP_PHY_CLK_SWAP,
				cam->bus.lane_polarities[0] ?
				CV181X_CSI2_WRAP_PHY_CLK_SWAP : 0);
	cv181x_csi2_update_bits(cam->csi_wrap, CV181X_CSI2_WRAP_PHY_80,
				CV181X_CSI2_WRAP_CLK_INV_MASK,
				CV181X_CSI2_WRAP_CLK_INV_MASK);

	for (i = 0; i < lanes; i++) {
		cv181x_csi2_update_bits(cam->csi_wrap,
					phy + CV181X_CSI2_WRAP_PHY_04,
					lane_masks[i],
					cam->bus.data_lanes[i] << lane_shifts[i]);
		cv181x_csi2_update_bits(cam->csi_wrap,
					phy + CV181X_CSI2_WRAP_PHY_08,
					CV181X_CSI2_WRAP_DATA_SWAP(i),
					cam->bus.lane_polarities[i + 1] ?
					CV181X_CSI2_WRAP_DATA_SWAP(i) : 0);
	}

	return 0;
}

static void cv181x_csi2_set_lane_deskew(struct cv181x_camera_dev *cam,
					u32 lane, u8 phase)
{
	static const u32 offsets[] = {
		CV181X_CSI2_WRAP_PHY_84, CV181X_CSI2_WRAP_PHY_84,
		CV181X_CSI2_WRAP_PHY_84, CV181X_CSI2_WRAP_PHY_84,
		CV181X_CSI2_WRAP_PHY_88, CV181X_CSI2_WRAP_PHY_88,
	};
	static const u32 shifts[] = { 0, 8, 16, 24, 0, 8 };

	cv181x_csi2_update_bits(cam->csi_wrap, offsets[lane],
				GENMASK(7 + shifts[lane], shifts[lane]),
				phase << shifts[lane]);
	cv181x_csi2_update_bits(cam->csi_wrap, CV181X_CSI2_WRAP_PHY_80,
				BIT(16 + lane), phase ? BIT(16 + lane) : 0);
}

static void cv181x_csi2_configure_deskew(struct cv181x_camera_dev *cam)
{
	u32 i;

	cv181x_csi2_set_lane_deskew(cam, cam->bus.clock_lane,
				    cam->lane_phase[0]);
	for (i = 0; i < cam->bus.num_data_lanes; i++)
		cv181x_csi2_set_lane_deskew(cam, cam->bus.data_lanes[i],
					    cam->lane_phase[i + 1]);
}

static int cv181x_csi2_reset(struct cv181x_camera_dev *cam)
{
	struct reset_control *phy = cam->resets[CV181X_CSI2_RST_PHY0].rstc;
	struct reset_control *apb = cam->resets[CV181X_CSI2_RST_PHY_APB0].rstc;
	int ret;

	ret = reset_control_assert(phy);
	if (ret)
		return ret;
	ret = reset_control_assert(apb);
	if (ret)
		return ret;
	udelay(5);
	ret = reset_control_deassert(phy);
	if (ret)
		return ret;
	ret = reset_control_deassert(apb);
	if (ret)
		return ret;

	cv181x_csi2_update_bits(cam->vip_sys, CV181X_VIP_SYS_RESET,
				CV181X_VIP_SYS_RESET_CSI_MAC0,
				CV181X_VIP_SYS_RESET_CSI_MAC0);
	usleep_range(20, 40);
	cv181x_csi2_update_bits(cam->vip_sys, CV181X_VIP_SYS_RESET,
				CV181X_VIP_SYS_RESET_CSI_MAC0, 0);

	return 0;
}

int cv181x_csi2_start(struct cv181x_camera_dev *cam)
{
	int ret;

	ret = cv181x_csi2_reset(cam);
	if (ret)
		return ret;

	cv181x_csi2_power(cam, false);
	cv181x_csi2_enable_lanes(cam, false);
	ret = cv181x_csi2_configure_lanes(cam);
	if (ret)
		return ret;
	cv181x_csi2_configure_deskew(cam);
	cv181x_csi2_configure_mac(cam);
	cv181x_csi2_power(cam, true);
	cv181x_csi2_enable_lanes(cam, true);

	return 0;
}
EXPORT_SYMBOL_GPL(cv181x_csi2_start);

void cv181x_csi2_stop(struct cv181x_camera_dev *cam)
{
	cv181x_csi2_enable_lanes(cam, false);
	cv181x_csi2_power(cam, false);
}
EXPORT_SYMBOL_GPL(cv181x_csi2_stop);

MODULE_DESCRIPTION("Sophgo CV181x MIPI CSI-2 receiver support");
MODULE_LICENSE("GPL");
