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
static GstElementDetails videotestsrc_details = {
  "Video test source",
  "Source/Video",
  "LGPL",
  "Creates a test video stream",
  VERSION,
  "David A. Schleef <ds@schleef.org>",
  "(C) 2002",
};

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

static void gst_videotestsrc_class_init (GstVideotestsrcClass * klass);
static void gst_videotestsrc_init (GstVideotestsrc * videotestsrc);
static GstElementStateReturn gst_videotestsrc_change_state (GstElement * element);
static void gst_videotestsrc_set_clock (GstElement *element, GstClock *clock);

static void gst_videotestsrc_set_pattern (GstVideotestsrc *src, int pattern_type);
static void gst_videotestsrc_set_property (GObject * object, guint prop_id,
					   const GValue * value, GParamSpec * pspec);
static void gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value,
					   GParamSpec * pspec);

static GstBuffer *gst_videotestsrc_get (GstPad * pad);

static void gst_videotestsrc_reset (GstVideotestsrc *videotestsrc);

static GstElementClass *parent_class = NULL;

static GstCaps * gst_videotestsrc_get_capslist (void);


static GstPadTemplate *
videotestsrc_src_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps;
    GstCaps *caps1 = GST_CAPS_NEW("src","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (1, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (1, G_MAXINT),
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));
    GstCaps *caps2 = GST_CAPS_NEW("src","video/x-raw-rgb",
		"width", GST_PROPS_INT_RANGE (1, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (1, G_MAXINT),
                "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

    caps = gst_caps_intersect(caps1, gst_videotestsrc_get_capslist ());
    gst_caps_unref (caps1);
    caps1 = caps;
    caps = gst_caps_intersect(caps2, gst_videotestsrc_get_capslist ());
    gst_caps_unref (caps2);
    caps2 = caps;
    caps = gst_caps_append(caps1, caps2);

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

GType
gst_videotestsrc_get_type (void)
{
  static GType videotestsrc_type = 0;

  if (!videotestsrc_type) {
    static const GTypeInfo videotestsrc_info = {
      sizeof (GstVideotestsrcClass), NULL,
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
gst_videotestsrc_class_init (GstVideotestsrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width", "width", "width",
        G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));	/* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
      g_param_spec_int ("height", "height", "height",
        G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));	/* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FOURCC,
      g_param_spec_string ("fourcc", "fourcc", "fourcc",
        NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE,
      g_param_spec_float ("fps", "FPS", "Frame rate",
        0., G_MAXFLOAT, 30., G_PARAM_READWRITE));
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

#if GST_VERSION_MINOR > 6
  gst_object_replace ((GstObject **)&v->clock, (GstObject *)clock);
#else
  gst_object_swap ((GstObject **)&v->clock, (GstObject *)clock);
#endif
}

static GstPadLinkReturn
gst_videotestsrc_srcconnect (GstPad * pad, GstCaps * caps)
{
  GstVideotestsrc *videotestsrc;

  GST_DEBUG ("gst_videotestsrc_srcconnect");
  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  for ( ; caps != NULL; caps = caps->next) {
    GstCaps *caps1 = gst_caps_copy_1(caps);
    GstPadLinkReturn ret;

    gst_caps_set(caps1, "framerate", GST_PROPS_FLOAT((float)videotestsrc->rate));
    gst_caps_set(caps1, "width", GST_PROPS_INT(videotestsrc->width));
    gst_caps_set(caps1, "height", GST_PROPS_INT(videotestsrc->height));

    //g_print("%s\n", gst_caps_to_string(caps1));

    ret = gst_pad_try_set_caps(pad, caps1);

    if (ret != GST_PAD_LINK_OK &&
        ret != GST_PAD_LINK_DONE) {
      gst_caps_unref(caps1);
      continue;
    }

    videotestsrc->fourcc = paintinfo_find_by_caps(caps1);
    if (!videotestsrc->fourcc) {
      gst_caps_unref(caps1);
      continue;
    }

    /* if we get here, it's allright */
    gst_caps_unref(caps1);
    break;
  }

  if (caps == NULL) {
    GST_DEBUG ("videotestsrc: no suitable opposite-side caps found");
    return GST_PAD_LINK_REFUSED;
  }

  GST_DEBUG ("videotestsrc: using fourcc element %p %s\n",
	videotestsrc->fourcc, videotestsrc->fourcc->name);

  if(videotestsrc->width==0){
    gst_caps_get_int (caps, "width", &videotestsrc->width);
  }
  if(videotestsrc->height==0){
    gst_caps_get_int (caps, "height", &videotestsrc->height);
  }
  if(videotestsrc->rate==0){
    gst_caps_get_float (caps, "framerate", &videotestsrc->rate);
  }

  videotestsrc->bpp = videotestsrc->fourcc->bitspp;

  GST_DEBUG ("size %d x %d", videotestsrc->width, videotestsrc->height);

  return GST_PAD_LINK_DONE;
}

static GstElementStateReturn
gst_videotestsrc_change_state (GstElement * element)
{
  GstVideotestsrc *v;

  v = GST_VIDEOTESTSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      v->pool = gst_pad_get_bufferpool (v->srcpad);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      v->pool = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      gst_videotestsrc_reset (v);
      break;
  }

  return parent_class->change_state (element);
}

static GstCaps *
gst_videotestsrc_get_capslist (void)
{
  static GstCaps *capslist = NULL;
  GstCaps *caps;
  int i;

  if (capslist)
    return capslist;

  for(i=0;i<n_fourccs;i++){
    caps = paint_get_caps(fourcc_list + i);
    capslist = gst_caps_append(capslist, caps);
  }

  return capslist;
}

static GstCaps *
gst_videotestsrc_getcaps (GstPad * pad, GstCaps * caps)
{
  GstVideotestsrc *vts;
  GstCaps *caps1;
  GstCaps *caps2;

  vts = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  caps1 = NULL;

  if (vts->forced_format != NULL) {
    struct fourcc_list_struct *fourcc;

    fourcc = paintrect_find_name (vts->forced_format);
    if (fourcc) {
      caps1 = paint_get_caps(fourcc);
    }
  }

  if (caps1 == NULL) {
    caps1 = gst_videotestsrc_get_capslist ();
  }

  if(vts->width){
    caps2 = gst_caps_append(
            GST_CAPS_NEW("ack","video/x-raw-yuv",
		"width",GST_PROPS_INT(vts->width),
		"height",GST_PROPS_INT(vts->height),
                "framerate", GST_PROPS_FLOAT(vts->rate)),
            GST_CAPS_NEW("ack","video/x-raw-rgb",
		"width",GST_PROPS_INT(vts->width),
		"height",GST_PROPS_INT(vts->height),
                "framerate", GST_PROPS_FLOAT(vts->rate)));
  }else{
    caps2 = gst_caps_append(
             GST_CAPS_NEW("ack","video/x-raw-yuv",
		"width",GST_PROPS_INT_RANGE(16,4096),
		"height",GST_PROPS_INT_RANGE(16,4096),
                "framerate", GST_PROPS_FLOAT(vts->rate)),
             GST_CAPS_NEW("ack","video/x-raw-rgb",
		"width",GST_PROPS_INT_RANGE(16,4096),
		"height",GST_PROPS_INT_RANGE(16,4096),
                "framerate", GST_PROPS_FLOAT(vts->rate)));
  }

  /* ref intersection and return it */
  return gst_caps_ref (gst_caps_intersect(caps1,caps2));
}

static void
gst_videotestsrc_init (GstVideotestsrc * videotestsrc)
{
  GST_DEBUG ("gst_videotestsrc_init");

  videotestsrc->srcpad =
    gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (videotestsrc_src_template_factory), "src");
  gst_pad_set_getcaps_function (videotestsrc->srcpad, gst_videotestsrc_getcaps);
  gst_element_add_pad (GST_ELEMENT (videotestsrc), videotestsrc->srcpad);
  gst_pad_set_get_function (videotestsrc->srcpad, gst_videotestsrc_get);
  gst_pad_set_link_function (videotestsrc->srcpad, gst_videotestsrc_srcconnect);

  videotestsrc->pool = NULL;
  gst_videotestsrc_set_pattern(videotestsrc, GST_VIDEOTESTSRC_SMPTE);
  gst_videotestsrc_reset (videotestsrc);
}

static void
gst_videotestsrc_reset (GstVideotestsrc *videotestsrc)
{
  videotestsrc->sync = TRUE;
  videotestsrc->width = 640;
  videotestsrc->height = 480;
  videotestsrc->rate = 30.;
  videotestsrc->timestamp = 0;
  videotestsrc->interval = GST_SECOND / videotestsrc->rate;
}


static GstBuffer *
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

  newsize = (videotestsrc->width * videotestsrc->height * videotestsrc->bpp) >> 3;

  GST_DEBUG ("size=%ld %dx%d\n", newsize, videotestsrc->width, videotestsrc->height);

  buf = NULL;
  if (videotestsrc->pool) {
    buf = gst_buffer_new_from_pool (videotestsrc->pool, 0, 0);
    /* if the buffer we get is too small, make our own */
    if (buf && GST_BUFFER_SIZE (buf) < newsize){
      gst_buffer_unref (buf);
      buf = NULL;
    }
  }
  if (!buf) {
    buf = gst_buffer_new ();
    GST_BUFFER_SIZE (buf) = newsize;
    GST_BUFFER_DATA (buf) = g_malloc (newsize);
  }
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  videotestsrc->make_image (videotestsrc, (void *) GST_BUFFER_DATA (buf),
			    videotestsrc->width, videotestsrc->height);
  
  if (videotestsrc->sync){
    do {
      GstClockID id;

      videotestsrc->timestamp += videotestsrc->interval;
      GST_BUFFER_TIMESTAMP (buf) = videotestsrc->timestamp;

      if (videotestsrc->clock) {
        id = gst_clock_new_single_shot_id (videotestsrc->clock, GST_BUFFER_TIMESTAMP (buf));
        gst_element_clock_wait (GST_ELEMENT (videotestsrc), id, &jitter);
        gst_clock_id_free (id);
      }
    } while (jitter > 100 * GST_MSECOND);
  }

  return buf;
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
      src->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->height = g_value_get_int (value);
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
      src->rate = g_value_get_float (value);
      src->interval = GST_SECOND/src->rate;
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
      g_value_set_int (value, src->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->height);
      break;
    case ARG_FOURCC:
      g_value_set_string (value, src->forced_format);
      break;
    case ARG_RATE:
      g_value_set_float (value, src->rate);
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
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the videotestsrc element */
  factory = gst_element_factory_new ("videotestsrc", GST_TYPE_VIDEOTESTSRC, &videotestsrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory,
					GST_PAD_TEMPLATE_GET (videotestsrc_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videotestsrc",
  plugin_init
};



