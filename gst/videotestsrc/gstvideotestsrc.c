/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2002> David A. Schleef <ds@schleef.org>
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

/**
 * SECTION:element-videotestsrc
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v videotestsrc pattern=snow ! ximagesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstvideotestsrc.h"
#include "videotestsrc.h"

#include <string.h>
#include <stdlib.h>
#include <liboil/liboil.h>

#define USE_PEER_BUFFERALLOC

GST_DEBUG_CATEGORY (videotestsrc_debug);
#define GST_CAT_DEFAULT videotestsrc_debug


static GstElementDetails videotestsrc_details =
GST_ELEMENT_DETAILS ("Video test source",
    "Source/Video",
    "Creates a test video stream",
    "David A. Schleef <ds@schleef.org>");


enum
{
  PROP_0,
  PROP_PATTERN,
  PROP_TIMESTAMP_OFFSET,
  PROP_IS_LIVE
      /* FILL ME */
};


GST_BOILERPLATE (GstVideoTestSrc, gst_videotestsrc, GstPushSrc,
    GST_TYPE_PUSH_SRC);


static void gst_videotestsrc_set_pattern (GstVideoTestSrc * videotestsrc,
    int pattern_type);
static void gst_videotestsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videotestsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_videotestsrc_getcaps (GstBaseSrc * bsrc);
static gboolean gst_videotestsrc_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_videotestsrc_negotiate (GstBaseSrc * bsrc);
static void gst_videotestsrc_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_videotestsrc_event (GstBaseSrc * bsrc, GstEvent * event);
static gboolean gst_videotestsrc_unlock (GstBaseSrc * bsrc);

static GstFlowReturn gst_videotestsrc_create (GstPushSrc * psrc,
    GstBuffer ** buffer);


#define GST_TYPE_VIDEOTESTSRC_PATTERN (gst_videotestsrc_pattern_get_type ())
static GType
gst_videotestsrc_pattern_get_type (void)
{
  static GType videotestsrc_pattern_type = 0;
  static GEnumValue pattern_types[] = {
    {GST_VIDEOTESTSRC_SMPTE, "smpte", "SMPTE 100% color bars"},
    {GST_VIDEOTESTSRC_SNOW, "snow", "Random (television snow)"},
    {GST_VIDEOTESTSRC_BLACK, "black", "100% Black"},
    {0, NULL, NULL},
  };

  if (!videotestsrc_pattern_type) {
    videotestsrc_pattern_type =
        g_enum_register_static ("GstVideoTestSrcPattern", pattern_types);
  }
  return videotestsrc_pattern_type;
}

static void
gst_videotestsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videotestsrc_details);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_videotestsrc_getcaps (NULL)));
}

static void
gst_videotestsrc_class_init (GstVideoTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_videotestsrc_set_property;
  gobject_class->get_property = gst_videotestsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PATTERN,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_VIDEOTESTSRC_PATTERN, 1,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE, G_PARAM_READWRITE));

  gstbasesrc_class->get_caps = gst_videotestsrc_getcaps;
  gstbasesrc_class->set_caps = gst_videotestsrc_setcaps;
  gstbasesrc_class->negotiate = gst_videotestsrc_negotiate;
  gstbasesrc_class->get_times = gst_videotestsrc_get_times;
  gstbasesrc_class->event = gst_videotestsrc_event;
  gstbasesrc_class->unlock = gst_videotestsrc_unlock;

  gstpushsrc_class->create = gst_videotestsrc_create;
}

static void
gst_videotestsrc_init (GstVideoTestSrc * src, GstVideoTestSrcClass * g_class)
{
  gst_videotestsrc_set_pattern (src, GST_VIDEOTESTSRC_SMPTE);

  src->segment_start_frame = -1;
  src->segment_end_frame = -1;
  src->timestamp_offset = 0;

  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);
}

static void
gst_videotestsrc_set_pattern (GstVideoTestSrc * videotestsrc, int pattern_type)
{
  videotestsrc->pattern_type = pattern_type;

  GST_DEBUG_OBJECT (videotestsrc, "setting pattern to %d", pattern_type);

  switch (pattern_type) {
    case GST_VIDEOTESTSRC_SMPTE:
      videotestsrc->make_image = gst_videotestsrc_smpte;
      break;
    case GST_VIDEOTESTSRC_SNOW:
      videotestsrc->make_image = gst_videotestsrc_snow;
      break;
    case GST_VIDEOTESTSRC_BLACK:
      videotestsrc->make_image = gst_videotestsrc_black;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_videotestsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoTestSrc *src = GST_VIDEOTESTSRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      gst_videotestsrc_set_pattern (src, g_value_get_enum (value));
      break;
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    default:
      break;
  }
}

static void
gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoTestSrc *src = GST_VIDEOTESTSRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      g_value_set_enum (value, src->pattern_type);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* threadsafe because this gets called as the plugin is loaded */
static GstCaps *
gst_videotestsrc_getcaps (GstBaseSrc * unused)
{
  static GstCaps *capslist = NULL;

  if (!capslist) {
    GstCaps *caps;
    GstStructure *structure;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < n_fourccs; i++) {
      structure = paint_get_structure (fourcc_list + i);
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
      gst_caps_append_structure (caps, structure);
    }

    capslist = caps;
  }

  return gst_caps_copy (capslist);
}

static gboolean
gst_videotestsrc_parse_caps (const GstCaps * caps,
    gint * width, gint * height, gdouble * rate,
    struct fourcc_list_struct **fourcc)
{
  const GstStructure *structure;
  GstPadLinkReturn ret;

  GST_DEBUG ("parsing caps");

  if (gst_caps_get_size (caps) < 1)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  *fourcc = paintinfo_find_by_structure (structure);
  if (!*fourcc) {
    g_critical ("videotestsrc format not found");
    return FALSE;
  }

  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);
  ret &= gst_structure_get_double (structure, "framerate", rate);

  return ret;
}

static gboolean
gst_videotestsrc_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  gboolean res;
  gint width, height;
  gdouble rate;
  struct fourcc_list_struct *fourcc;
  GstVideoTestSrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (bsrc);

  res = gst_videotestsrc_parse_caps (caps, &width, &height, &rate, &fourcc);
  if (res) {
    /* looks ok here */
    videotestsrc->fourcc = fourcc;
    videotestsrc->width = width;
    videotestsrc->height = height;
    videotestsrc->rate = rate;
    videotestsrc->bpp = videotestsrc->fourcc->bitspp;

    GST_DEBUG_OBJECT (videotestsrc, "size %dx%d, %f fps", videotestsrc->width,
        videotestsrc->height, videotestsrc->rate);
  }
  return res;
}

static gboolean
gst_videotestsrc_negotiate (GstBaseSrc * bsrc)
{
  GstCaps *caps;
  GstCaps *temp;
  gboolean result = FALSE;

  /* get all possible caps on this link */
  caps = gst_pad_get_allowed_caps (GST_BASE_SRC_PAD (bsrc));
  temp = gst_caps_normalize (caps);
  gst_caps_unref (caps);
  caps = temp;

  if (gst_caps_get_size (caps) > 0) {
    GstStructure *structure;

    /* pick the first one */
    gst_caps_truncate (caps);

    structure = gst_caps_get_structure (caps, 0);

    gst_caps_structure_fixate_field_nearest_int (structure, "width", 320);
    gst_caps_structure_fixate_field_nearest_int (structure, "height", 240);
    gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
        30.0);

    result = gst_pad_set_caps (GST_BASE_SRC_PAD (bsrc), caps);
    gst_caps_unref (caps);
  }

  return result;
}

static void
gst_videotestsrc_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_videotestsrc_event (GstBaseSrc * bsrc, GstEvent * event)
{
  gboolean res = TRUE;
  GstVideoTestSrc *videotestsrc;
  gint64 new_n_frames;

  videotestsrc = GST_VIDEOTESTSRC (bsrc);
  new_n_frames = videotestsrc->n_frames;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
      GstSeekType cur_type, stop_type;
      GstSeekFlags flags;
      gint64 cur, stop;

      gst_event_parse_seek (event, NULL, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);

      switch (format) {
        case GST_FORMAT_TIME:
          new_n_frames = cur * (double) videotestsrc->rate / GST_SECOND;
          videotestsrc->segment_start_frame = new_n_frames;
          videotestsrc->segment_end_frame =
              stop * (double) videotestsrc->rate / GST_SECOND;
          videotestsrc->segment = flags & GST_SEEK_FLAG_SEGMENT;
          break;
        case GST_FORMAT_DEFAULT:
          new_n_frames = cur;
          videotestsrc->segment_start_frame = new_n_frames;
          videotestsrc->segment_end_frame = stop;
          videotestsrc->segment = flags & GST_SEEK_FLAG_SEGMENT;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  if (videotestsrc->n_frames != new_n_frames) {
    videotestsrc->n_frames = new_n_frames;
  }

  return res;
}

/* with STREAM_LOCK */
static GstClockReturn
gst_videotestsrc_wait (GstVideoTestSrc * src, GstClockTime time)
{
  GstClockReturn ret;
  GstClockTime base_time;

  GST_LOCK (src);
  /* clock_id should be NULL outside of this function */
  g_assert (src->clock_id == NULL);
  g_assert (GST_CLOCK_TIME_IS_VALID (time));
  base_time = GST_ELEMENT (src)->base_time;
  src->clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (src),
      time + base_time);
  GST_UNLOCK (src);

  ret = gst_clock_id_wait (src->clock_id, NULL);

  GST_LOCK (src);
  gst_clock_id_unref (src->clock_id);
  src->clock_id = NULL;
  GST_UNLOCK (src);

  return ret;
}

static gboolean
gst_videotestsrc_unlock (GstBaseSrc * bsrc)
{
  GstVideoTestSrc *src = GST_VIDEOTESTSRC (bsrc);

  GST_LOCK (src);
  if (src->clock_id)
    gst_clock_id_unschedule (src->clock_id);
  GST_UNLOCK (src);

  return TRUE;
}

static GstFlowReturn
gst_videotestsrc_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstVideoTestSrc *src;
  gulong newsize;
  GstBuffer *outbuf;
  GstFlowReturn res;

  src = GST_VIDEOTESTSRC (psrc);

  if (src->fourcc == NULL)
    goto not_negotiated;

  newsize = gst_videotestsrc_get_size (src, src->width, src->height);

  g_return_val_if_fail (newsize > 0, GST_FLOW_ERROR);

  GST_LOG_OBJECT (src, "creating buffer of %ld bytes for %dx%d image",
      newsize, src->width, src->height);

  if (gst_base_src_is_live (GST_BASE_SRC (src))) {
    GstClockReturn ret;

    ret =
        gst_videotestsrc_wait (src, src->running_time + src->timestamp_offset);
    if (ret == GST_CLOCK_UNSCHEDULED)
      goto unscheduled;
  }
#ifdef USE_PEER_BUFFERALLOC
  res =
      gst_pad_alloc_buffer (GST_BASE_SRC_PAD (psrc), GST_BUFFER_OFFSET_NONE,
      newsize, GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)), &outbuf);
  if (res != GST_FLOW_OK)
    goto no_buffer;
#else
  res = GST_FLOW_OK;

  outbuf = gst_buffer_new_and_alloc (newsize);
  if (outbuf == NULL)
    goto no_buffer;
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)));
#endif

  src->make_image (src, (void *) GST_BUFFER_DATA (outbuf),
      src->width, src->height);

  GST_BUFFER_TIMESTAMP (outbuf) = src->timestamp_offset + src->running_time;
  GST_BUFFER_DURATION (outbuf) = GST_SECOND / (double) src->rate;

  src->n_frames++;
  src->running_time += GST_BUFFER_DURATION (outbuf);

  *buffer = outbuf;

  return GST_FLOW_OK;

unscheduled:
  {
    GST_DEBUG_OBJECT (src, "Unscheduled while waiting for clock");
    return GST_FLOW_WRONG_STATE;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before get function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_buffer:
  {
    return res;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  oil_init ();

  GST_DEBUG_CATEGORY_INIT (videotestsrc_debug, "videotestsrc", 0,
      "Video Test Source");

  return gst_element_register (plugin, "videotestsrc", GST_RANK_NONE,
      GST_TYPE_VIDEOTESTSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videotestsrc",
    "Creates a test video stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
