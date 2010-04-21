/*
 * GStreamer
 * Copyright (C) 2005 Martin Eikermann <meiker@upb.de>
 * Copyright (C) 2008-2010 Sebastian Dröge <slomo@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DEINTERLACE_H__
#define __GST_DEINTERLACE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>

#ifdef HAVE_GCC_ASM
#if defined(HAVE_CPU_I386) || defined(HAVE_CPU_X86_64)
#define BUILD_X86_ASM
#endif
#endif

G_BEGIN_DECLS

#define GST_TYPE_DEINTERLACE \
  (gst_deinterlace_get_type())
#define GST_DEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEINTERLACE,GstDeinterlace))
#define GST_DEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEINTERLACE,GstDeinterlace))
#define GST_IS_DEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEINTERLACE))
#define GST_IS_DEINTERLACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEINTERLACE))

typedef struct _GstDeinterlace GstDeinterlace;
typedef struct _GstDeinterlaceClass GstDeinterlaceClass;

#define GST_TYPE_DEINTERLACE_METHOD		(gst_deinterlace_method_get_type ())
#define GST_IS_DEINTERLACE_METHOD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD))
#define GST_IS_DEINTERLACE_METHOD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD))
#define GST_DEINTERLACE_METHOD_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD, GstDeinterlaceMethodClass))
#define GST_DEINTERLACE_METHOD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD, GstDeinterlaceMethod))
#define GST_DEINTERLACE_METHOD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD, GstDeinterlaceMethodClass))
#define GST_DEINTERLACE_METHOD_CAST(obj)	((GstDeinterlaceMethod*)(obj))

typedef struct _GstDeinterlaceMethod GstDeinterlaceMethod;
typedef struct _GstDeinterlaceMethodClass GstDeinterlaceMethodClass;


#define PICTURE_PROGRESSIVE 0
#define PICTURE_INTERLACED_BOTTOM 1
#define PICTURE_INTERLACED_TOP 2
#define PICTURE_INTERLACED_MASK (PICTURE_INTERLACED_BOTTOM | PICTURE_INTERLACED_TOP)

typedef struct
{
  /* pointer to the start of data for this field */
  GstBuffer *buf;
  /* see PICTURE_ flags in *.c */
  guint flags;
} GstDeinterlaceField;

/*
 * This structure defines the deinterlacer plugin.
 */


typedef void (*GstDeinterlaceMethodDeinterlaceFunction) (GstDeinterlaceMethod *self, const GstDeinterlaceField *history, guint history_count, GstBuffer *outbuf);

struct _GstDeinterlaceMethod {
  GstObject parent;

  GstVideoFormat format;
  gint frame_width, frame_height;
  gint width[4];
  gint height[4];
  gint offset[4];
  gint row_stride[4];
  gint pixel_stride[4];

  GstDeinterlaceMethodDeinterlaceFunction deinterlace_frame;
};

struct _GstDeinterlaceMethodClass {
  GstObjectClass parent_class;
  guint fields_required;
  guint latency;

  gboolean (*supported) (GstDeinterlaceMethodClass *klass, GstVideoFormat format, gint width, gint height);

  void (*setup) (GstDeinterlaceMethod *self, GstVideoFormat format, gint width, gint height);

  GstDeinterlaceMethodDeinterlaceFunction deinterlace_frame_yuy2;
  GstDeinterlaceMethodDeinterlaceFunction deinterlace_frame_yvyu;

  const gchar *name;
  const gchar *nick;
};

GType gst_deinterlace_method_get_type (void);

#define GST_TYPE_DEINTERLACE_SIMPLE_METHOD		(gst_deinterlace_simple_method_get_type ())
#define GST_IS_DEINTERLACE_SIMPLE_METHOD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_SIMPLE_METHOD))
#define GST_IS_DEINTERLACE_SIMPLE_METHOD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_SIMPLE_METHOD))
#define GST_DEINTERLACE_SIMPLE_METHOD_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_SIMPLE_METHOD, GstDeinterlaceSimpleMethodClass))
#define GST_DEINTERLACE_SIMPLE_METHOD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_SIMPLE_METHOD, GstDeinterlaceSimpleMethod))
#define GST_DEINTERLACE_SIMPLE_METHOD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_SIMPLE_METHOD, GstDeinterlaceSimpleMethodClass))
#define GST_DEINTERLACE_SIMPLE_METHOD_CAST(obj)	((GstDeinterlaceSimpleMethod*)(obj))

typedef struct _GstDeinterlaceSimpleMethod GstDeinterlaceSimpleMethod;
typedef struct _GstDeinterlaceSimpleMethodClass GstDeinterlaceSimpleMethodClass;
typedef struct _GstDeinterlaceScanlineData GstDeinterlaceScanlineData;

/*
 * This structure defines the simple deinterlacer plugin.
 */

struct _GstDeinterlaceScanlineData {
 const guint8 *tt0, *t0, *m0, *b0, *bb0;
 const guint8 *tt1, *t1, *m1, *b1, *bb1;
 const guint8 *tt2, *t2, *m2, *b2, *bb2;
 const guint8 *tt3, *t3, *m3, *b3, *bb3;
 gboolean bottom_field;
};

/**
 * For interpolate_scanline the input is:
 *
 * |   t-3       t-2       t-1       t
 * | Field 3 | Field 2 | Field 1 | Field 0 |
 * |  TT3    |         |   TT1   |         |
 * |         |   T2    |         |   T0    |
 * |   M3    |         |    M1   |         |
 * |         |   B2    |         |   B0    |
 * |  BB3    |         |   BB1   |         |
 *
 * For copy_scanline the input is:
 *
 * |   t-3       t-2       t-1       t
 * | Field 3 | Field 2 | Field 1 | Field 0 |
 * |         |   TT2   |         |  TT0    |
 * |   T3    |         |   T1    |         |
 * |         |    M2   |         |   M0    |
 * |   B3    |         |   B1    |         |
 * |         |   BB2   |         |  BB0    |
 *
 * All other values are NULL.
 */

typedef void (*GstDeinterlaceSimpleMethodPackedFunction) (GstDeinterlaceSimpleMethod *self, guint8 *out, const GstDeinterlaceScanlineData *scanlines);

struct _GstDeinterlaceSimpleMethod {
  GstDeinterlaceMethod parent;

  GstDeinterlaceSimpleMethodPackedFunction interpolate_scanline_packed;
  GstDeinterlaceSimpleMethodPackedFunction copy_scanline_packed;
};

struct _GstDeinterlaceSimpleMethodClass {
  GstDeinterlaceMethodClass parent_class;

  /* Packed formats */
  GstDeinterlaceSimpleMethodPackedFunction interpolate_scanline_yuy2;
  GstDeinterlaceSimpleMethodPackedFunction copy_scanline_yuy2;
  GstDeinterlaceSimpleMethodPackedFunction interpolate_scanline_yvyu;
  GstDeinterlaceSimpleMethodPackedFunction copy_scanline_yvyu;
};

GType gst_deinterlace_simple_method_get_type (void);

#define GST_DEINTERLACE_MAX_FIELD_HISTORY 10

typedef enum
{
  GST_DEINTERLACE_TOMSMOCOMP,
  GST_DEINTERLACE_GREEDY_H,
  GST_DEINTERLACE_GREEDY_L,
  GST_DEINTERLACE_VFIR,
  GST_DEINTERLACE_LINEAR,
  GST_DEINTERLACE_LINEAR_BLEND,
  GST_DEINTERLACE_SCALER_BOB,
  GST_DEINTERLACE_WEAVE,
  GST_DEINTERLACE_WEAVE_TFF,
  GST_DEINTERLACE_WEAVE_BFF
} GstDeinterlaceMethods;

typedef enum
{
  GST_DEINTERLACE_ALL,         /* All (missing data is interp.) */
  GST_DEINTERLACE_TF,          /* Top Fields Only */
  GST_DEINTERLACE_BF           /* Bottom Fields Only */
} GstDeinterlaceFields;

typedef enum
{
  GST_DEINTERLACE_LAYOUT_AUTO,
  GST_DEINTERLACE_LAYOUT_TFF,
  GST_DEINTERLACE_LAYOUT_BFF
} GstDeinterlaceFieldLayout;

typedef enum {
  GST_DEINTERLACE_MODE_AUTO,
  GST_DEINTERLACE_MODE_INTERLACED,
  GST_DEINTERLACE_MODE_DISABLED
} GstDeinterlaceMode;

struct _GstDeinterlace
{
  GstElement parent;

  GstPad *srcpad, *sinkpad;

  /* <private> */

  GstDeinterlaceMode mode;

  GstDeinterlaceFieldLayout field_layout;

  GstDeinterlaceFields fields;

  GstDeinterlaceMethods method_id;
  GstDeinterlaceMethod *method;

  GstVideoFormat format;
  gint width, height; /* frame width & height */
  guint frame_size; /* frame size in bytes */
  gint fps_n, fps_d; /* frame rate */
  gboolean interlaced; /* is input interlaced? */

  GstClockTime field_duration; /* Duration of one field */

  /* The most recent pictures 
     PictureHistory[0] is always the most recent.
     Pointers are NULL if the picture in question isn't valid, e.g. because
     the program just started or a picture was skipped.
   */
  GstDeinterlaceField field_history[GST_DEINTERLACE_MAX_FIELD_HISTORY];
  guint history_count;

  /* Set to TRUE if we're in still frame mode,
     i.e. just forward all buffers
   */
  gboolean still_frame_mode;

  /* Last buffer that was pushed in */
  GstBuffer *last_buffer;

  /* Current segment */
  GstSegment segment;

  /* QoS stuff */
  gdouble proportion;
  GstClockTime earliest_time;

  /* Upstream negotiation stuff */
  GstCaps *sink_caps;
  GstCaps *src_caps;

  GstCaps *request_caps;
};

struct _GstDeinterlaceClass
{
  GstElementClass parent_class;
};

GType gst_deinterlace_get_type (void);

G_END_DECLS

#endif /* __GST_DEINTERLACE_H__ */
