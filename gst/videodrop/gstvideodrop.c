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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gstvideodrop.h>

/* elementfactory information */
static GstElementDetails videodrop_details = GST_ELEMENT_DETAILS (
  "Video frame dropper",
  "Filter/Effect/Video",
  "Re-FPS'es video",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
);

/* GstVideodrop signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY(src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW(
    "framedropper_yuv",
    "video/x-raw-yuv",
      "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
      "width",     GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",    GST_PROPS_INT_RANGE (0, G_MAXINT),
      "format",    GST_PROPS_LIST (
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','V')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('U','Y','V','Y'))
                   )
  )
)

GST_PAD_TEMPLATE_FACTORY(sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW(
    "framedropper_yuv",
    "video/x-raw-yuv",
      "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
      "width",     GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",    GST_PROPS_INT_RANGE (0, G_MAXINT),
      "format",    GST_PROPS_LIST (
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','V')),
                     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('U','Y','V','Y'))
                   )
  )
)

static void	gst_videodrop_base_init		(gpointer g_class);
static void	gst_videodrop_class_init	(GstVideodropClass *klass);
static void	gst_videodrop_init		(GstVideodrop *videodrop);
static void	gst_videodrop_chain		(GstPad *pad, GstData *_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_videodrop_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_videodrop_get_type (void)
{
  static GType videodrop_type = 0;

  if (!videodrop_type) {
    static const GTypeInfo videodrop_info = {
      sizeof (GstVideodropClass),
      gst_videodrop_base_init,
      NULL,
      (GClassInitFunc) gst_videodrop_class_init,
      NULL,
      NULL,
      sizeof (GstVideodrop),
      0,
      (GInstanceInitFunc) gst_videodrop_init,
    };

    videodrop_type = g_type_register_static (GST_TYPE_ELEMENT,
					     "GstVideodrop",
					     &videodrop_info, 0);
  }

  return videodrop_type;
}

static void
gst_videodrop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videodrop_details);

  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_template));
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (src_template));
}
static void
gst_videodrop_class_init (GstVideodropClass *klass)
{
  parent_class = g_type_class_peek_parent (klass);
}

#define gst_caps_get_float_range(caps, name, min, max) \
  gst_props_entry_get_float_range(gst_props_get_entry((caps)->properties, \
                                                    name), \
                                  min, max)

static GstPadLinkReturn
gst_videodrop_link (GstPad *pad, GstCaps *caps)
{
  GstVideodrop *videodrop;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));

  videodrop->inited = FALSE;

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_get_float (caps, "framerate", &videodrop->from_fps);

  /* calc output fps */
  peercaps = gst_pad_get_allowed_caps (videodrop->srcpad);
  if (gst_caps_has_fixed_property (peercaps, "framerate")) {
    gst_caps_get_float (peercaps, "framerate", &videodrop->to_fps);
  } else {
    gfloat min, max;
    gst_caps_get_float_range (peercaps, "framerate", &min, &max);
    if (videodrop->from_fps >= min &&
        videodrop->from_fps <= max) {
      videodrop->to_fps = videodrop->from_fps;
    } else {
      videodrop->to_fps = max;
    }
  }
  gst_caps_unref (peercaps);

  GST_DEBUG ("%f -> %f fps",
	     videodrop->from_fps, videodrop->to_fps);

  peercaps = gst_caps_copy (caps);

  peercaps->properties = gst_caps_set (peercaps, "framerate",
				       GST_PROPS_FLOAT (videodrop->to_fps));

  if ((ret = gst_pad_try_set_caps (videodrop->srcpad, peercaps)) > 0) {
    videodrop->inited = TRUE;
    videodrop->total = videodrop->pass = 0;
  }

  return ret;
}

static void
gst_videodrop_init (GstVideodrop *videodrop)
{
  GST_DEBUG ("gst_videodrop_init");
  videodrop->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template),
		  "sink");
  gst_element_add_pad (GST_ELEMENT (videodrop), videodrop->sinkpad);
  gst_pad_set_chain_function (videodrop->sinkpad, gst_videodrop_chain);
  gst_pad_set_link_function (videodrop->sinkpad, gst_videodrop_link);

  videodrop->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template),
		  "src");
  gst_element_add_pad (GST_ELEMENT(videodrop), videodrop->srcpad);

  videodrop->inited = FALSE;
  videodrop->total = videodrop->pass = 0;
}

static void
gst_videodrop_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideodrop *videodrop;

  GST_DEBUG ("gst_videodrop_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videodrop = GST_VIDEODROP (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    gst_pad_push (videodrop->srcpad, GST_DATA (buf));
    return;
  }

  videodrop->total++;
  while (videodrop->to_fps / videodrop->from_fps >
	 (gfloat) videodrop->pass / videodrop->total) {
    videodrop->pass++;
    gst_buffer_ref (buf);
    gst_pad_push (videodrop->srcpad, GST_DATA (buf));
  }

  gst_buffer_unref (buf);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "videodrop", GST_RANK_NONE, GST_TYPE_VIDEODROP);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videodrop",
  "Re-FPS'es video",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
