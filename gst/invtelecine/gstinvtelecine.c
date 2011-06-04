/* GStreamer
 * Copyright (C) 2010 David A. Schleef <ds@schleef.org>
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
#include <stdlib.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY (gst_invtelecine_debug);
#define GST_CAT_DEFAULT gst_invtelecine_debug

#define GST_TYPE_INVTELECINE \
  (gst_invtelecine_get_type())
#define GST_INVTELECINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_INVTELECINE,GstInvtelecine))
#define GST_INVTELECINE_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_INVTELECINE,GstInvtelecineClass))
#define GST_IS_GST_INVTELECINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_INVTELECINE))
#define GST_IS_GST_INVTELECINE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_INVTELECINE))

typedef struct _GstInvtelecine GstInvtelecine;
typedef struct _GstInvtelecineClass GstInvtelecineClass;
typedef struct _Field Field;

#define FIFO_SIZE 20

struct _Field
{
  GstBuffer *buffer;
  int field_index;
  double prev;
  double prev1;
  double prev2;
  double prev3;

};

struct _GstInvtelecine
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  /* properties */
  gboolean verify_field_flags;

  /* state */
  int next_field;
  int num_fields;
  int field;

  gboolean locked;
  int last_lock;
  int phase;

  Field fifo[FIFO_SIZE];

  int width;
  int height;
  GstVideoFormat format;
  gboolean interlaced;

  double bad_flag_metric;
};

struct _GstInvtelecineClass
{
  GstElementClass element_class;

};

enum
{
  ARG_0,
  PROP_VERIFY_FIELD_FLAGS
};

static GstStaticPadTemplate gst_invtelecine_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{YUY2,UYVY,I420,YV12}")
    )
    );

static GstStaticPadTemplate gst_invtelecine_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{YUY2,UYVY,I420,YV12}")
    )
    );

static void gst_invtelecine_base_init (gpointer g_class);
static void gst_invtelecine_class_init (GstInvtelecineClass * klass);
static void gst_invtelecine_init (GstInvtelecine * invtelecine);
static GstFlowReturn gst_invtelecine_chain (GstPad * pad, GstBuffer * buffer);

static void gst_invtelecine_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_invtelecine_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_invtelecine_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_invtelecine_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GstFlowReturn
gst_invtelecine_output_fields (GstInvtelecine * invtelecine, int num_fields);


static GType
gst_invtelecine_get_type (void)
{
  static GType invtelecine_type = 0;

  if (!invtelecine_type) {
    static const GTypeInfo invtelecine_info = {
      sizeof (GstInvtelecineClass),
      gst_invtelecine_base_init,
      NULL,
      (GClassInitFunc) gst_invtelecine_class_init,
      NULL,
      NULL,
      sizeof (GstInvtelecine),
      0,
      (GInstanceInitFunc) gst_invtelecine_init,
    };

    invtelecine_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstInvtelecine", &invtelecine_info, 0);
  }

  return invtelecine_type;
}

static void
gst_invtelecine_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Inverse Telecine filter", "Filter/Video",
      "Detects and reconstructs progressive content from telecine video",
      "Entropy Wave <ds@entropywave.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_invtelecine_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_invtelecine_src_template));
}

static void
gst_invtelecine_class_init (GstInvtelecineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_invtelecine_set_property;
  object_class->get_property = gst_invtelecine_get_property;

  element_class->change_state = gst_invtelecine_change_state;

  g_object_class_install_property (object_class, PROP_VERIFY_FIELD_FLAGS,
      g_param_spec_boolean ("verify-field-flags", "verify field flags",
          "Verify that field dominance (top/bottom field first) buffer "
          "flags are correct", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_invtelecine_init (GstInvtelecine * invtelecine)
{
  GST_DEBUG ("gst_invtelecine_init");
  invtelecine->sinkpad =
      gst_pad_new_from_static_template (&gst_invtelecine_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (invtelecine), invtelecine->sinkpad);
  gst_pad_set_chain_function (invtelecine->sinkpad, gst_invtelecine_chain);
  gst_pad_set_setcaps_function (invtelecine->sinkpad, gst_invtelecine_setcaps);

  invtelecine->srcpad =
      gst_pad_new_from_static_template (&gst_invtelecine_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (invtelecine), invtelecine->srcpad);

  invtelecine->bad_flag_metric = 1.0;
  invtelecine->verify_field_flags = FALSE;
}

static gboolean
gst_invtelecine_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInvtelecine *invtelecine;
  gboolean ret;
  int width, height;
  GstVideoFormat format;
  gboolean interlaced = TRUE;
  int fps_n, fps_d;

  invtelecine = GST_INVTELECINE (gst_pad_get_parent (pad));

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  gst_video_format_parse_caps_interlaced (caps, &interlaced);
  ret &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);

  if (ret) {
    GstCaps *srccaps = gst_caps_copy (caps);

    ret = gst_pad_set_caps (invtelecine->srcpad, srccaps);

  }

  if (ret) {
    invtelecine->format = format;
    invtelecine->width = width;
    invtelecine->height = height;
    invtelecine->interlaced = interlaced;
  }

  g_object_unref (invtelecine);

  return ret;
}



#define MAX_FIELD_SCORE 100

static double
gst_invtelecine_compare_fields (GstInvtelecine * invtelecine, int field1,
    int field2)
{
  int i;
  int j;
  guint8 *data1;
  guint8 *data2_1;
  guint8 *data2_2;
  int field_index;
  int have;
  int vave;
  int hdiff;
  int vdiff;
  double sum;
  double linesum;
  double den;

  if (field1 < 0 || field2 < 0)
    return MAX_FIELD_SCORE;
  if (invtelecine->fifo[field1].buffer == NULL ||
      invtelecine->fifo[field2].buffer == NULL)
    return MAX_FIELD_SCORE;
  if (invtelecine->fifo[field1].buffer == invtelecine->fifo[field2].buffer &&
      invtelecine->fifo[field1].field_index ==
      invtelecine->fifo[field2].field_index) {
    return 0;
  }

  sum = 0;
  field_index = invtelecine->fifo[field1].field_index;
  for (j = field_index; j < invtelecine->height; j += 2) {
    if (j == 0 || j == invtelecine->height - 1)
      continue;

    if (invtelecine->format == GST_VIDEO_FORMAT_I420 ||
        invtelecine->format == GST_VIDEO_FORMAT_YV12) {
      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * j;
      data2_1 =
          GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * (j - 1);
      data2_2 =
          GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * (j + 1);

      /* planar 4:2:0 */
      linesum = 0;
      for (i = 1; i < invtelecine->width - 1; i++) {
        have = data1[i - 1] + data1[i + 1];
        hdiff = abs (data1[i - 1] - data1[i + 1]);
        vave = data2_1[i] + data2_2[i];
        vdiff = abs (data2_1[i] - data2_2[i]);
        den = MAX (1, MAX (hdiff, vdiff));
        linesum += (have - vave) * (have - vave) / (den * den);
      }
    } else {
      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * 2 * j;
      data2_1 =
          GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * 2 * (j - 1);
      data2_2 =
          GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * 2 * (j + 1);
      if (invtelecine->format == GST_VIDEO_FORMAT_UYVY) {
        data1++;
        data2_1++;
        data2_2++;
      }

      /* packed 4:2:2 */
      linesum = 0;
      for (i = 1; i < invtelecine->width - 1; i++) {
        have = data1[(i - 1) * 2] + data1[(i + 1) * 2];
        hdiff = abs (data1[(i - 1) * 2] - data1[(i + 1) * 2]);
        vave = data2_1[i * 2] + data2_2[i * 2];
        vdiff = abs (data2_1[i * 2] - data2_2[i * 2]);
        den = MAX (1, MAX (hdiff, vdiff));
        linesum += (have - vave) * (have - vave) / (den * den);
      }
    }
    sum += linesum;
  }

  sum /= (invtelecine->width * invtelecine->height / 2);

  return MIN (sum, MAX_FIELD_SCORE);
}

static double
gst_invtelecine_compare_fields_mse (GstInvtelecine * invtelecine, int field1,
    int field2)
{
  int i;
  int j;
  guint8 *data1;
  guint8 *data2;
  int field_index1;
  int field_index2;
  int diff;
  double sum;
  double linesum;

  if (field1 < 0 || field2 < 0)
    return MAX_FIELD_SCORE;
  if (invtelecine->fifo[field1].buffer == NULL ||
      invtelecine->fifo[field2].buffer == NULL)
    return MAX_FIELD_SCORE;
  if (invtelecine->fifo[field1].buffer == invtelecine->fifo[field2].buffer &&
      invtelecine->fifo[field1].field_index ==
      invtelecine->fifo[field2].field_index) {
    return 0;
  }

  sum = 0;
  field_index1 = invtelecine->fifo[field1].field_index;
  field_index2 = invtelecine->fifo[field2].field_index;
  if (invtelecine->format == GST_VIDEO_FORMAT_I420 ||
      invtelecine->format == GST_VIDEO_FORMAT_YV12) {
    for (j = 0; j < invtelecine->height; j += 2) {
      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * (j + field_index1);
      data2 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * (j + field_index2);

      linesum = 0;
      for (i = 0; i < invtelecine->width; i++) {
        diff = (data1[i] - data2[i]);
        linesum += diff * diff;
      }
      sum += linesum;
    }
  } else {
    for (j = 0; j < invtelecine->height; j += 2) {
      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * 2 * (j + field_index1);
      data2 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * 2 * (j + field_index2);

      if (invtelecine->format == GST_VIDEO_FORMAT_UYVY) {
        data1++;
        data2++;
      }

      linesum = 0;
      for (i = 0; i < invtelecine->width; i++) {
        diff = (data1[i * 2] - data2[i * 2]);
        linesum += diff * diff;
      }
      sum += linesum;
    }
  }

  sum /= invtelecine->width * invtelecine->height / 2;

  //return MIN (sum, MAX_FIELD_SCORE);
  return sum;
}

static double
gst_invtelecine_compare_fields_mse_ave (GstInvtelecine * invtelecine,
    int field1, int field2)
{
  int i;
  int j;
  guint8 *data1;
  guint8 *data2_1;
  guint8 *data2_2;
  int field_index1;
  int field_index2 G_GNUC_UNUSED;       /* FIXME: should it be used? */
  double diff;
  double sum;
  double linesum;

#define MAX_FIELD_SCORE_2 1e9
  if (field1 < 0 || field2 < 0)
    return MAX_FIELD_SCORE_2;
  if (invtelecine->fifo[field1].buffer == NULL ||
      invtelecine->fifo[field2].buffer == NULL)
    return MAX_FIELD_SCORE_2;
  if (invtelecine->fifo[field1].buffer == invtelecine->fifo[field2].buffer &&
      invtelecine->fifo[field1].field_index ==
      invtelecine->fifo[field2].field_index) {
    return 0;
  }

  sum = 0;
  field_index1 = invtelecine->fifo[field1].field_index;
  field_index2 = invtelecine->fifo[field2].field_index;
  if (invtelecine->format == GST_VIDEO_FORMAT_I420 ||
      invtelecine->format == GST_VIDEO_FORMAT_YV12) {
    for (j = 0; j < invtelecine->height; j += 2) {
      if (j + field_index1 == 0 || j + field_index1 == invtelecine->height - 1)
        continue;

      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * (j + field_index1);
      data2_1 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * (j + field_index1 - 1);
      data2_2 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * (j + field_index1 + 1);

      linesum = 0;
      for (i = 0; i < invtelecine->width; i++) {
        diff = (data1[i] - (data2_1[i] + data2_2[i]) / 2);
        diff *= diff;
        linesum += diff * diff;
      }
      sum += linesum;
    }
  } else {
    for (j = 0; j < invtelecine->height; j += 2) {
      if (j + field_index1 == 0 || j + field_index1 == invtelecine->height - 1)
        continue;

      data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) +
          invtelecine->width * 2 * (j + field_index1);
      data2_1 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * 2 * (j + field_index1 - 1);
      data2_2 = GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) +
          invtelecine->width * 2 * (j + field_index1 + 1);

      if (invtelecine->format == GST_VIDEO_FORMAT_UYVY) {
        data1++;
        data2_1++;
        data2_2++;
      }

      linesum = 0;
      for (i = 0; i < invtelecine->width; i++) {
        diff = (data1[i] - (data2_1[i] + data2_2[i]) / 2);
        diff *= diff;
        linesum += diff * diff;
      }
      sum += linesum;
    }
  }

  sum /= invtelecine->width * (invtelecine->height / 2 - 1);

  g_assert (sum > 0);

  //return MIN (sum, MAX_FIELD_SCORE);
  return sqrt (sum);
}

static void
gst_invtelecine_push_field (GstInvtelecine * invtelecine, GstBuffer * buffer,
    int field_index)
{
  int i;

  g_assert (invtelecine->num_fields < FIFO_SIZE - 1);
  g_assert (invtelecine->num_fields >= 0);

  i = invtelecine->num_fields;
  invtelecine->num_fields++;
  GST_DEBUG ("ref %p", buffer);
  invtelecine->fifo[i].buffer = gst_buffer_ref (buffer);
  invtelecine->fifo[i].field_index = field_index;
  invtelecine->fifo[i].prev =
      gst_invtelecine_compare_fields (invtelecine, i, i - 1);
  invtelecine->fifo[i].prev2 =
      gst_invtelecine_compare_fields_mse (invtelecine, i, i - 2);

  if (invtelecine->verify_field_flags) {
    invtelecine->fifo[i].prev3 =
        gst_invtelecine_compare_fields_mse_ave (invtelecine, i, i - 3);
    invtelecine->fifo[i].prev1 =
        gst_invtelecine_compare_fields_mse_ave (invtelecine, i, i - 1);

#define ALPHA 0.2
    if (invtelecine->fifo[i].prev3 != 0) {
      invtelecine->bad_flag_metric *= (1 - ALPHA);
      invtelecine->bad_flag_metric +=
          ALPHA * (invtelecine->fifo[i].prev1 / invtelecine->fifo[i].prev3);
    }
#if 0
    g_print ("42 %g %g %g\n", invtelecine->bad_flag_metric,
        invtelecine->fifo[i].prev1, invtelecine->fifo[i].prev3);
#endif

    if (invtelecine->bad_flag_metric > 1.2) {
      GST_WARNING ("bad field flags?  metric %g > 1.2",
          invtelecine->bad_flag_metric);
    }
  }

}

int pulldown_2_3[] = { 2, 3 };

typedef struct _PulldownFormat PulldownFormat;
struct _PulldownFormat
{
  const char *name;
  int cycle_length;
  int n_fields[10];
};

static const PulldownFormat formats[] = {
  /* interlaced */
  {"interlaced", 1, {1}},
  /* 30p */
  {"2:2", 2, {2}},
  /* 24p */
  {"3:2", 5, {2, 3,}},
};

static int
get_score_2 (GstInvtelecine * invtelecine, int format_index, int phase)
{
  const PulldownFormat *format = formats + format_index;
  int field_index;
  int k;
  int i;
  int score;

  GST_DEBUG ("score2 format_index %d phase %d", format_index, phase);

  phase = (invtelecine->field + phase) % format->cycle_length;

  field_index = 0;
  k = 0;
  while (phase > 0) {
    field_index++;
    if (field_index >= format->n_fields[k]) {
      field_index = 0;
      k++;
      if (format->n_fields[k] == 0) {
        k = 0;
      }
    }
    phase--;
  }

  /* k is the frame index in the format */
  /* field_index is the field index in the frame */

  score = 0;
  for (i = 0; i < 15; i++) {
    if (field_index == 0) {
      if (invtelecine->fifo[i].prev > 50) {
        /* Strong picture change signal */
        score++;
      }
    } else {
      if (invtelecine->fifo[i].prev > 50) {
        /* A secondary field with visible combing */
        score -= 5;
      } else if (field_index == 1) {
        if (invtelecine->fifo[i].prev > 5) {
          score--;
        } else if (invtelecine->fifo[i].prev < 3) {
          /* In the noise */
          score++;
        }
      } else {
        if (invtelecine->fifo[i].prev2 < 1) {
          score += 2;
        }
        if (invtelecine->fifo[i].prev2 > 10) {
          /* A tertiary field that doesn't match */
          score -= 5;
        }
      }
    }

    GST_DEBUG ("i=%d phase=%d fi=%d prev=%g score=%d", i, phase, field_index,
        invtelecine->fifo[i].prev, score);

    field_index++;
    if (field_index >= format->n_fields[k]) {
      field_index = 0;
      k++;
      if (format->n_fields[k] == 0) {
        k = 0;
      }
    }
  }

  return score;
}

int format_table[] = { 0, 1, 1, 2, 2, 2, 2, 2 };
int phase_table[] = { 0, 0, 1, 0, 1, 2, 3, 4 };

static void
gst_invtelecine_process (GstInvtelecine * invtelecine, gboolean flush)
{
  //int score;
  int num_fields;
  int scores[8];
  int i;
  int max_i;
  //int format;
  int phase;

  GST_DEBUG ("process %d", invtelecine->num_fields);
  while (invtelecine->num_fields > 15) {
    num_fields = 0;

    for (i = 0; i < 8; i++) {
      scores[i] = get_score_2 (invtelecine, format_table[i], phase_table[i]);
    }

#if 0
    g_print ("scores %d %d %d %d %d %d %d %d %d\n", invtelecine->field,
        scores[0], scores[1], scores[2], scores[3],
        scores[4], scores[5], scores[6], scores[7]);
#endif

    max_i = invtelecine->last_lock;
    for (i = 0; i < 8; i++) {
      int field_index;
      int k;

      phase = (invtelecine->field + phase_table[i]) %
          formats[format_table[i]].cycle_length;

      field_index = 0;
      k = 0;
      while (phase > 0) {
        field_index++;
        if (field_index >= formats[format_table[i]].n_fields[k]) {
          field_index = 0;
          k++;
          if (formats[format_table[i]].n_fields[k] == 0) {
            k = 0;
          }
        }
        phase--;
      }

      if (field_index == 0) {
        if (scores[i] > scores[max_i]) {
          max_i = i;
        }
      }
    }

    if (max_i != invtelecine->last_lock) {

      GST_WARNING ("new structure %s, phase %d",
          formats[format_table[max_i]].name, phase_table[max_i]);

      invtelecine->last_lock = max_i;
    }

    {
      int field_index;
      int k;

      phase = (invtelecine->field + phase_table[max_i]) %
          formats[format_table[max_i]].cycle_length;

      field_index = 0;
      k = 0;
      while (phase > 0) {
        field_index++;
        if (field_index >= formats[format_table[max_i]].n_fields[k]) {
          field_index = 0;
          k++;
          if (formats[format_table[max_i]].n_fields[k] == 0) {
            k = 0;
          }
        }
        phase--;
      }

      num_fields = formats[format_table[max_i]].n_fields[k];
    }

    if (num_fields == 0) {
      GST_WARNING ("unlocked");
      num_fields = 1;
    }

    gst_invtelecine_output_fields (invtelecine, num_fields);

    while (num_fields > 0) {
      GST_DEBUG ("unref %p", invtelecine->fifo[0].buffer);
      gst_buffer_unref (invtelecine->fifo[0].buffer);
      invtelecine->num_fields--;
      memmove (invtelecine->fifo, invtelecine->fifo + 1,
          invtelecine->num_fields * sizeof (Field));
      num_fields--;
      invtelecine->field++;
    }

    invtelecine->phase++;
    if (invtelecine->phase == 2) {
      invtelecine->phase = 0;
    }
  }

}

static void
copy_field (GstInvtelecine * invtelecine, GstBuffer * d, GstBuffer * s,
    int field_index)
{
  int j;
  guint8 *dest;
  guint8 *src;
  int width = invtelecine->width;
  int height = invtelecine->height;

  if (invtelecine->format == GST_VIDEO_FORMAT_I420 ||
      invtelecine->format == GST_VIDEO_FORMAT_YV12) {
    /* planar 4:2:0 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width;
      src = GST_BUFFER_DATA (s) + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height / 2; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + j * width / 2;
      src = GST_BUFFER_DATA (s) + width * height + j * width / 2;
      memcpy (dest, src, width / 2);
    }
    for (j = field_index; j < height / 2; j += 2) {
      dest =
          GST_BUFFER_DATA (d) + width * height + width / 2 * height / 2 +
          j * width / 2;
      src =
          GST_BUFFER_DATA (s) + width * height + width / 2 * height / 2 +
          j * width / 2;
      memcpy (dest, src, width / 2);
    }
  } else {
    /* packed 4:2:2 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width * 2;
      src = GST_BUFFER_DATA (s) + j * width * 2;
      memcpy (dest, src, width * 2);
    }
  }
}

static GstFlowReturn
gst_invtelecine_output_fields (GstInvtelecine * invtelecine, int num_fields)
{
  GstBuffer *buffer;
  int field_index;

  field_index = invtelecine->fifo[0].field_index;

  if (invtelecine->format == GST_VIDEO_FORMAT_I420 ||
      invtelecine->format == GST_VIDEO_FORMAT_YV12) {
    buffer =
        gst_buffer_new_and_alloc (invtelecine->width * invtelecine->height * 3 /
        2);
  } else {
    buffer =
        gst_buffer_new_and_alloc (invtelecine->width * invtelecine->height * 2);
  }

  copy_field (invtelecine, buffer, invtelecine->fifo[0].buffer, field_index);
  copy_field (invtelecine, buffer, invtelecine->fifo[1].buffer,
      field_index ^ 1);

  gst_buffer_set_caps (buffer, GST_BUFFER_CAPS (invtelecine->fifo[0].buffer));

  GST_BUFFER_TIMESTAMP (buffer) =
      GST_BUFFER_TIMESTAMP (invtelecine->fifo[0].buffer);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale (GST_SECOND, num_fields * 1001, 60000);
  if (num_fields == 3) {
    GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_RFF);
  }
  if (num_fields == 1) {
    GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_ONEFIELD);
  }
  if (field_index == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_TFF);
  }

  return gst_pad_push (invtelecine->srcpad, buffer);
}

static GstFlowReturn
gst_invtelecine_chain (GstPad * pad, GstBuffer * buffer)
{
  GstInvtelecine *invtelecine = GST_INVTELECINE (gst_pad_get_parent (pad));
  int field_index;

  GST_DEBUG ("Received buffer at %u:%02u:%02u:%09u",
      (guint) (GST_BUFFER_TIMESTAMP (buffer) / (GST_SECOND * 60 * 60)),
      (guint) ((GST_BUFFER_TIMESTAMP (buffer) / (GST_SECOND * 60)) % 60),
      (guint) ((GST_BUFFER_TIMESTAMP (buffer) / GST_SECOND) % 60),
      (guint) (GST_BUFFER_TIMESTAMP (buffer) % GST_SECOND));

  field_index = (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_TFF) ? 0 : 1;
//#define BAD
#ifdef BAD
  field_index ^= 1;
#endif

  GST_DEBUG ("duration %" GST_TIME_FORMAT " flags %04x %s %s %s",
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_BUFFER_FLAGS (buffer),
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_TFF) ? "tff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_RFF) ? "rff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_ONEFIELD) ? "onefield" :
      "");

  if (GST_BUFFER_FLAGS (buffer) & GST_BUFFER_FLAG_DISCONT) {
    GST_ERROR ("discont");

    invtelecine->next_field = field_index;
    invtelecine->bad_flag_metric = 1.0;
  }

  if (invtelecine->next_field != field_index) {
    GST_WARNING ("wrong field first, expecting %d got %d",
        invtelecine->next_field, field_index);
    invtelecine->next_field = field_index;
  }

  gst_invtelecine_push_field (invtelecine, buffer, invtelecine->next_field);
  invtelecine->next_field ^= 1;

  if (!(GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_ONEFIELD)) {
    gst_invtelecine_push_field (invtelecine, buffer, invtelecine->next_field);
    invtelecine->next_field ^= 1;

    if ((GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_RFF)) {
      gst_invtelecine_push_field (invtelecine, buffer, invtelecine->next_field);
      invtelecine->next_field ^= 1;
    }
  }

  gst_invtelecine_process (invtelecine, FALSE);

  gst_buffer_unref (buffer);

  gst_object_unref (invtelecine);

  return GST_FLOW_OK;
}

static void
gst_invtelecine_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstInvtelecine *invtelecine = GST_INVTELECINE (object);

  switch (prop_id) {
    case PROP_VERIFY_FIELD_FLAGS:
      invtelecine->verify_field_flags = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_invtelecine_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstInvtelecine *invtelecine = GST_INVTELECINE (object);

  switch (prop_id) {
    case PROP_VERIFY_FIELD_FLAGS:
      g_value_set_boolean (value, invtelecine->verify_field_flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_invtelecine_change_state (GstElement * element, GstStateChange transition)
{
  //GstInvtelecine *invtelecine = GST_INVTELECINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      //gst_invtelecine_reset (invtelecine);
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_invtelecine_debug, "invtelecine", 0,
      "Inverse telecine element");

  return gst_element_register (plugin, "invtelecine", GST_RANK_NONE,
      GST_TYPE_INVTELECINE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "invtelecine",
    "Inverse Telecine",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
