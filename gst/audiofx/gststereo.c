/* Gnome-Streamer
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

#include <gststereo.h>


static GstElementDetails stereo_details = {
  "Stereo effect",
  "Filter/Effect",
  "Muck with the stereo signal, enhance it's 'stereo-ness'",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_ACTIVE,
  ARG_STEREO
};


static void	gst_stereo_class_init		(GstStereoClass *klass);
static void	gst_stereo_init			(GstStereo *stereo);

static void	gst_stereo_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_stereo_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_stereo_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_stereo_get_type(void) {
  static GType stereo_type = 0;

  if (!stereo_type) {
    static const GTypeInfo stereo_info = {
      sizeof(GstStereoClass),      NULL,
      NULL,
      (GClassInitFunc)gst_stereo_class_init,
      NULL,
      NULL,
      sizeof(GstStereo),
      0,
      (GInstanceInitFunc)gst_stereo_init,
    };
    stereo_type = g_type_register_static(GST_TYPE_ELEMENT, "GstStereo", &stereo_info, 0);
  }
  return stereo_type;
}

static void
gst_stereo_class_init (GstStereoClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ACTIVE,
    g_param_spec_int("active","active","active",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_STEREO,
    g_param_spec_float("stereo","stereo","stereo",
                       0.0,1.0,0.0,G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_stereo_set_property;
  gobject_class->get_property = gst_stereo_get_property;
}

static void
gst_stereo_init (GstStereo *stereo)
{
  stereo->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(stereo),stereo->sinkpad);
  gst_pad_set_chain_function(stereo->sinkpad,gst_stereo_chain);
  stereo->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(stereo),stereo->srcpad);

  stereo->active = FALSE;
  stereo->stereo = 2.5;
}

static void
gst_stereo_chain (GstPad *pad,GstBuffer *buf)
{
  GstStereo *stereo;
  gint16 *data;
  gint samples;
  gint i;
  gdouble avg,ldiff,rdiff,tmp,mul;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  stereo = GST_STEREO(GST_OBJECT_PARENT (pad));
  g_return_if_fail(stereo != NULL);
  g_return_if_fail(GST_IS_STEREO(stereo));

/*  FIXME */
/*  if (buf->meta) */
/*    memcpy(&stereo->meta,buf->meta,sizeof(stereo->meta)); */

  if (stereo->active) {

    /*if (stereo->meta.channels == 2 && stereo->meta.format == AFMT_S16_LE) { */
      data = (gint16 *)GST_BUFFER_DATA(buf);
      samples = GST_BUFFER_SIZE(buf) / 2;
      mul = stereo->stereo;
      for (i = 0; i < samples / 2; i += 2) {
        avg = (data[i] + data[i + 1]) / 2;
        ldiff = data[i] - avg;
        rdiff = data[i + 1] - avg;

        tmp = avg + ldiff * mul;
        if (tmp < -32768)
          tmp = -32768;
        if (tmp > 32767)
          tmp = 32767;
        data[i] = tmp;

        tmp = avg + rdiff * mul;
        if (tmp < -32768)
          tmp = -32768;
        if (tmp > 32767)
          tmp = 32767;
        data[i + 1] = tmp;
      }
    /*} */
  }

  gst_pad_push(stereo->srcpad,buf);
}

static void
gst_stereo_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstStereo *stereo;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_STEREO(object));
  stereo = GST_STEREO(object);

  switch (prop_id) {
    case ARG_ACTIVE:
      stereo->active = g_value_get_int (value);
      break;
    case ARG_STEREO:
      stereo->stereo = g_value_get_float (value) * 10.0;
      break;
    default:
      break;
  }
}

static void
gst_stereo_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstStereo *stereo;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_STEREO(object));
  stereo = GST_STEREO(object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_int (value, stereo->active);
      break;
    case ARG_STEREO:
      g_value_set_float (value, stereo->stereo / 10.0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("stereo",GST_TYPE_STEREO,
                                   &stereo_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "stereo",
  plugin_init
};

