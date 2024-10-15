/* GStreamer H264 encoder plugin
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 * Copyright (C) 2005 Josef Zlomek <josef.zlomek@itonis.tv>
 * Copyright (C) 2016 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_X264_ENC_H__
#define __GST_X264_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include "gstencoderbitrateprofilemanager.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/* The x264.h header says this isn't needed with MinGW, but sometimes the
 * compiler is unable to correctly do the pointer indirection for us, which
 * leads to a segfault when you try to dereference any const values provided
 * by x264.dll. See: https://bugzilla.gnome.org/show_bug.cgi?id=779249
 *
 * This problem does not occur in a static build, so ignore it. */
#if defined(_WIN32) && !defined(X264_API_IMPORTS) && !defined(STATIC_BUILD)
# define X264_API_IMPORTS
#endif
#include <x264.h>

G_BEGIN_DECLS

#define GST_TYPE_X264_ENC \
  (gst_x264_enc_get_type())
#define GST_X264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_X264_ENC,GstX264Enc))
#define GST_X264_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_X264_ENC,GstX264EncClass))
#define GST_IS_X264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_X264_ENC))
#define GST_IS_X264_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_X264_ENC))

typedef struct _GstX264Enc GstX264Enc;
typedef struct _GstX264EncClass GstX264EncClass;
typedef struct _GstX264EncVTable GstX264EncVTable;

struct _GstX264Enc
{
  GstVideoEncoder element;

  /*< private >*/
  GstX264EncVTable *vtable;
  x264_t *x264enc;
  x264_param_t x264param;
  gint current_byte_stream;

  /* List of frame/buffer mapping structs for
   * pending frames */
  GList *pending_frames;

  /* properties */
  guint threads;
  gboolean sliced_threads;
  gint sync_lookahead;
  gint pass;
  guint quantizer;
  gchar *mp_cache_file;
  gboolean byte_stream;
  guint bitrate;
  gboolean intra_refresh;
  gint me;
  guint subme;
  guint analyse;
  gboolean dct8x8;
  guint ref;
  guint bframes;
  gboolean b_adapt;
  gboolean b_pyramid;
  gboolean weightb;
  guint sps_id;
  gboolean au_nalu;
  gboolean trellis;
  guint vbv_buf_capacity;
  guint keyint_max;
  gboolean cabac;
  gfloat ip_factor;
  gfloat pb_factor;
  guint qp_min;
  guint qp_max;
  guint qp_step;
  gboolean mb_tree;
  gint rc_lookahead;
  guint noise_reduction;
  gboolean interlaced;
  gint speed_preset;
  gint psy_tune;
  guint tune;
  GString *tunings;
  GString *option_string_prop; /* option-string property */
  GString *option_string; /* used by set prop */
  gint frame_packing;
  gboolean insert_vui;

  /* input description */
  GstVideoCodecState *input_state;

  /* configuration changed  while playing */
  gboolean reconfig;

  /* from the downstream caps */
  const gchar *peer_profile;
  gboolean peer_intra_profile;
  gint peer_level_idc;

  /* cached values to set x264_picture_t */
  gint x264_nplanes;

  GstEncoderBitrateProfileManager *bitrate_manager;
};

struct _GstX264EncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_x264_enc_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (x264enc);

G_END_DECLS

#endif /* __GST_X264_ENC_H__ */
