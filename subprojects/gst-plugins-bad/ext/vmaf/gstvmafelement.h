/* VMAF plugin
 * Copyright (C) 2021 Hudl
 *   @author: Casey Bateman <Casey.Bateman@hudl.com>
 * Copyright (C) 2025 Fluendo S.A. <contact@fluendo.com>
 *   Authors: Diego Nieto <dnieto@fluendo.com>
 *   Authors: Andoni Morales <amorales@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:plugin-vmaf
 *
 * Provides Video Multi-Method Assessment Fusion quality metrics.
 *
 * Since: 1.28
 */

#ifndef __GST_VMAFELEMENT_H__
#define __GST_VMAFELEMENT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

#include <libvmaf.h>

G_BEGIN_DECLS
#define GST_TYPE_VMAF (gst_vmaf_get_type())
#define GST_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMAF, GstVmaf))
#define GST_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VMAF, GstVmafClass))
#define GST_IS_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VMAF))
#define GST_IS_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VMAF))
typedef struct _GstVmaf GstVmaf;
typedef struct _GstVmafClass GstVmafClass;

struct _GstVmaf
{
  GstVideoAggregator videoaggregator;

  GstVideoAggregatorPad *ref_pad;
  GstVideoAggregatorPad *dist_pad;

  // VMAF settings
  enum VmafPoolingMethod vmaf_config_pool_method;
  enum VmafOutputFormat vmaf_config_results_format;
  gchar *vmaf_config_model_filename;
  gboolean vmaf_config_disable_clip;
  gboolean vmaf_config_enable_transform;
  gboolean vmaf_config_phone_model;
  gboolean vmaf_config_psnr;
  gboolean vmaf_config_ssim;
  gboolean vmaf_config_ms_ssim;
  guint vmaf_config_num_threads;
  guint vmaf_config_subsample;
  gboolean vmaf_config_conf_int;
  gboolean vmaf_config_frame_messaging;
  gchar *vmaf_config_results_filename;
  enum VmafLogLevel vmaf_config_log_level;

  // Process state
  gboolean flushed;
  gboolean initialized;

  gint processed_frames;
  enum VmafPixelFormat pix_fmt;

  VmafContext *vmaf_ctx;
  VmafModel *vmaf_model;
  VmafModelCollection *vmaf_model_collection;
};

struct _GstVmafClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_vmaf_get_type (void);

G_END_DECLS
#endif /* __GST_VMAFELEMENT_H__ */
