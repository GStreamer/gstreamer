/* 
 * GStreamer
 * Copyright (C) 2007,2009 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <cog/cogframe.h>
#ifdef HAVE_ORC
#include <orc/orc.h>
#endif
#include <math.h>

#include "gstcogutils.h"

#define GST_CAT_DEFAULT gst_mse_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_MSE            (gst_mse_get_type())
#define GST_MSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSE,GstMSE))
#define GST_IS_MSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSE))
#define GST_MSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_MSE,GstMSEClass))
#define GST_IS_MSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_MSE))
#define GST_MSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_MSE,GstMSEClass))
typedef struct _GstMSE GstMSE;
typedef struct _GstMSEClass GstMSEClass;

typedef void (*GstMSEProcessFunc) (GstMSE *, guint8 *, guint);

struct _GstMSE
{
  GstElement element;

  /* < private > */
  GstPad *srcpad;
  GstPad *sinkpad_ref;
  GstPad *sinkpad_test;

  GstBuffer *buffer_ref;

  GMutex *lock;
  GCond *cond;
  gboolean cancel;

  GstVideoFormat format;
  int width;
  int height;

  double luma_mse_sum;
  double chroma_mse_sum;
  int n_frames;
};

struct _GstMSEClass
{
  GstElementClass parent;
};

GType gst_mse_get_type (void);


enum
{
  PROP_0,
  LUMA_PSNR,
  CHROMA_PSNR
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_mse_debug, "mse", 0, "cogmse element");

GST_BOILERPLATE_FULL (GstMSE, gst_mse, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_mse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_mse_chain_test (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_mse_chain_ref (GstPad * pad, GstBuffer * buffer);
static gboolean gst_mse_sink_event (GstPad * pad, GstEvent * event);
static void gst_mse_reset (GstMSE * filter);
static GstCaps *gst_mse_getcaps (GstPad * pad);
static gboolean gst_mse_set_caps (GstPad * pad, GstCaps * outcaps);
static void gst_mse_finalize (GObject * object);

static void cog_frame_mse (CogFrame * a, CogFrame * b, double *mse);
static double mse_to_db (double mse, gboolean is_chroma);


static GstStaticPadTemplate gst_framestore_sink_ref_template =
GST_STATIC_PAD_TEMPLATE ("sink_ref",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,YUY2,AYUV}"))
    );

static GstStaticPadTemplate gst_framestore_sink_test_template =
GST_STATIC_PAD_TEMPLATE ("sink_test",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,YUY2,AYUV}"))
    );

static GstStaticPadTemplate gst_framestore_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,YUY2,AYUV}"))
    );

static void
gst_mse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_framestore_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_framestore_sink_ref_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_framestore_sink_test_template);

  gst_element_class_set_details_simple (element_class, "Calculate MSE",
      "Filter/Effect",
      "Calculates mean squared error between two video streams",
      "David Schleef <ds@schleef.org>");
}

static void
gst_mse_class_init (GstMSEClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_mse_set_property;
  gobject_class->get_property = gst_mse_get_property;

  gobject_class->finalize = gst_mse_finalize;

  g_object_class_install_property (gobject_class, LUMA_PSNR,
      g_param_spec_double ("luma-psnr", "luma-psnr", "luma-psnr",
          0, 70, 40, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, CHROMA_PSNR,
      g_param_spec_double ("chroma-psnr", "chroma-psnr", "chroma-psnr",
          0, 70, 40, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

}

static void
gst_mse_init (GstMSE * filter, GstMSEClass * klass)
{
  gst_element_create_all_pads (GST_ELEMENT (filter));

  filter->srcpad = gst_element_get_static_pad (GST_ELEMENT (filter), "src");

  gst_pad_set_getcaps_function (filter->srcpad, gst_mse_getcaps);

  filter->sinkpad_ref =
      gst_element_get_static_pad (GST_ELEMENT (filter), "sink_ref");

  gst_pad_set_chain_function (filter->sinkpad_ref, gst_mse_chain_ref);
  gst_pad_set_event_function (filter->sinkpad_ref, gst_mse_sink_event);
  gst_pad_set_getcaps_function (filter->sinkpad_ref, gst_mse_getcaps);

  filter->sinkpad_test =
      gst_element_get_static_pad (GST_ELEMENT (filter), "sink_test");

  gst_pad_set_chain_function (filter->sinkpad_test, gst_mse_chain_test);
  gst_pad_set_event_function (filter->sinkpad_test, gst_mse_sink_event);
  gst_pad_set_getcaps_function (filter->sinkpad_test, gst_mse_getcaps);
  gst_pad_set_setcaps_function (filter->sinkpad_test, gst_mse_set_caps);

  gst_mse_reset (filter);

  filter->cond = g_cond_new ();
  filter->lock = g_mutex_new ();
}

static void
gst_mse_finalize (GObject * object)
{
  GstMSE *fs = GST_MSE (object);

  gst_object_unref (fs->srcpad);
  gst_object_unref (fs->sinkpad_ref);
  gst_object_unref (fs->sinkpad_test);
  g_mutex_free (fs->lock);
  g_cond_free (fs->cond);
  gst_buffer_replace (&fs->buffer_ref, NULL);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static GstCaps *
gst_mse_getcaps (GstPad * pad)
{
  GstMSE *fs;
  GstCaps *caps;
  GstCaps *icaps;
  GstCaps *peercaps;

  fs = GST_MSE (gst_pad_get_parent (pad));

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (pad != fs->srcpad) {
    peercaps = gst_pad_peer_get_caps (fs->srcpad);
    if (peercaps) {
      icaps = gst_caps_intersect (caps, peercaps);
      gst_caps_unref (caps);
      gst_caps_unref (peercaps);
      caps = icaps;
    }
  }

  if (pad != fs->sinkpad_ref) {
    peercaps = gst_pad_peer_get_caps (fs->sinkpad_ref);
    if (peercaps) {
      icaps = gst_caps_intersect (caps, peercaps);
      gst_caps_unref (caps);
      gst_caps_unref (peercaps);
      caps = icaps;
    }
  }

  if (pad != fs->sinkpad_test) {
    peercaps = gst_pad_peer_get_caps (fs->sinkpad_test);
    if (peercaps) {
      icaps = gst_caps_intersect (caps, peercaps);
      gst_caps_unref (caps);
      gst_caps_unref (peercaps);
      caps = icaps;
    }
  }

  gst_object_unref (fs);

  return caps;
}

static gboolean
gst_mse_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMSE *fs;

  fs = GST_MSE (gst_pad_get_parent (pad));

  gst_video_format_parse_caps (caps, &fs->format, &fs->width, &fs->height);

  gst_object_unref (fs);

  return TRUE;
}

static void
gst_mse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMSE *fs = GST_MSE (object);

  switch (prop_id) {
    case LUMA_PSNR:
      g_value_set_double (value,
          mse_to_db (fs->luma_mse_sum / fs->n_frames, FALSE));
      break;
    case CHROMA_PSNR:
      g_value_set_double (value,
          mse_to_db (fs->chroma_mse_sum / fs->n_frames, TRUE));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mse_reset (GstMSE * fs)
{
  fs->luma_mse_sum = 0;
  fs->chroma_mse_sum = 0;
  fs->n_frames = 0;
  fs->cancel = FALSE;

  if (fs->buffer_ref) {
    gst_buffer_unref (fs->buffer_ref);
    fs->buffer_ref = NULL;
  }
}


static GstFlowReturn
gst_mse_chain_ref (GstPad * pad, GstBuffer * buffer)
{
  GstMSE *fs;

  fs = GST_MSE (gst_pad_get_parent (pad));

  GST_DEBUG ("chain ref");

  g_mutex_lock (fs->lock);
  while (fs->buffer_ref) {
    GST_DEBUG ("waiting for ref buffer clear");
    g_cond_wait (fs->cond, fs->lock);
    if (fs->cancel) {
      g_mutex_unlock (fs->lock);
      gst_object_unref (fs);
      return GST_FLOW_WRONG_STATE;
    }
  }

  fs->buffer_ref = buffer;
  g_cond_signal (fs->cond);

  g_mutex_unlock (fs->lock);

  gst_object_unref (fs);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mse_chain_test (GstPad * pad, GstBuffer * buffer)
{
  GstMSE *fs;
  GstFlowReturn ret;
  GstBuffer *buffer_ref;

  fs = GST_MSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (fs, "chain test");

  g_mutex_lock (fs->lock);
  while (fs->buffer_ref == NULL) {
    GST_DEBUG_OBJECT (fs, "waiting for ref buffer");
    g_cond_wait (fs->cond, fs->lock);
    if (fs->cancel) {
      g_mutex_unlock (fs->lock);
      gst_object_unref (fs);
      return GST_FLOW_WRONG_STATE;
    }
  }

  buffer_ref = fs->buffer_ref;
  fs->buffer_ref = NULL;
  g_cond_signal (fs->cond);

  g_mutex_unlock (fs->lock);

  if (1) {
    CogFrame *frame_ref;
    CogFrame *frame_test;
    double mse[3];

    frame_ref = gst_cog_buffer_wrap (gst_buffer_ref (buffer_ref), fs->format,
        fs->width, fs->height);
    frame_test = gst_cog_buffer_wrap (gst_buffer_ref (buffer), fs->format,
        fs->width, fs->height);

    cog_frame_mse (frame_ref, frame_test, mse);

    GST_INFO ("mse %g %g %g", mse_to_db (mse[0], FALSE),
        mse_to_db (mse[1], TRUE), mse_to_db (mse[2], TRUE));

    fs->luma_mse_sum += mse[0];
    fs->chroma_mse_sum += 0.5 * (mse[1] + mse[2]);
    fs->n_frames++;

    cog_frame_unref (frame_ref);
    cog_frame_unref (frame_test);
  }


  ret = gst_pad_push (fs->srcpad, buffer);
  gst_buffer_unref (buffer_ref);

  gst_object_unref (fs);

  return ret;
}

static gboolean
gst_mse_sink_event (GstPad * pad, GstEvent * event)
{
  GstMSE *fs;

  fs = GST_MSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate;
      double applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      GST_DEBUG ("new_segment %d %g %g %d %" G_GINT64_FORMAT
          " %" G_GINT64_FORMAT " %" G_GINT64_FORMAT,
          update, rate, applied_rate, format, start, stop, position);

    }
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG ("flush start");
      fs->cancel = TRUE;
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG ("flush stop");
      fs->cancel = FALSE;
      break;
    default:
      break;
  }

  gst_pad_push_event (fs->srcpad, event);
  gst_object_unref (fs);

  return TRUE;
}

static int
sum_square_diff_u8 (uint8_t * s1, uint8_t * s2, int n)
{
#ifndef HAVE_ORC
  int sum = 0;
  int i;
  int x;

  for (i = 0; i < n; i++) {
    x = s1[i] - s2[i];
    sum += x * x;
  }
  return sum;
#else
  static OrcProgram *p = NULL;
  OrcExecutor *ex;
  int val;

  if (p == NULL) {
    OrcCompileResult ret;

    p = orc_program_new_ass (4, 1, 1);
    orc_program_add_temporary (p, 2, "t1");
    orc_program_add_temporary (p, 2, "t2");
    orc_program_add_temporary (p, 4, "t3");

    orc_program_append_ds_str (p, "convubw", "t1", "s1");
    orc_program_append_ds_str (p, "convubw", "t2", "s2");
    orc_program_append_str (p, "subw", "t1", "t1", "t2");
    orc_program_append_str (p, "mullw", "t1", "t1", "t1");
    orc_program_append_ds_str (p, "convuwl", "t3", "t1");
    orc_program_append_ds_str (p, "accl", "a1", "t3");

    ret = orc_program_compile (p);
    if (!ORC_COMPILE_RESULT_IS_SUCCESSFUL (ret)) {
      GST_ERROR ("Orc compiler failure");
      return 0;
    }
  }

  ex = orc_executor_new (p);
  orc_executor_set_n (ex, n);
  orc_executor_set_array_str (ex, "s1", s1);
  orc_executor_set_array_str (ex, "s2", s2);

  orc_executor_run (ex);
  val = orc_executor_get_accumulator (ex, 0);
  orc_executor_free (ex);

  return val;
#endif
}

static double
cog_frame_component_squared_error (CogFrameData * a, CogFrameData * b)
{
  int j;
  double sum;

  g_return_val_if_fail (a->width == b->width, 0.0);
  g_return_val_if_fail (a->height == b->height, 0.0);

  sum = 0;
  for (j = 0; j < a->height; j++) {
    sum += sum_square_diff_u8 (COG_FRAME_DATA_GET_LINE (a, j),
        COG_FRAME_DATA_GET_LINE (b, j), a->width);
  }
  return sum;
}

static void
cog_frame_mse (CogFrame * a, CogFrame * b, double *mse)
{
  double sum, n;

  sum = cog_frame_component_squared_error (&a->components[0],
      &b->components[0]);
  n = a->components[0].width * a->components[0].height;
  mse[0] = sum / n;

  sum = cog_frame_component_squared_error (&a->components[1],
      &b->components[1]);
  n = a->components[1].width * a->components[1].height;
  mse[1] = sum / n;

  sum = cog_frame_component_squared_error (&a->components[2],
      &b->components[2]);
  n = a->components[2].width * a->components[2].height;
  mse[2] = sum / n;
}

static double
mse_to_db (double mse, gboolean is_chroma)
{
  if (is_chroma) {
    return 10.0 * log (mse / (224.0 * 224.0)) / log (10.0);
  } else {
    return 10.0 * log (mse / (219.0 * 219.0)) / log (10.0);
  }
}
