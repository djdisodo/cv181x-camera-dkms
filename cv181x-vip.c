// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sophgo CV181X CSI/VI capture driver
 *
 * Copyright (C) 2026 Sodo
 */

#include <linux/clk.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "cv181x-camera.h"

#define CV181X_VIP_DRIVER_NAME		"cv181x-vip"
#define CV181X_CSI_MIN_QUEUED_BUFS	2
#define CV181X_CSI_DEFAULT_AXI_RATE	396000000

static inline struct cv181x_camera_dev *video_to_csi(struct video_device *vdev)
{
	return container_of(vdev, struct cv181x_camera_dev, vdev);
}

static void cv181x_csi_return_buffers(struct cv181x_camera_dev *csi,
				      enum vb2_buffer_state state)
{
	struct cv181x_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&csi->qlock, flags);
	if (csi->active) {
		vb2_buffer_done(&csi->active->vb.vb2_buf, state);
		csi->active = NULL;
	}
	list_for_each_entry_safe(buf, tmp, &csi->queued, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&csi->qlock, flags);
}

static struct cv181x_buffer *
cv181x_csi_next_buffer_locked(struct cv181x_camera_dev *csi)
{
	struct cv181x_buffer *buf;

	if (list_empty(&csi->queued))
		return NULL;

	buf = list_first_entry(&csi->queued, struct cv181x_buffer, list);
	list_del(&buf->list);

	return buf;
}

static void cv181x_csi_complete_active(struct cv181x_camera_dev *csi,
				       enum vb2_buffer_state state)
{
	struct cv181x_buffer *done = NULL;
	dma_addr_t addr = 0;
	unsigned long flags;
	bool arm = false;

	spin_lock_irqsave(&csi->qlock, flags);
	done = csi->active;
	csi->active = cv181x_csi_next_buffer_locked(csi);
	if (csi->active) {
		addr = vb2_dma_contig_plane_dma_addr(&csi->active->vb.vb2_buf,
						     0);
		arm = true;
	}
	if (done) {
		done->vb.sequence = csi->sequence++;
		done->vb.field = V4L2_FIELD_NONE;
		done->vb.vb2_buf.timestamp = ktime_get_ns();
		vb2_set_plane_payload(&done->vb.vb2_buf, 0, csi->pix.sizeimage);
	}
	spin_unlock_irqrestore(&csi->qlock, flags);

	if (done)
		vb2_buffer_done(&done->vb.vb2_buf, state);

	if (arm) {
		cv181x_vi_dma_set_addr(csi, addr);
		cv181x_isp_trigger(csi);
	}
}

static int cv181x_csi_hw_start(struct cv181x_camera_dev *csi)
{
	struct cv181x_buffer *buf;
	dma_addr_t addr;
	unsigned long flags;

	if (csi->irq <= 0)
		return dev_err_probe(csi->dev, -ENXIO, "missing ISP IRQ\n");

	if (!csi->source)
		return dev_err_probe(csi->dev, -ENOLINK,
				     "missing CSI source subdevice\n");

	spin_lock_irqsave(&csi->qlock, flags);
	csi->active = cv181x_csi_next_buffer_locked(csi);
	buf = csi->active;
	spin_unlock_irqrestore(&csi->qlock, flags);
	if (!buf)
		return -ENOBUFS;

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);

	cv181x_vi_dma_configure(csi, addr);
	cv181x_vi_start(csi);
	cv181x_isp_start(csi);
	cv181x_vi_enable(csi);

	return 0;
}

static void cv181x_csi_hw_stop(struct cv181x_camera_dev *csi)
{
	cv181x_isp_stop(csi);
	cv181x_vi_stop(csi);
}

static irqreturn_t cv181x_csi_irq(int irq, void *data)
{
	struct cv181x_camera_dev *csi = data;
	bool frame_done = cv181x_isp_irq_status(csi);
	bool vi_status = cv181x_vi_irq_status(csi);

	if (!frame_done && !vi_status)
		return IRQ_NONE;

	if (READ_ONCE(csi->streaming) && frame_done)
		cv181x_csi_complete_active(csi, VB2_BUF_STATE_DONE);

	return IRQ_HANDLED;
}

static int cv181x_csi_queue_setup(struct vb2_queue *q, unsigned int *nbufs,
				  unsigned int *nplanes, unsigned int sizes[],
				  struct device *alloc_devs[])
{
	struct cv181x_camera_dev *csi = vb2_get_drv_priv(q);

	if (*nplanes) {
		if (sizes[0] < csi->pix.sizeimage)
			return -EINVAL;
		return 0;
	}

	if (*nbufs < CV181X_CSI_MIN_QUEUED_BUFS)
		*nbufs = CV181X_CSI_MIN_QUEUED_BUFS;

	*nplanes = 1;
	sizes[0] = csi->pix.sizeimage;

	return 0;
}

static int cv181x_csi_buf_prepare(struct vb2_buffer *vb)
{
	struct cv181x_camera_dev *csi = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < csi->pix.sizeimage)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, csi->pix.sizeimage);

	return 0;
}

static void cv181x_csi_buf_queue(struct vb2_buffer *vb)
{
	struct cv181x_camera_dev *csi = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cv181x_buffer *buf =
		container_of(vbuf, struct cv181x_buffer, vb);
	dma_addr_t addr = 0;
	unsigned long flags;
	bool arm = false;

	spin_lock_irqsave(&csi->qlock, flags);
	if (READ_ONCE(csi->streaming) && !csi->active) {
		csi->active = buf;
		addr = vb2_dma_contig_plane_dma_addr(vb, 0);
		arm = true;
	} else {
		list_add_tail(&buf->list, &csi->queued);
	}
	spin_unlock_irqrestore(&csi->qlock, flags);

	if (arm) {
		cv181x_vi_dma_set_addr(csi, addr);
		cv181x_isp_trigger(csi);
	}
}

static int cv181x_csi_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cv181x_camera_dev *csi = vb2_get_drv_priv(q);
	int ret;

	ret = pm_runtime_resume_and_get(csi->dev);
	if (ret < 0)
		goto err_buffers;

	ret = video_device_pipeline_start(&csi->vdev, &csi->pipe);
	if (ret)
		goto err_pm;

	ret = cv181x_csi_hw_start(csi);
	if (ret)
		goto err_pipe;

	ret = v4l2_subdev_enable_streams(&csi->csi2_subdev,
					 CV181X_CSI2_PAD_SOURCE, BIT(0));
	if (ret)
		goto err_hw;

	csi->sequence = 0;
	WRITE_ONCE(csi->streaming, true);
	cv181x_isp_trigger(csi);

	return 0;

err_hw:
	cv181x_csi_hw_stop(csi);
err_pipe:
	video_device_pipeline_stop(&csi->vdev);
err_pm:
	pm_runtime_put(csi->dev);
err_buffers:
	cv181x_csi_return_buffers(csi, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void cv181x_csi_stop_streaming(struct vb2_queue *q)
{
	struct cv181x_camera_dev *csi = vb2_get_drv_priv(q);

	WRITE_ONCE(csi->streaming, false);
	cv181x_csi_hw_stop(csi);
	v4l2_subdev_disable_streams(&csi->csi2_subdev,
				    CV181X_CSI2_PAD_SOURCE, BIT(0));

	video_device_pipeline_stop(&csi->vdev);
	pm_runtime_put(csi->dev);
	cv181x_csi_return_buffers(csi, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops cv181x_csi_vb2_ops = {
	.queue_setup = cv181x_csi_queue_setup,
	.buf_prepare = cv181x_csi_buf_prepare,
	.buf_queue = cv181x_csi_buf_queue,
	.start_streaming = cv181x_csi_start_streaming,
	.stop_streaming = cv181x_csi_stop_streaming,
};

static int cv181x_csi_querycap(struct file *file, void *priv,
			       struct v4l2_capability *cap)
{
	strscpy(cap->driver, CV181X_VIP_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Sophgo CV181x camera", sizeof(cap->card));

	return 0;
}

static int cv181x_csi_enum_fmt(struct file *file, void *priv,
			       struct v4l2_fmtdesc *f)
{
	if (f->index >= cv181x_num_vi_formats)
		return -EINVAL;

	f->pixelformat = cv181x_camera_vi_format(f->index)->fourcc;

	return 0;
}

static int cv181x_csi_g_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct cv181x_camera_dev *csi = video_drvdata(file);

	f->fmt.pix = csi->pix;

	return 0;
}

static int cv181x_csi_try_fmt(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	const struct cv181x_camera_format *fmt;

	fmt = cv181x_camera_find_fourcc(f->fmt.pix.pixelformat);
	cv181x_camera_fill_pix(&f->fmt.pix, fmt, f->fmt.pix.width,
			       f->fmt.pix.height);

	return 0;
}

static int cv181x_csi_s_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct cv181x_camera_dev *csi = video_drvdata(file);
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_format sd_fmt = {
		.pad = CV181X_CSI2_PAD_SINK,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	if (vb2_is_busy(&csi->queue))
		return -EBUSY;

	ret = cv181x_csi_try_fmt(file, priv, f);
	if (ret)
		return ret;

	csi->fmt = cv181x_camera_find_fourcc(f->fmt.pix.pixelformat);
	sd_fmt.format.width = f->fmt.pix.width;
	sd_fmt.format.height = f->fmt.pix.height;
	sd_fmt.format.code = csi->fmt->mbus_code;

	state = v4l2_subdev_lock_and_get_active_state(&csi->csi2_subdev);
	ret = cv181x_csi2_set_format(&csi->csi2_subdev, state, &sd_fmt);
	v4l2_subdev_unlock_state(state);
	if (ret)
		return ret;

	f->fmt.pix = csi->pix;

	return 0;
}

static int cv181x_csi_enum_input(struct file *file, void *priv,
				 struct v4l2_input *input)
{
	if (input->index)
		return -EINVAL;

	strscpy(input->name, "CSI-2", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int cv181x_csi_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int cv181x_csi_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static const struct v4l2_ioctl_ops cv181x_csi_ioctl_ops = {
	.vidioc_querycap = cv181x_csi_querycap,
	.vidioc_enum_fmt_vid_cap = cv181x_csi_enum_fmt,
	.vidioc_g_fmt_vid_cap = cv181x_csi_g_fmt,
	.vidioc_try_fmt_vid_cap = cv181x_csi_try_fmt,
	.vidioc_s_fmt_vid_cap = cv181x_csi_s_fmt,
	.vidioc_enum_input = cv181x_csi_enum_input,
	.vidioc_g_input = cv181x_csi_g_input,
	.vidioc_s_input = cv181x_csi_s_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations cv181x_csi_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static const struct media_entity_operations cv181x_csi_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int cv181x_csi_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_connection *asc)
{
	struct cv181x_camera_dev *csi =
		container_of(notifier, struct cv181x_camera_dev, notifier);
	int ret;

	csi->source = sd;

	ret = media_create_pad_link(&sd->entity, 0, &csi->csi2_subdev.entity,
				    CV181X_CSI2_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		csi->source = NULL;

	return ret;
}

static void cv181x_csi_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *sd,
				     struct v4l2_async_connection *asc)
{
	struct cv181x_camera_dev *csi =
		container_of(notifier, struct cv181x_camera_dev, notifier);

	if (csi->source == sd)
		csi->source = NULL;
}

static const struct v4l2_async_notifier_operations cv181x_csi_notify_ops = {
	.bound = cv181x_csi_notify_bound,
	.unbind = cv181x_csi_notify_unbind,
};

static int cv181x_csi_parse_fwnode(struct cv181x_camera_dev *csi)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return dev_err_probe(csi->dev, -ENODEV,
				     "missing CSI sink endpoint\n");

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret) {
		fwnode_handle_put(ep);
		return dev_err_probe(csi->dev, ret,
				     "failed to parse CSI endpoint\n");
	}

	csi->bus = vep.bus.mipi_csi2;
	cv181x_csi2_init_lane_deskew(csi);
	if (device_property_present(csi->dev, "sophgo,lane-deskew")) {
		ret = device_property_read_u8_array(csi->dev,
						    "sophgo,lane-deskew",
						    csi->lane_phase,
						    ARRAY_SIZE(csi->lane_phase));
		if (ret) {
			fwnode_handle_put(ep);
			return dev_err_probe(csi->dev, ret,
					     "invalid lane deskew values\n");
		}
	}

	asc = v4l2_async_nf_add_fwnode_remote(&csi->notifier, ep,
					      struct v4l2_async_connection);
	fwnode_handle_put(ep);
	if (IS_ERR(asc))
		return dev_err_probe(csi->dev, PTR_ERR(asc),
				     "failed to add remote endpoint\n");

	return 0;
}

static int cv181x_csi_init_video(struct cv181x_camera_dev *csi)
{
	struct vb2_queue *q = &csi->queue;
	struct video_device *vdev = &csi->vdev;
	int ret;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = csi;
	q->buf_struct_size = sizeof(struct cv181x_buffer);
	q->ops = &cv181x_csi_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = CV181X_CSI_MIN_QUEUED_BUFS;
	q->lock = &csi->lock;
	q->dev = csi->dev;

	ret = vb2_queue_init(q);
	if (ret)
		return dev_err_probe(csi->dev, ret,
				     "failed to initialize vb2 queue\n");

	strscpy(vdev->name, "cv181x-vi-capture", sizeof(vdev->name));
	vdev->v4l2_dev = &csi->v4l2_dev;
	vdev->fops = &cv181x_csi_fops;
	vdev->ioctl_ops = &cv181x_csi_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->lock = &csi->lock;
	vdev->queue = q;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->entity.function = MEDIA_ENT_F_IO_V4L;
	video_set_drvdata(vdev, csi);

	csi->video_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &csi->video_pad);
	if (ret)
		return dev_err_probe(csi->dev, ret,
				     "failed to initialize video pads\n");

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_entity;

	ret = media_create_pad_link(&csi->csi2_subdev.entity, CV181X_CSI2_PAD_SOURCE,
				    &vdev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		goto err_create_link;

	return 0;

err_create_link:
	dev_err_probe(csi->dev, ret, "failed to create video pad link\n");
	video_unregister_device(vdev);
	return ret;
err_entity:
	media_entity_cleanup(&vdev->entity);
	return ret;
}

static void cv181x_csi_cleanup_video(struct cv181x_camera_dev *csi)
{
	video_unregister_device(&csi->vdev);
	media_entity_cleanup(&csi->vdev.entity);
	vb2_queue_release(&csi->queue);
}

static int cv181x_csi_probe(struct platform_device *pdev)
{
	static const char * const reset_names[] = {
		[CV181X_CSI2_RST_VIPSYS] = "vipsys",
		[CV181X_CSI2_RST_CAM0] = "cam0",
		[CV181X_CSI2_RST_PHY0] = "phy0",
		[CV181X_CSI2_RST_PHY1] = "phy1",
		[CV181X_CSI2_RST_PHY_APB0] = "phy-apb0",
		[CV181X_CSI2_RST_PHY_APB1] = "phy-apb1",
	};
	struct cv181x_camera_dev *csi;
	struct device *dev = &pdev->dev;
	int ret;

	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = dev;
	platform_set_drvdata(pdev, csi);
	mutex_init(&csi->lock);
	spin_lock_init(&csi->qlock);
	INIT_LIST_HEAD(&csi->queued);

	csi->csi_mac = devm_platform_ioremap_resource_byname(pdev, "csi-mac");
	if (IS_ERR(csi->csi_mac))
		return PTR_ERR(csi->csi_mac);

	csi->csi_wrap = devm_platform_ioremap_resource_byname(pdev, "csi-wrap");
	if (IS_ERR(csi->csi_wrap))
		return PTR_ERR(csi->csi_wrap);

	csi->vi = devm_platform_ioremap_resource_byname(pdev, "vi");
	if (IS_ERR(csi->vi))
		return PTR_ERR(csi->vi);

	csi->vip_sys = devm_platform_ioremap_resource_byname(pdev, "vip-sys");
	if (IS_ERR(csi->vip_sys))
		return PTR_ERR(csi->vip_sys);

	csi->irq = platform_get_irq_byname_optional(pdev, "isp");
	if (csi->irq == -EPROBE_DEFER)
		return csi->irq;
	if (csi->irq < 0)
		csi->irq = 0;
	if (csi->irq) {
		ret = devm_request_irq(dev, csi->irq, cv181x_csi_irq, 0,
				       dev_name(dev), csi);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to request ISP IRQ\n");
	}

	ret = devm_clk_bulk_get_all(dev, &csi->clks);
	if (ret < 0)
		return ret;
	csi->num_clks = ret;

	csi->axi_clk = devm_clk_get_optional(dev, "axi");
	if (IS_ERR(csi->axi_clk))
		return PTR_ERR(csi->axi_clk);

	if (csi->axi_clk) {
		ret = clk_set_rate(csi->axi_clk, CV181X_CSI_DEFAULT_AXI_RATE);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set AXI clock rate\n");
	}

	for (unsigned int i = 0; i < CV181X_CSI2_NUM_RESETS; i++)
		csi->resets[i].id = reset_names[i];

	ret = devm_reset_control_bulk_get_optional_exclusive(dev,
							     CV181X_CSI2_NUM_RESETS,
							     csi->resets);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(csi->num_clks, csi->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clocks\n");

	ret = reset_control_bulk_deassert(CV181X_CSI2_NUM_RESETS, csi->resets);
	if (ret)
		goto err_clks;

	csi->fmt = &cv181x_camera_formats[0];
	cv181x_camera_fill_pix(&csi->pix, csi->fmt, CV181X_CSI2_DEFAULT_WIDTH,
			       CV181X_CSI2_DEFAULT_HEIGHT);
	strscpy(csi->mdev.model, "Sophgo CV181x camera",
		sizeof(csi->mdev.model));
	csi->mdev.dev = dev;
	media_device_init(&csi->mdev);

	csi->v4l2_dev.mdev = &csi->mdev;
	ret = v4l2_device_register(dev, &csi->v4l2_dev);
	if (ret)
		goto err_register_v4l2;

	v4l2_subdev_init(&csi->csi2_subdev, &cv181x_csi2_subdev_ops);
	csi->csi2_subdev.internal_ops = &cv181x_csi2_internal_ops;
	csi->csi2_subdev.owner = THIS_MODULE;
	csi->csi2_subdev.dev = dev;
	csi->csi2_subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(csi->csi2_subdev.name, "cv181x-csi2",
		sizeof(csi->csi2_subdev.name));
	csi->csi2_subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi->csi2_subdev.entity.ops = &cv181x_csi_entity_ops;
	csi->csi2_pads[CV181X_CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	csi->csi2_pads[CV181X_CSI2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi->csi2_subdev.entity, CV181X_CSI2_NUM_PADS,
				     csi->csi2_pads);
	if (ret)
		goto err_init_subdev_pads;

	ret = v4l2_subdev_init_finalize(&csi->csi2_subdev);
	if (ret)
		goto err_finalize_subdev;

	ret = v4l2_device_register_subdev(&csi->v4l2_dev, &csi->csi2_subdev);
	if (ret)
		goto err_cleanup_subdev;

	ret = cv181x_csi_init_video(csi);
	if (ret)
		goto err_unregister_subdev;

	v4l2_async_nf_init(&csi->notifier, &csi->v4l2_dev);
	csi->notifier.ops = &cv181x_csi_notify_ops;

	ret = cv181x_csi_parse_fwnode(csi);
	if (ret)
		goto err_video;

	ret = v4l2_async_nf_register(&csi->notifier);
	if (ret)
		goto err_register_notifier;

	ret = media_device_register(&csi->mdev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register media device\n");
		goto err_register_media;
	}

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret)
		goto err_register_subdev_nodes;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

err_register_subdev_nodes:
	dev_err_probe(dev, ret, "failed to register subdevice nodes\n");
	media_device_unregister(&csi->mdev);
err_register_media:
	v4l2_async_nf_unregister(&csi->notifier);
err_register_notifier:
	dev_err_probe(dev, ret, "failed to register async notifier\n");
	v4l2_async_nf_cleanup(&csi->notifier);
err_video:
	cv181x_csi_cleanup_video(csi);
err_unregister_subdev:
	v4l2_device_unregister_subdev(&csi->csi2_subdev);
err_cleanup_subdev:
	v4l2_subdev_cleanup(&csi->csi2_subdev);
err_finalize_subdev:
	media_entity_cleanup(&csi->csi2_subdev.entity);
err_init_subdev_pads:
	v4l2_device_unregister(&csi->v4l2_dev);
err_register_v4l2:
	media_device_cleanup(&csi->mdev);
	reset_control_bulk_assert(CV181X_CSI2_NUM_RESETS, csi->resets);
err_clks:
	clk_bulk_disable_unprepare(csi->num_clks, csi->clks);
	return ret;
}

static void cv181x_csi_remove(struct platform_device *pdev)
{
	struct cv181x_camera_dev *csi = platform_get_drvdata(pdev);

	pm_runtime_disable(csi->dev);
	media_device_unregister(&csi->mdev);
	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
	cv181x_csi_cleanup_video(csi);
	v4l2_device_unregister_subdev(&csi->csi2_subdev);
	v4l2_subdev_cleanup(&csi->csi2_subdev);
	media_entity_cleanup(&csi->csi2_subdev.entity);
	v4l2_device_unregister(&csi->v4l2_dev);
	media_device_cleanup(&csi->mdev);
	reset_control_bulk_assert(CV181X_CSI2_NUM_RESETS, csi->resets);
	clk_bulk_disable_unprepare(csi->num_clks, csi->clks);
}

static const struct of_device_id cv181x_csi_of_match[] = {
	{ .compatible = "sophgo,cv181x-vip" },
	{ .compatible = "sophgo,sg2002-csi" },
	{ .compatible = "sophgo,cv181x-csi" },
	{ .compatible = "sophgo,cv181x-csi2" },
	{ .compatible = "sophgo,cv181x-camera" },
	{ }
};
MODULE_DEVICE_TABLE(of, cv181x_csi_of_match);

static struct platform_driver cv181x_csi_driver = {
	.probe = cv181x_csi_probe,
	.remove = cv181x_csi_remove,
	.driver = {
		.name = CV181X_VIP_DRIVER_NAME,
		.of_match_table = cv181x_csi_of_match,
	},
};
module_platform_driver(cv181x_csi_driver);

MODULE_DESCRIPTION("Sophgo CV181x video input pipeline driver");
MODULE_AUTHOR("Sodo");
MODULE_LICENSE("GPL");
