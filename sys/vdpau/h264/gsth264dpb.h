/* GStreamer
 *
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
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

#ifndef _GST_H264_DPB_H_
#define _GST_H264_DPB_H_

#include <glib-object.h>
#include <vdpau/vdpau.h>

#include <gst/video/video.h>
#include <gst/codecparsers/gsth264meta.h>

G_BEGIN_DECLS

#define MAX_DPB_SIZE 16

#define GST_TYPE_H264_DPB             (gst_h264_dpb_get_type ())
#define GST_H264_DPB(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_H264_DPB, GstH264DPB))
#define GST_H264_DPB_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_H264_DPB, GstH264DPBClass))
#define GST_IS_H264_DPB(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_H264_DPB))
#define GST_IS_H264_DPB_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_H264_DPB))
#define GST_H264_DPB_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_H264_DPB, GstH264DPBClass))

typedef struct _GstH264DPB GstH264DPB;
typedef struct _GstH264DPBClass GstH264DPBClass;

typedef struct _GstH264Frame
{
  GstVideoCodecFrame *frame;

  guint poc;
  guint16 frame_idx;
  gboolean is_reference;
  gboolean is_long_term;
  gboolean output_needed;
} GstH264Frame;


typedef GstFlowReturn (*GstH264DPBOutputFunc) (GstH264DPB *dpb, GstH264Frame *h264_frame, gpointer user_data);

struct _GstH264DPB
{
  GObject parent_instance;

  /* private */
  GstH264Frame *frames[MAX_DPB_SIZE];  
  guint n_frames;
  
  guint max_frames;
  gint max_longterm_frame_idx;

  GstH264DPBOutputFunc output;
  gpointer user_data;
};

struct _GstH264DPBClass
{
  GObjectClass parent_class;
};

void
gst_h264_dpb_fill_reference_frames (GstH264DPB *dpb, VdpReferenceFrameH264 reference_frames[16]);

gboolean gst_h264_dpb_add (GstH264DPB *dpb, GstH264Frame *h264_frame);
void gst_h264_dpb_flush (GstH264DPB *dpb, gboolean output);

void gst_h264_dpb_mark_sliding (GstH264DPB *dpb);

void gst_h264_dpb_mark_long_term_unused (GstH264DPB *dpb, guint16 long_term_pic_num);
void gst_h264_dpb_mark_short_term_unused (GstH264DPB *dpb, guint16 pic_num);
void gst_h264_dpb_mark_all_unused (GstH264DPB *dpb);
void gst_h264_dpb_mark_long_term (GstH264DPB *dpb, guint16 pic_num, guint16 long_term_frame_idx);

void gst_h264_dpb_set_output_func (GstH264DPB *dpb, GstH264DPBOutputFunc func,
    gpointer user_data);

GType gst_h264_dpb_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GST_H264_DPB_H_ */
