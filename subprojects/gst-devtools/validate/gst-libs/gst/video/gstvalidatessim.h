/* GStreamer
 *
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GST_VALIDATE_SSIM_H
#define _GST_VALIDATE_SSIM_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <gst/gst.h>
#include <glib-object.h>
#include <gst/video/video.h>
#include <gst/validate/validate.h>

G_BEGIN_DECLS

typedef struct _GstValidateSsimPrivate GstValidateSsimPrivate;

typedef struct {
  GstObject parent;

  GstValidateSsimPrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
} GstValidateSsim;

typedef struct {
  GstObjectClass parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
} GstValidateSsimClass;

#define GST_VALIDATE_SSIM_TIME_FORMAT "u-%02u-%02u.%09u"

#define GST_VALIDATE_SSIM_TYPE (gst_validate_ssim_get_type ())
#define GST_VALIDATE_SSIM(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_VALIDATE_SSIM_TYPE, GstValidateSsim))
#define GST_VALIDATE_SSIM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_VALIDATE_SSIM_TYPE, GstValidateSsimClass))
#define IS_GST_VALIDATE_SSIM(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_VALIDATE_SSIM_TYPE))
#define IS_GST_VALIDATE_SSIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_VALIDATE_SSIM_TYPE))
#define GST_VALIDATE_SSIM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_VALIDATE_SSIM_TYPE, GstValidateSsimClass))

GType gst_validate_ssim_get_type                (void);

GstValidateSsim * gst_validate_ssim_new         (GstValidateRunner *runner,
                                                 gfloat min_avg_similarity,
                                                 gfloat min_lowest_similarity,
                                                 gint fps_n,
                                                 gint fps_d);

gboolean gst_validate_ssim_compare_image_files  (GstValidateSsim *self, const gchar *ref_file,
                                                 const gchar * file, gfloat * mean, gfloat * lowest,
                                                 gfloat * highest, const gchar *outfolder);

void gst_validate_ssim_compare_frames           (GstValidateSsim * self, GstVideoFrame *ref_frame,
                                                 GstVideoFrame *frame, GstBuffer **outbuf,
                                                 gfloat * mean, gfloat * lowest, gfloat * highest);

G_END_DECLS

#endif
