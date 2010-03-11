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
};

struct _GstInvtelecine
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  int next_field;
  int num_fields;
  int field;

  gboolean locked;
  int phase;

  Field fifo[FIFO_SIZE];
};

struct _GstInvtelecineClass
{
  GstElementClass element_class;

};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_invtelecine_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")
    )
    );

static GstStaticPadTemplate gst_invtelecine_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")
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

static GstStateChangeReturn gst_invtelecine_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GstFlowReturn
gst_invtelecine_output_fields (GstInvtelecine * invtelecine, int num_fields);


GType
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
  static const GstElementDetails invtelecine_details =
      GST_ELEMENT_DETAILS ("H.264 Decoder",
      "Codec/Decoder/Video",
      "Decode H.264/MPEG-4 AVC video streams",
      "Entropy Wave <ds@entropywave.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &invtelecine_details);

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
}

static void
gst_invtelecine_init (GstInvtelecine * invtelecine)
{
  GST_DEBUG ("gst_invtelecine_init");
  invtelecine->sinkpad =
      gst_pad_new_from_static_template (&gst_invtelecine_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (invtelecine), invtelecine->sinkpad);
  gst_pad_set_chain_function (invtelecine->sinkpad, gst_invtelecine_chain);

  invtelecine->srcpad =
      gst_pad_new_from_static_template (&gst_invtelecine_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (invtelecine), invtelecine->srcpad);

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
  for (j = field_index; j < 480; j += 2) {
    if (j == 0 || j == 479)
      continue;

    data1 = GST_BUFFER_DATA (invtelecine->fifo[field1].buffer) + 720 * j;
    data2_1 =
        GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) + 720 * (j - 1);
    data2_2 =
        GST_BUFFER_DATA (invtelecine->fifo[field2].buffer) + 720 * (j + 1);

    linesum = 0;
    for (i = 1; i < 719; i++) {
      have = data1[i - 1] + data1[i + 1];
      hdiff = abs (data1[i - 1] - data1[i + 1]);
      vave = data2_1[i] + data2_2[i];
      vdiff = abs (data2_1[i] - data2_2[i]);
      den = MAX (1, MAX (hdiff, vdiff));
      linesum += (have - vave) * (have - vave) / (den * den);
    }
    sum += linesum;
  }

  sum /= 720 * 240;

  return MIN (sum, MAX_FIELD_SCORE);
}

static void
gst_invtelecine_push_field (GstInvtelecine * invtelecine, GstBuffer * buffer,
    int field_index)
{
  int i;

  g_assert (invtelecine->num_fields < FIFO_SIZE - 1);

  i = invtelecine->num_fields;
  invtelecine->num_fields++;
  GST_DEBUG ("ref %p", buffer);
  invtelecine->fifo[i].buffer = gst_buffer_ref (buffer);
  invtelecine->fifo[i].field_index = field_index;
  invtelecine->fifo[i].prev =
      gst_invtelecine_compare_fields (invtelecine, i, i - 1);
  //g_print("compare %f\n", invtelecine->fifo[i].prev);

}

int pulldown_2_3[] = { 2, 3 };

static int
get_score (GstInvtelecine * invtelecine, int phase)
{
  int i;
  int score = 0;
  int field_index = 0;

  GST_DEBUG ("scoring for phase %d", phase);
  for (i = 0; i < 15; i++) {
    if (field_index == 0) {
      if (invtelecine->fifo[i].prev > 50) {
        /* Strong picture change signal */
        score++;
      } else if (i < 14 &&
          pulldown_2_3[phase] >= 2 &&
          (invtelecine->fifo[i].prev < invtelecine->fifo[i + 1].prev * 0.5)) {
        score--;
      } else if (i < 13 &&
          pulldown_2_3[phase] >= 3 &&
          (invtelecine->fifo[i].prev < invtelecine->fifo[i + 2].prev * 0.5)) {
        score--;
      } else {

      }
    } else {
      if (invtelecine->fifo[i].prev > 50) {
        /* A secondary field with visible combing */
        return -10;
      } else if (invtelecine->fifo[i].prev > 5) {
        score--;
      } else if (invtelecine->fifo[i].prev < 3) {
        /* In the noise */
        score++;
      } else {
      }
    }
    GST_DEBUG ("i=%d phase=%d fi=%d prev=%g score=%d", i, phase, field_index,
        invtelecine->fifo[i].prev, score);
    field_index++;
    if (field_index == pulldown_2_3[phase]) {
      field_index = 0;
      phase++;
      if (phase == 2)
        phase = 0;
    }
  }

  return score;
}

static void
gst_invtelecine_process (GstInvtelecine * invtelecine, gboolean flush)
{
  int score;
  int num_fields;

  GST_DEBUG ("process %d", invtelecine->num_fields);
  while (invtelecine->num_fields > 15) {
    if (invtelecine->locked) {
      score = get_score (invtelecine, invtelecine->phase);
      if (score < 4) {
        GST_WARNING ("unlocked field=%d (phase = %d, score = %d)",
            invtelecine->field, invtelecine->phase, score);
        invtelecine->locked = FALSE;
      }
    }
    if (!invtelecine->locked) {
      int p;
      int a[2];

      for (p = 0; p < 2; p++) {
        a[p] = get_score (invtelecine, p);
      }
      if (a[0] >= 8 && a[1] < 4) {
        GST_WARNING ("locked field=%d (phase = %d, score = %d)",
            invtelecine->field, 0, a[0]);
        invtelecine->locked = TRUE;
        invtelecine->phase = 0;
      } else if (a[1] >= 8 && a[0] < 4) {
        GST_WARNING ("locked field=%d (phase = %d, score = %d)",
            invtelecine->field, 1, a[1]);
        invtelecine->locked = TRUE;
        invtelecine->phase = 1;
      }
    }
    //g_print ("score %d %d\n", a[0], a[1]);

    if (invtelecine->locked) {
      num_fields = pulldown_2_3[invtelecine->phase];

      g_print ("frame %d %g %g %g\n",
          invtelecine->field,
          invtelecine->fifo[0].prev,
          invtelecine->fifo[1].prev,
          (num_fields == 3) ? invtelecine->fifo[2].prev : 0);

    } else {
      num_fields = 2;
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
copy_field (GstBuffer * d, GstBuffer * s, int field_index)
{
  int j;
  guint8 *dest;
  guint8 *src;

  for (j = field_index; j < 480; j += 2) {
    dest = GST_BUFFER_DATA (d) + j * 720;
    src = GST_BUFFER_DATA (s) + j * 720;
    memcpy (dest, src, 720);
  }
  for (j = field_index; j < 240; j += 2) {
    dest = GST_BUFFER_DATA (d) + 720 * 480 + j * 360;
    src = GST_BUFFER_DATA (s) + 720 * 480 + j * 360;
    memcpy (dest, src, 360);
  }
  for (j = field_index; j < 240; j += 2) {
    dest = GST_BUFFER_DATA (d) + 720 * 480 + 360 * 240 + j * 360;
    src = GST_BUFFER_DATA (s) + 720 * 480 + 360 * 240 + j * 360;
    memcpy (dest, src, 360);
  }
}

static GstFlowReturn
gst_invtelecine_output_fields (GstInvtelecine * invtelecine, int num_fields)
{
  GstBuffer *buffer;
  int field_index;

  field_index = invtelecine->fifo[0].field_index;

  buffer = gst_buffer_new_and_alloc (720 * 480 + 360 * 240 + 360 * 240);

  copy_field (buffer, invtelecine->fifo[0].buffer, field_index);
  copy_field (buffer, invtelecine->fifo[1].buffer, field_index ^ 1);

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

  GST_DEBUG ("duration %lld flags %04x %s %s %s",
      GST_BUFFER_DURATION (buffer),
      GST_BUFFER_FLAGS (buffer),
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_TFF) ? "tff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_RFF) ? "rff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_ONEFIELD) ? "onefield" :
      "");

  if (GST_BUFFER_FLAGS (buffer) & GST_BUFFER_FLAG_DISCONT) {
    GST_DEBUG ("discont");

    invtelecine->next_field = field_index;
  }

  if (invtelecine->next_field != field_index) {
    GST_DEBUG ("wrong field first, expecting %d got %d",
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
  //GstInvtelecine *invtelecine = GST_INVTELECINE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_invtelecine_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  //GstInvtelecine *invtelecine = GST_INVTELECINE (object);

  switch (prop_id) {
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
