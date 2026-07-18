/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __CV181X_CAMERA_H__
#define __CV181X_CAMERA_H__

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "cv181x-components.h"

#define CV181X_CSI2_DEFAULT_WIDTH	640
#define CV181X_CSI2_DEFAULT_HEIGHT	480
#define CV181X_CSI2_MAX_WIDTH		4096
#define CV181X_CSI2_MAX_HEIGHT		4096
#define CV181X_CSI2_NUM_LANE_PHASES	5

enum cv181x_csi2_pad {
	CV181X_CSI2_PAD_SINK,
	CV181X_CSI2_PAD_SOURCE,
	CV181X_CSI2_NUM_PADS,
};

enum cv181x_csi2_reset {
	CV181X_CSI2_RST_VIPSYS,
	CV181X_CSI2_RST_CAM0,
	CV181X_CSI2_RST_PHY0,
	CV181X_CSI2_RST_PHY1,
	CV181X_CSI2_RST_PHY_APB0,
	CV181X_CSI2_RST_PHY_APB1,
	CV181X_CSI2_NUM_RESETS,
};

struct cv181x_camera_format {
	u32 fourcc;
	u32 mbus_code;
	u32 csi_dt;
};

struct cv181x_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct cv181x_camera_dev {
	struct device *dev;
	void __iomem *csi_mac;
	void __iomem *csi_wrap;
	void __iomem *vi;
	void __iomem *vip_sys;

	struct clk_bulk_data *clks;
	struct clk *axi_clk;
	int num_clks;
	struct reset_control_bulk_data resets[CV181X_CSI2_NUM_RESETS];
	int irq;

	struct media_device mdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev csi2_subdev;
	struct v4l2_subdev *source;
	struct media_pad csi2_pads[CV181X_CSI2_NUM_PADS];
	struct media_pipeline pipe;

	struct video_device vdev;
	struct media_pad video_pad;
	struct vb2_queue queue;
	struct mutex lock; /* Serializes V4L2 file operations. */
	spinlock_t qlock; /* Protects queued and active buffers. */
	struct list_head queued;
	struct cv181x_buffer *active;

	struct v4l2_pix_format pix;
	const struct cv181x_camera_format *fmt;
	struct v4l2_mbus_config_mipi_csi2 bus;
	u8 lane_phase[CV181X_CSI2_NUM_LANE_PHASES];
	u32 sequence;
	bool streaming;
};

extern const struct cv181x_camera_format cv181x_camera_formats[];
extern const unsigned int cv181x_num_camera_formats;
extern const unsigned int cv181x_num_vi_formats;
extern const struct v4l2_subdev_ops cv181x_csi2_subdev_ops;
extern const struct v4l2_subdev_internal_ops cv181x_csi2_internal_ops;

const struct cv181x_camera_format *cv181x_camera_find_fourcc(u32 fourcc);
const struct cv181x_camera_format *cv181x_camera_find_mbus(u32 code);
const struct cv181x_camera_format *cv181x_camera_vi_format(unsigned int index);
void cv181x_camera_fill_pix(struct v4l2_pix_format *pix,
			    const struct cv181x_camera_format *fmt,
			    u32 width, u32 height);
int cv181x_csi2_set_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt);

#endif
