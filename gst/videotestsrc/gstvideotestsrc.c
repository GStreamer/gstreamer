/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


/*#define DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstvideotestsrc.h>
#include <videotestsrc.h>

#include <string.h>
#include <stdlib.h>



/* elementfactory information */
static GstElementDetails videotestsrc_details = GST_ELEMENT_DETAILS (
  "Video test source",
  "Source/Video",
  "Creates a test video stream",
  "David A. Schleef <ds@schleef.org>"
);

/* GstVideotestsrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FOURCC,
  ARG_RATE,
  ARG_TYPE,
  ARG_SYNC,
  /* FILL ME */
};

static void gst_videotestsrc_base_init (gpointer g_class);
static void gst_videotestsrc_class_init (GstVideotestsrcClass * klass);
static void gst_videotestsrc_init (GstVideotestsrc * videotestsrc);
static GstElementStateReturn gst_videotestsrc_change_state (GstElement * element);
static void gst_videotestsrc_set_clock (GstElement *element, GstClock *clock);

static void gst_videotestsrc_set_pattern (GstVideotestsrc *src, int pattern_type);
static void gst_videotestsrc_set_property (GObject * object, guint prop_id,
					   const GValue * value, GParamSpec * pspec);
static void gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value,
					   GParamSpec * pspec);

static GstData *gst_videotestsrc_get (GstPad * pad);

static const GstQueryType *
		gst_videotestsrc_get_query_types (GstPad      *pad);
static gboolean gst_videotestsrc_src_query (GstPad      *pad,
					    GstQueryType type,
					    GstFormat   *format,
					    gint64      *value);

static GstElementClass *parent_class = NULL;

static GstCaps * gst_videotestsrc_get_capslist (void);
#if 0
static GstCaps * gst_videotestsrc_get_capslist_size (int width, int height, double rate);
#endif


static GstPadTemplate *
gst_videotestsrc_src_template_factory(void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_videotestsrc_get_capslist());
}

GType
gst_videotestsrc_get_type (void)
{
  static GType videotestsrc_type = 0;

  if (!videotestsrc_type) {
    static const GTypeInfo videotestsrc_info = {
      sizeof (GstVideotestsrcClass),
      gst_videotestsrc_base_init,
      NULL,
      (GClassInitFunc) gst_videotestsrc_class_init,
      NULL,
      NULL,
      sizeof (GstVideotestsrc),
      0,
      (GInstanceInitFunc) gst_videotestsrc_init,
    };

    videotestsrc_type =
      g_type_register_static (GST_TYPE_ELEMENT, "GstVideotestsrc", &videotestsrc_info, 0);
  }
  return videotestsrc_type;
}

#define GST_TYPE_VIDEOTESTSRC_PATTERN (gst_videotestsrc_pattern_get_type ())
static GType
gst_videotestsrc_pattern_get_type (void)
{
  static GType videotestsrc_pattern_type = 0;
  static GEnumValue pattern_types[] = {
    { GST_VIDEOTESTSRC_SMPTE, "smpte", "SMPTE 100% color bars" },
    { GST_VIDEOTESTSRC_SNOW,  "snow",  "Random (television snow)" },
    { GST_VIDEOTESTSRC_BLACK, "black", "0% Black" },
    { 0, NULL, NULL },
  };

  if (!videotestsrc_pattern_type){
    videotestsrc_pattern_type = g_enum_register_static("GstVideotestsrcPattern",
		    pattern_types);
  }
  return videotestsrc_pattern_type;
}

static void
gst_videotestsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videotestsrc_details);

  gst_element_class_add_pad_template (element_class,
	  gst_videotestsrc_src_template_factory());
}
static void
gst_videotestsrc_class_init (GstVideotestsrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width", "width", "Default width",
        1, G_MAXINT, 320, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
      g_param_spec_int ("height", "height", "Default height",
        1, G_MAXINT, 240, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FOURCC,
      g_param_spec_string ("fourcc", "fourcc", "fourcc",
        NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE,
      g_param_spec_double ("fps", "FPS", "Default frame rate",
        0., G_MAXDOUBLE, 30., G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TYPE,
      g_param_spec_enum ("pattern", "Pattern", "Type of test pattern to generate",
        GST_TYPE_VIDEOTESTSRC_PATTERN, 1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Synchronize to clock",
        TRUE, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videotestsrc_set_property;
  gobject_class->get_property = gst_videotestsrc_get_property;

  gstelement_class->change_state = gst_videotestsrc_change_state;
  gstelement_class->set_clock    = gst_videotestsrc_set_clock;
}

static void
gst_videotestsrc_set_clock (GstElement *element, GstClock *clock)
{
  GstVideotestsrc *v;

  v = GST_VIDEOTESTSRC (element);

  gst_object_replace ((GstObject **)&v->clock, (GstObject *)clock);
}

static GstCaps *
gst_videotestsrc_src_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  /* FIXME this function isn't very intelligent in choosing "good" caps */

  if (gst_caps_get_size (caps) > 1) return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
	30.0)) {
    return newcaps;
  }

  /* failed to fixate */
  gst_caps_free (newcaps);
  return NULL;
}

static GstPadLinkReturn
gst_videotestsrc_src_link (GstPad * pad, const GstCaps * caps)
{
  GstVideotestsrc *videotestsrc;
  const GstStructure *structure;
  GstPadLinkReturn ret;

  GST_DEBUG ("gst_videotestsrc_src_link");
  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  videotestsrc->fourcc = paintinfo_find_by_structure(structure);
  if (!videotestsrc->fourcc) {
    g_critical ("videotestsrc format not found\n");
    return GST_PAD_LINK_REFUSED;
  }

  ret = gst_structure_get_int (structure, "width", &videotestsrc->width);
  ret &= gst_structure_get_int (structure, "height", &videotestsrc->height);
  ret &= gst_structure_get_double (structure, "framerate",
      &videotestsrc->rate);

  if (!ret) return GST_PAD_LINK_REFUSED;

  videotestsrc->bpp = videotestsrc->fourcc->bitspp;

  GST_DEBUG ("size %d x %d", videotestsrc->width, videotestsrc->height);

  return GST_PAD_LINK_OK;
}

static void
gst_videotestsrc_src_unlink (GstPad * pad)
{
  GstVideotestsrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));
}

static GstElementStateReturn
gst_videotestsrc_change_state (GstElement * element)
{
  GstVideotestsrc *videotestsrc;

  videotestsrc = GST_VIDEOTESTSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      videotestsrc->timestamp_offset = 0;
      videotestsrc->n_frames = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return parent_class->change_state (element);
}

static GstCaps *
gst_videotestsrc_get_capslist (void)
{
  GstCaps *caps;
  GstStructure *structure;
  int i;

  caps = gst_caps_new_empty();
  for(i=0;i<n_fourccs;i++){
    structure = paint_get_structure (fourcc_list + i);
    gst_structure_set(structure,
	"width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
	"height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
	"framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}

#if 0
static GstCaps *
gst_videotestsrc_get_capslist_size (int width, int height, double rate)
{
  GstCaps *caps;
  GstStructure *structure;
  int i;

  caps = gst_caps_new_empty();
  for(i=0;i<n_fourccs;i++){
    structure = paint_get_structure (fourcc_list + i);
    gst_structure_set(structure,
	"width", G_TYPE_INT, width,
	"height", G_TYPE_INT, height,
	"framerate", G_TYPE_INT, rate, NULL);
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}
#endif

static GstCaps *
gst_videotestsrc_getcaps (GstPad * pad)
{
  GstVideotestsrc *vts;

  vts = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  return gst_videotestsrc_get_capslist ();
}

static void
gst_videotestsrc_init (GstVideotestsrc * videotestsrc)
{
  GST_DEBUG ("gst_videotestsrc_init");

  videotestsrc->srcpad = gst_pad_new_from_template (
      gst_videotestsrc_src_template_factory(), "src");
  gst_pad_set_getcaps_function (videotestsrc->srcpad, gst_videotestsrc_getcaps);
  gst_pad_set_fixate_function (videotestsrc->srcpad, gst_videotestsrc_src_fixate);
  gst_element_add_pad (GST_ELEMENT (videotestsrc), videotestsrc->srcpad);
  gst_pad_set_get_function (videotestsrc->srcpad, gst_videotestsrc_get);
  gst_pad_set_link_function (videotestsrc->srcpad, gst_videotestsrc_src_link);
  gst_pad_set_unlink_function (videotestsrc->srcpad, gst_videotestsrc_src_unlink);
  gst_pad_set_query_function (videotestsrc->srcpad, gst_videotestsrc_src_query);
  gst_pad_set_query_type_function (videotestsrc->srcpad,
				   gst_videotestsrc_get_query_types);

  gst_videotestsrc_set_pattern(videotestsrc, GST_VIDEOTESTSRC_SMPTE);

  videotestsrc->sync = TRUE;
  videotestsrc->default_width = 320;
  videotestsrc->default_height = 240;
  videotestsrc->default_rate = 30.;
}


static const GstQueryType *
gst_videotestsrc_get_query_types (GstPad *pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
} 

static gboolean
gst_videotestsrc_src_query (GstPad      *pad,
			    GstQueryType type,
			    GstFormat   *format,
			    gint64      *value)
{
  gboolean res = FALSE;
  GstVideotestsrc *videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));
	        
  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = videotestsrc->n_frames * GST_SECOND / (double) videotestsrc->rate;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT: /* frames */
          *value = videotestsrc->n_frames;
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  return res;
}


static GstData *
gst_videotestsrc_get (GstPad * pad)
{
  GstVideotestsrc *videotestsrc;
  gulong newsize;
  GstBuffer *buf;
  GstClockTimeDiff jitter = 0;

  GST_DEBUG ("gst_videotestsrc_get");

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  if (videotestsrc->fourcc == NULL) {
    gst_element_error (GST_ELEMENT (videotestsrc),
		       "No color format set - aborting");
    return NULL;
  }

  newsize = (videotestsrc->width * videotestsrc->height * videotestsrc->bpp) >> 3;
  g_return_val_if_fail (newsize > 0, NULL);

  GST_DEBUG ("size=%ld %dx%d", newsize, videotestsrc->width, videotestsrc->height);

  buf = gst_buffer_new_and_alloc (newsize);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  videotestsrc->make_image (videotestsrc, (void *) GST_BUFFER_DATA (buf),
			    videotestsrc->width, videotestsrc->height);
  
  if (videotestsrc->sync){
    do {
      GstClockID id;

      GST_BUFFER_TIMESTAMP (buf) = videotestsrc->timestamp_offset +
	(videotestsrc->n_frames * GST_SECOND)/(double)videotestsrc->rate;
      videotestsrc->n_frames++;

      /* FIXME this is not correct if we do QoS */
      if (videotestsrc->clock) {
        id = gst_clock_new_single_shot_id (videotestsrc->clock,
	    GST_BUFFER_TIMESTAMP (buf));
        gst_element_clock_wait (GST_ELEMENT (videotestsrc), id, &jitter);
        gst_clock_id_free (id);
      }
    } while (jitter > 100 * GST_MSECOND);
  }else{
    GST_BUFFER_TIMESTAMP (buf) = videotestsrc->timestamp_offset +
      (videotestsrc->n_frames * GST_SECOND)/(double)videotestsrc->rate;
    videotestsrc->n_frames++;
  }
  GST_BUFFER_DURATION (buf) = GST_SECOND / (double) videotestsrc->rate;

  return GST_DATA (buf);
}

static void
gst_videotestsrc_set_pattern (GstVideotestsrc *src, int pattern_type)
{
  src->type = pattern_type;

  GST_DEBUG ("setting pattern to %d\n",pattern_type);
  switch(pattern_type){
    case GST_VIDEOTESTSRC_SMPTE:
      src->make_image = gst_videotestsrc_smpte;
      break;
    case GST_VIDEOTESTSRC_SNOW:
      src->make_image = gst_videotestsrc_snow;
      break;
    case GST_VIDEOTESTSRC_BLACK:
      src->make_image = gst_videotestsrc_black;
      break;
    default:
      g_assert_not_reached();
  }
}

static void
gst_videotestsrc_set_property (GObject * object, guint prop_id, const GValue * value,
			       GParamSpec * pspec)
{
  GstVideotestsrc *src;
  const char *format;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOTESTSRC (object));
  src = GST_VIDEOTESTSRC (object);

  GST_DEBUG ("gst_videotestsrc_set_property");
  switch (prop_id) {
    case ARG_WIDTH:
      src->default_width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->default_height = g_value_get_int (value);
      break;
    case ARG_FOURCC:
      format = g_value_get_string (value);
      if(paintrect_find_name (format) != NULL){
        src->forced_format = g_strdup(format);
        GST_DEBUG ("forcing format to \"%s\"\n", format);
      }else{
        GST_DEBUG ("unknown format \"%s\"\n", format);
      }
      break;
    case ARG_RATE:
      src->default_rate = g_value_get_double (value);
      break;
    case ARG_TYPE:
      gst_videotestsrc_set_pattern (src, g_value_get_enum (value));
      break;
    case ARG_SYNC:
      src->sync = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideotestsrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOTESTSRC (object));
  src = GST_VIDEOTESTSRC (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, src->default_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->default_height);
      break;
    case ARG_FOURCC:
      g_value_set_string (value, src->forced_format);
      break;
    case ARG_RATE:
      g_value_set_double (value, src->default_rate);
      break;
    case ARG_TYPE:
      g_value_set_enum (value, src->type);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, src->sync);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videotestsrc", GST_RANK_NONE, GST_TYPE_VIDEOTESTSRC);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videotestsrc",
  "Creates a test video stream",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
