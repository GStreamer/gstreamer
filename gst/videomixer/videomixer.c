/* Generic video mixer plugin
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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
#include <gst/base/gstcollectpads.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_videomixer_debug);
#define GST_CAT_DEFAULT gst_videomixer_debug

#define GST_TYPE_VIDEO_MIXER_PAD (gst_videomixer_pad_get_type())
#define GST_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_MIXER_PAD, GstVideoMixerPad))
#define GST_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER_PAD, GstVideoMixerPadiClass))
#define GST_IS_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_MIXER_PAD))
#define GST_IS_VIDEO_MIXER_PAD_CLASS(obj) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_MIXER_PAD))

typedef struct _GstVideoMixerPad GstVideoMixerPad;
typedef struct _GstVideoMixerPadClass GstVideoMixerPadClass;
typedef struct _GstVideoMixerCollect GstVideoMixerCollect;

#define GST_TYPE_VIDEO_MIXER (gst_videomixer_get_type())
#define GST_VIDEO_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_MIXER, GstVideoMixer))
#define GST_VIDEO_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER, GstVideoMixerClass))
#define GST_IS_VIDEO_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_MIXER))
#define GST_IS_VIDEO_MIXER_CLASS(obj) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_MIXER))

static GType gst_videomixer_get_type (void);

typedef struct _GstVideoMixer GstVideoMixer;
typedef struct _GstVideoMixerClass GstVideoMixerClass;

static void gst_videomixer_pad_base_init (gpointer g_class);
static void gst_videomixer_pad_class_init (GstVideoMixerPadClass * klass);
static void gst_videomixer_pad_init (GstVideoMixerPad * mixerpad);

static void gst_videomixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_videomixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_videomixer_sort_pads (GstVideoMixer * mix);

#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_ALPHA  1.0
enum
{
  ARG_PAD_0,
  ARG_PAD_ZORDER,
  ARG_PAD_XPOS,
  ARG_PAD_YPOS,
  ARG_PAD_ALPHA,
};

struct _GstVideoMixerCollect
{
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  GstVideoMixerPad *mixpad;
};

/* all information needed for one video stream */
struct _GstVideoMixerPad
{
  GstPad parent;                /* subclass the pad */

  guint64 queued;

  guint in_width, in_height;
  gint fps_n;
  gint fps_d;

  gint xpos, ypos;
  guint zorder;
  gint blend_mode;
  gdouble alpha;

  GstVideoMixerCollect *mixcol;
};

struct _GstVideoMixerPadClass
{
  GstPadClass parent_class;
};

static GType
gst_videomixer_pad_get_type (void)
{
  static GType videomixer_pad_type = 0;

  if (!videomixer_pad_type) {
    static const GTypeInfo videomixer_pad_info = {
      sizeof (GstVideoMixerPadClass),
      gst_videomixer_pad_base_init,
      NULL,
      (GClassInitFunc) gst_videomixer_pad_class_init,
      NULL,
      NULL,
      sizeof (GstVideoMixerPad),
      0,
      (GInstanceInitFunc) gst_videomixer_pad_init,
    };

    videomixer_pad_type = g_type_register_static (GST_TYPE_PAD,
        "GstVideoMixerPad", &videomixer_pad_info, 0);
  }
  return videomixer_pad_type;
}

static void
gst_videomixer_pad_base_init (gpointer g_class)
{
}

static void
gst_videomixer_pad_class_init (GstVideoMixerPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_videomixer_pad_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_videomixer_pad_get_property);

  g_object_class_install_property (gobject_class, ARG_PAD_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture",
          0.0, 1.0, DEFAULT_PAD_ALPHA, G_PARAM_READWRITE));
}

static void
gst_videomixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (object);

  switch (prop_id) {
    case ARG_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
      break;
    case ARG_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case ARG_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case ARG_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videomixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoMixerPad *pad;
  GstVideoMixer *mix;

  pad = GST_VIDEO_MIXER_PAD (object);
  mix = GST_VIDEO_MIXER (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case ARG_PAD_ZORDER:
      pad->zorder = g_value_get_uint (value);
      gst_videomixer_sort_pads (mix);
      break;
    case ARG_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      break;
    case ARG_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      break;
    case ARG_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (mix);
}

/**
  * GstVideoMixerBackground:
  */
typedef enum
{
  VIDEO_MIXER_BACKGROUND_CHECKER,
  VIDEO_MIXER_BACKGROUND_BLACK,
  VIDEO_MIXER_BACKGROUND_WHITE,
}
GstVideoMixerBackground;

struct _GstVideoMixer
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  /* Sink pads using Collect Pads from core's base library */
  GstCollectPads *collect;
  /* sinkpads, a GSList of GstVideoMixerPads */
  GSList *sinkpads;

  gint numpads;

  /* the master pad */
  GstVideoMixerPad *master;

  gint in_width, in_height;
  gint out_width, out_height;

  GstVideoMixerBackground background;

  gint fps_n;
  gint fps_d;
};

struct _GstVideoMixerClass
{
  GstElementClass parent_class;
};

static gboolean
gst_videomixer_pad_sink_setcaps (GstPad * pad, GstCaps * vscaps)
{
  GstVideoMixer *mix;
  GstVideoMixerPad *mixpad;
  GstStructure *structure;
  gint in_width, in_height;
  gboolean ret = FALSE;
  const GValue *framerate;

  mix = GST_VIDEO_MIXER (gst_pad_get_parent (pad));
  mixpad = GST_VIDEO_MIXER_PAD (pad);

  if (!mixpad) {
    goto beach;
  }

  GST_DEBUG_OBJECT (mixpad, "setcaps triggered");

  structure = gst_caps_get_structure (vscaps, 0);

  if (!gst_structure_get_int (structure, "width", &in_width)
      || !gst_structure_get_int (structure, "height", &in_height)
      || (framerate = gst_structure_get_value (structure, "framerate")) == NULL)
    goto beach;

  mixpad->fps_n = gst_value_get_fraction_numerator (framerate);
  mixpad->fps_d = gst_value_get_fraction_denominator (framerate);

  mixpad->in_width = in_width;
  mixpad->in_height = in_height;

  mixpad->xpos = 0;
  mixpad->ypos = 0;

  /* Biggest input geometry will be our output geometry */
  mix->in_width = MAX (mix->in_width, mixpad->in_width);
  mix->in_height = MAX (mix->in_height, mixpad->in_height);

  /* If mix framerate < mixpad framerate, using fractions */
  GST_DEBUG_OBJECT (mix, "comparing mix framerate %d/%d to mixpad's %d/%d",
      mix->fps_n, mix->fps_d, mixpad->fps_n, mixpad->fps_d);
  if ((!mix->fps_n && !mix->fps_d) ||
      ((gint64) mix->fps_n * mixpad->fps_d <
          (gint64) mixpad->fps_n * mix->fps_d)) {
    mix->fps_n = mixpad->fps_n;
    mix->fps_d = mixpad->fps_d;
    GST_DEBUG_OBJECT (mixpad, "becomes the master pad");
    mix->master = mixpad;
  }

  ret = TRUE;

beach:
  gst_object_unref (mix);

  return ret;
}

static void
gst_videomixer_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  GST_DEBUG_OBJECT (pad, "connected");
}

static void
gst_videomixer_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  GST_DEBUG_OBJECT (pad, "unlinked");
}

static void
gst_videomixer_pad_init (GstVideoMixerPad * mixerpad)
{
  g_signal_connect (mixerpad, "linked",
      G_CALLBACK (gst_videomixer_pad_link), mixerpad);
  g_signal_connect (mixerpad, "unlinked",
      G_CALLBACK (gst_videomixer_pad_unlink), mixerpad);

  /* setup some pad functions */
  gst_pad_set_setcaps_function (GST_PAD (mixerpad),
      gst_videomixer_pad_sink_setcaps);

  mixerpad->zorder = DEFAULT_PAD_ZORDER;
  mixerpad->xpos = DEFAULT_PAD_XPOS;
  mixerpad->ypos = DEFAULT_PAD_YPOS;
  mixerpad->alpha = DEFAULT_PAD_ALPHA;
}


/* elementfactory information */
static GstElementDetails gst_videomixer_details =
GST_ELEMENT_DETAILS ("video mixer",
    "Filter/Editor/Video",
    "Mix multiple video streams",
    "Wim Taymans <wim@fluendo.com>");

/* VideoMixer signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BACKGROUND VIDEO_MIXER_BACKGROUND_CHECKER
enum
{
  ARG_0,
  ARG_BACKGROUND,
};

#define GST_TYPE_VIDEO_MIXER_BACKGROUND (gst_video_mixer_background_get_type())
static GType
gst_video_mixer_background_get_type (void)
{
  static GType video_mixer_background_type = 0;
  static GEnumValue video_mixer_background[] = {
    {VIDEO_MIXER_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {VIDEO_MIXER_BACKGROUND_BLACK, "Black", "black"},
    {VIDEO_MIXER_BACKGROUND_WHITE, "White", "white"},
    {0, NULL, NULL},
  };

  if (!video_mixer_background_type) {
    video_mixer_background_type =
        g_enum_register_static ("GstVideoMixerBackground",
        video_mixer_background);
  }
  return video_mixer_background_type;
}

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv,"
        "format = (fourcc) AYUV,"
        "width = (int) [ 1, max ],"
        "height = (int) [ 1, max ]," "framerate = (fraction) [ 0/1, MAX ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-yuv,"
        "format = (fourcc) AYUV,"
        "width = (int) [ 1, max ],"
        "height = (int) [ 1, max ]," "framerate = (fraction) [ 0/1, MAX ]")
    );

static void gst_videomixer_base_init (gpointer g_class);
static void gst_videomixer_class_init (GstVideoMixerClass * klass);
static void gst_videomixer_init (GstVideoMixer * videomixer);

static GstCaps *gst_videomixer_getcaps (GstPad * pad);

static GstFlowReturn gst_videomixer_collected (GstCollectPads * pads,
    GstVideoMixer * mix);
static GstPad *gst_videomixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_videomixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videomixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_videomixer_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_videomixer_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_videomixer_get_type (void)
{
  static GType videomixer_type = 0;

  if (!videomixer_type) {
    static const GTypeInfo videomixer_info = {
      sizeof (GstVideoMixerClass),
      gst_videomixer_base_init,
      NULL,
      (GClassInitFunc) gst_videomixer_class_init,
      NULL,
      NULL,
      sizeof (GstVideoMixer),
      0,
      (GInstanceInitFunc) gst_videomixer_init,
    };

    videomixer_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVideoMixer",
        &videomixer_info, 0);
  }
  return videomixer_type;
}

static void
gst_videomixer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_videomixer_details);
}

static void
gst_videomixer_class_init (GstVideoMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->get_property = gst_videomixer_get_property;
  gobject_class->set_property = gst_videomixer_set_property;

  g_object_class_install_property (gobject_class, ARG_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_videomixer_request_new_pad;
  gstelement_class->change_state = gst_videomixer_change_state;
}

static void
gst_videomixer_init (GstVideoMixer * mix)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mix);

  mix->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (GST_PAD (mix->srcpad), gst_videomixer_getcaps);
  gst_element_add_pad (GST_ELEMENT (mix), mix->srcpad);

  mix->collect = gst_collect_pads_new ();
  mix->background = DEFAULT_BACKGROUND;
  mix->in_width = 0;
  mix->in_height = 0;
  mix->out_width = 0;
  mix->out_height = 0;

  gst_collect_pads_set_function (mix->collect,
      (GstCollectPadsFunction) gst_videomixer_collected, mix);
}

static GstCaps *
gst_videomixer_getcaps (GstPad * pad)
{
  GstVideoMixer *mix;
  GstCaps *caps;
  GstStructure *structure;

  mix = GST_VIDEO_MIXER (gst_pad_get_parent (pad));
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (mix->srcpad));

  structure = gst_caps_get_structure (caps, 0);

  if (mix->out_width != 0) {
    gst_structure_set (structure, "width", G_TYPE_INT, mix->out_width, NULL);
  }
  if (mix->out_height != 0) {
    gst_structure_set (structure, "height", G_TYPE_INT, mix->out_height, NULL);
  }
  if (mix->fps_d != 0) {
    gst_structure_set (structure,
        "framerate", GST_TYPE_FRACTION, mix->fps_n, mix->fps_d, NULL);
  }

  gst_object_unref (mix);

  return caps;
}

static GstPad *
gst_videomixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstVideoMixer *mix = NULL;
  GstVideoMixerPad *mixpad = NULL;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("videomixer: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (element), NULL);

  mix = GST_VIDEO_MIXER (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gint serial = 0;
    gchar *name = NULL;
    GstVideoMixerCollect *mixcol = NULL;

    if (req_name == NULL || strlen (req_name) < 6) {
      /* no name given when requesting the pad, use random serial number */
      serial = rand ();
    } else {
      /* parse serial number from requested padname */
      serial = atoi (&req_name[5]);
    }
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%d", serial);
    mixpad = g_object_new (GST_TYPE_VIDEO_MIXER_PAD, "name", name, "direction",
        templ->direction, "template", templ, NULL);
    g_free (name);

    mixpad->zorder = mix->numpads;
    mixpad->xpos = DEFAULT_PAD_XPOS;
    mixpad->ypos = DEFAULT_PAD_YPOS;
    mixpad->alpha = DEFAULT_PAD_ALPHA;

    mixcol = (GstVideoMixerCollect *)
        gst_collect_pads_add_pad (mix->collect, GST_PAD (mixpad),
        sizeof (GstVideoMixerCollect));

    /* Keep track of eachother */
    mixcol->mixpad = mixpad;
    mixpad->mixcol = mixcol;

    /* Keep an internal list of mixpads for zordering */
    mix->sinkpads = g_slist_append (mix->sinkpads, mixpad);
    mix->numpads++;
  } else {
    g_warning ("videomixer: this is not our template!\n");
    return NULL;
  }

  /* dd the pad to the element */
  gst_element_add_pad (element, GST_PAD (mixpad));

  return GST_PAD (mixpad);
}

#define BLEND_NORMAL(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)     \
        Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;           \
        U = ((U1*(255-alpha))+(U2*alpha))>>8;           \
        V = ((V1*(255-alpha))+(V2*alpha))>>8;

#define BLEND_ADD(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)                \
        Y = Y1+((Y2*alpha)>>8);                                 \
        U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;         \
        V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;         \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        }                                                       \
        U = MIN (U,255);                                        \
        V = MIN (V,255);

#define BLEND_SUBTRACT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)           \
        Y = Y1-((Y2*alpha)>>8);                                 \
        U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;         \
        V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;         \
        if (Y<0) {                                              \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }

#define BLEND_DARKEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)     \
        if (Y1 < Y2) {                                  \
          Y = Y1; U = U1; V = V1;                       \
        }                                               \
        else {                                          \
          Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;         \
          U = ((U1*(255-alpha))+(U2*alpha))>>8;         \
          V = ((V1*(255-alpha))+(V2*alpha))>>8;         \
        }

#define BLEND_LIGHTEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)    \
        if (Y1 > Y2) {                                  \
          Y = Y1; U = U1; V = V1;                       \
        }                                               \
        else {                                          \
          Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;         \
          U = ((U1*(255-alpha))+(U2*alpha))>>8;         \
          V = ((V1*(255-alpha))+(V2*alpha))>>8;         \
        }

#define BLEND_MULTIPLY(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)                   \
        Y = (Y1*(256*(255-alpha) +(Y2*alpha)))>>16;                     \
        U = ((U1*(255-alpha)*256)+(alpha*(U1*Y2+128*(256-Y2))))>>16;    \
        V = ((V1*(255-alpha)*256)+(alpha*(V1*Y2+128*(256-Y2))))>>16;

#define BLEND_DIFFERENCE(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)         \
        Y = ABS((gint)Y1-(gint)Y2)+127;                         \
        U = ABS((gint)U1-(gint)U2)+127;                         \
        V = ABS((gint)V1-(gint)V2)+127;                         \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \
        U = CLAMP(U, 0, 255);                                   \
        V = CLAMP(V, 0, 255);

#define BLEND_EXCLUSION(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = ((gint)(Y1^0xff)*Y2+(gint)(Y2^0xff)*Y1)>>8;         \
        U = ((gint)(U1^0xff)*Y2+(gint)(Y2^0xff)*U1)>>8;         \
        V = ((gint)(V1^0xff)*Y2+(gint)(Y2^0xff)*V1)>>8;         \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \
        U = CLAMP(U, 0, 255);                                   \
        V = CLAMP(V, 0, 255);

#define BLEND_SOFTLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = (gint)Y1+(gint)Y2 - 127;                            \
        U = (gint)U1+(gint)U2 - 127;                            \
        V = (gint)V1+(gint)V2 - 127;                            \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \

#define BLEND_HARDLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = (gint)Y1+(gint)Y2*2 - 255;                          \
        U = (gint)U1+(gint)U2 - 127;                            \
        V = (gint)V1+(gint)V2 - 127;                            \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \

#define BLEND_MODE BLEND_NORMAL
#if 0
#define BLEND_MODE BLEND_NORMAL
#define BLEND_MODE BLEND_ADD
#define BLEND_MODE BLEND_SUBTRACT
#define BLEND_MODE BLEND_LIGHTEN
#define BLEND_MODE BLEND_DARKEN
#define BLEND_MODE BLEND_MULTIPLY
#define BLEND_MODE BLEND_DIFFERENCE
#define BLEND_MODE BLEND_EXCLUSION
#define BLEND_MODE BLEND_SOFTLIGHT
#define BLEND_MODE BLEND_HARDLIGHT
#endif

#define ROUND_UP_2(x) (((x) + 1) & ~1)
#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

/* note that this function does packing conversion and blending at the
 * same time */
static void
gst_videomixer_blend_ayuv_ayuv (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  gint alpha, b_alpha;
  gint i, j;
  gint src_stride, dest_stride;
  gint src_add, dest_add;
  gint Y, U, V;

  src_stride = ROUND_UP_2 (src_width) * 4;
  dest_stride = ROUND_UP_2 (dest_width) * 4;

  b_alpha = (gint) (src_alpha * 255);

  /* adjust src pointers for negative sizes */
  if (xpos < 0) {
    src += -xpos * 4;
    src_width -= -xpos;
    xpos = 0;
  }
  if (ypos < 0) {
    src += -ypos * src_stride;
    src_height -= -ypos;
    ypos = 0;
  }
  /* adjust width/height if the src is bigger than dest */
  if (xpos + src_width > dest_width) {
    src_width = dest_width - xpos;
  }
  if (ypos + src_height > dest_height) {
    src_height = dest_height - ypos;
  }

  src_add = src_stride - (4 * ROUND_UP_2 (src_width));
  dest_add = dest_stride - (4 * ROUND_UP_2 (src_width));

  dest = dest + 4 * xpos + (ypos * dest_stride);

  /* we convert a square of 2x2 samples to generate 4 Luma and 2 chroma samples */
  for (i = 0; i < ROUND_UP_2 (src_height); i++) {
    for (j = 0; j < ROUND_UP_2 (src_width); j++) {
      alpha = (src[0] * b_alpha) >> 8;
      BLEND_MODE (dest[1], dest[2], dest[3], src[1], src[2], src[3],
          alpha, Y, U, V);
      dest[0] = 0xff;
      dest[1] = Y;
      dest[2] = U;
      dest[3] = V;

      src += 4;
      dest += 4;
    }
    src += src_add;
    dest += dest_add;
  }
}

#undef BLEND_MODE

static int
pad_zorder_compare (const GstVideoMixerPad * pad1,
    const GstVideoMixerPad * pad2)
{
  return pad1->zorder - pad2->zorder;
}

static void
gst_videomixer_sort_pads (GstVideoMixer * mix)
{
  mix->sinkpads = g_slist_sort (mix->sinkpads,
      (GCompareFunc) pad_zorder_compare);
}

/* fill a buffer with a checkerboard pattern */
static void
gst_videomixer_fill_checker (guint8 * dest, gint width, gint height)
{
  gint stride;
  gint i, j;
  static int tab[] = { 80, 160, 80, 160 };

  stride = ROUND_UP_2 (width);

  for (i = 0; i < height; i++) {
    for (j = 0; j < stride; j++) {
      *dest++ = 0xff;
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];
      *dest++ = 128;
      *dest++ = 128;
    }
  }
}

static void
gst_videomixer_fill_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  gint stride;
  gint i, j;

  stride = ROUND_UP_2 (width);

  for (i = 0; i < height; i++) {
    for (j = 0; j < stride; j++) {
      *dest++ = 0xff;
      *dest++ = colY;
      *dest++ = colU;
      *dest++ = colV;
    }
  }
}

/* try to get a buffer on all pads. As long as the queued value is
 * negative, we skip buffers */
static gboolean
gst_videomixer_fill_queues (GstVideoMixer * mix)
{
  GSList *walk = NULL;
  gboolean eos = TRUE;

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (mix), FALSE);

  /* try to make sure we have a buffer from each usable pad first */
  walk = mix->collect->data;
  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstVideoMixerCollect *mixcol = (GstVideoMixerCollect *) data;
    GstVideoMixerPad *mixpad = mixcol->mixpad;

    walk = g_slist_next (walk);

    if (mixcol->buffer == NULL) {
      GstBuffer *buf = NULL;

      GST_LOG ("we need a new buffer");

      buf = gst_collect_pads_pop (mix->collect, data);

      if (buf) {
        guint64 duration;

        GST_LOG ("we have a buffer !");

        mixcol->buffer = buf;
        duration = GST_BUFFER_DURATION (mixcol->buffer);
        /* no duration on the buffer, use the framerate */
        if (!GST_CLOCK_TIME_IS_VALID (duration)) {
          if (mixpad->fps_n == 0) {
            duration = GST_CLOCK_TIME_NONE;
          } else {
            duration = GST_SECOND * mixpad->fps_d / mixpad->fps_n;
          }
        }
        if (GST_CLOCK_TIME_IS_VALID (duration))
          mixpad->queued += duration;
        else if (!mixpad->queued)
          mixpad->queued = GST_CLOCK_TIME_NONE;
      } else {
        GST_LOG ("pop returned a NULL buffer");
      }
    }
    if (mixcol->buffer !=
        NULL /* && GST_CLOCK_TIME_IS_VALID (mixpad->queued) */ ) {
      /* got a buffer somewhere so we're not eos */
      eos = FALSE;
    }
  }

  return eos;
}

/* blend all buffers present on the pads */
static void
gst_videomixer_blend_buffers (GstVideoMixer * mix, GstBuffer * outbuf)
{
  GSList *walk;

  walk = mix->sinkpads;
  while (walk) {                /* We walk with this list because it's ordered */
    GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (walk->data);
    GstVideoMixerCollect *mixcol = pad->mixcol;

    walk = g_slist_next (walk);

    if (mixcol->buffer != NULL) {
      gst_videomixer_blend_ayuv_ayuv (GST_BUFFER_DATA (mixcol->buffer),
          pad->xpos, pad->ypos,
          pad->in_width, pad->in_height,
          pad->alpha,
          GST_BUFFER_DATA (outbuf), mix->out_width, mix->out_height);
      if (pad == mix->master) {
        GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (mixcol->buffer);
        GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (mixcol->buffer);
      }
    }
  }
}

/* remove buffers from the queue that were expired in the
 * interval of the master, we also prepare the queued value
 * in the pad so that we can skip and fill buffers later on */
static void
gst_videomixer_update_queues (GstVideoMixer * mix)
{
  GSList *walk;
  guint64 interval;

  interval = mix->master->queued;
  if (interval <= 0) {
    if (mix->fps_n == 0) {
      interval = G_MAXINT64;
    } else {
      interval = GST_SECOND * mix->fps_d / mix->fps_n;
    }
  }

  walk = mix->sinkpads;
  while (walk) {
    GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (walk->data);
    GstVideoMixerCollect *mixcol = pad->mixcol;

    walk = g_slist_next (walk);

    if (mixcol->buffer != NULL && GST_CLOCK_TIME_IS_VALID (pad->queued)) {
      pad->queued -= interval;
      GST_DEBUG_OBJECT (pad, "queued now %lld", pad->queued);
      if (pad->queued == 0) {
        GST_DEBUG ("unreffing buffer");
        gst_buffer_unref (mixcol->buffer);
        mixcol->buffer = NULL;
      }
    }
  }
}

/*
 * The basic idea is to get a buffer on all pads and mix them together.
 * Based on the framerate, buffers are removed from the queues to make room
 * for a new buffer. 
 
static void
gst_videomixer_loop (GstElement * element)
{
  GstVideoMixer *mix;
  GstBuffer *outbuf;
  gint outsize;
  gint new_width, new_height;
  gboolean eos;

  mix = GST_VIDEO_MIXER (element);

  eos = gst_videomixer_fill_queues (mix);
  if (eos) {
    gst_pad_push (mix->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (mix));
    return;
  }

  new_width = mix->in_width;
  new_height = mix->in_height;

  if (new_width != mix->out_width ||
      new_height != mix->out_height || !GST_PAD_CAPS (mix->srcpad)) {
    GstCaps *newcaps;

    newcaps = gst_caps_make_writable (
        gst_pad_get_negotiated_caps (GST_PAD (mix->master)));
    gst_caps_set_simple (newcaps, "format", GST_TYPE_FOURCC,
        GST_STR_FOURCC ("AYUV"), "width", G_TYPE_INT, new_width, "height",
        G_TYPE_INT, new_height, NULL);

    if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (mix->srcpad, newcaps))) {
      GST_ELEMENT_ERROR (mix, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }

    mix->out_width = new_width;
    mix->out_height = new_height;
  }

  outsize = ROUND_UP_2 (mix->out_width) * ROUND_UP_2 (mix->out_height) * 4;

  outbuf = gst_pad_alloc_buffer_and_set_caps (mix->srcpad, GST_BUFFER_OFFSET_NONE, outsize);
  switch (mix->background) {
    case VIDEO_MIXER_BACKGROUND_CHECKER:
      gst_videomixer_fill_checker (GST_BUFFER_DATA (outbuf),
          new_width, new_height);
      break;
    case VIDEO_MIXER_BACKGROUND_BLACK:
      gst_videomixer_fill_color (GST_BUFFER_DATA (outbuf),
          new_width, new_height, 16, 128, 128);
      break;
    case VIDEO_MIXER_BACKGROUND_WHITE:
      gst_videomixer_fill_color (GST_BUFFER_DATA (outbuf),
          new_width, new_height, 240, 128, 128);
      break;
  }

  gst_videomixer_blend_buffers (mix, outbuf);

  gst_videomixer_update_queues (mix);

  gst_pad_push (mix->srcpad, GST_DATA (outbuf));
}*/

static GstFlowReturn
gst_videomixer_collected (GstCollectPads * pads, GstVideoMixer * mix)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = NULL;
  size_t outsize = 0;
  gboolean eos = FALSE;

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (mix), GST_FLOW_ERROR);

  GST_LOG ("all pads are collected");

  eos = gst_videomixer_fill_queues (mix);

  if (eos) {
    /* Push EOS downstream */
    GST_LOG ("all our sinkpads are EOS, pushing downstream");
    gst_pad_push_event (mix->srcpad, gst_event_new_eos ());
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }

  /* Calculating out buffer size from input size */
  outsize = ROUND_UP_2 (mix->in_width) * ROUND_UP_2 (mix->in_height) * 4;

  /* If geometry has changed we need to set new caps on the buffer */
  if (mix->in_width != mix->out_width || mix->in_height != mix->out_height) {
    GstCaps *newcaps = NULL;

    newcaps = gst_caps_make_writable
        (gst_pad_get_negotiated_caps (GST_PAD (mix->master)));
    gst_caps_set_simple (newcaps,
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("AYUV"),
        "width", G_TYPE_INT, mix->in_width,
        "height", G_TYPE_INT, mix->in_height, NULL);

    mix->out_width = mix->in_width;
    mix->out_height = mix->in_height;

    ret =
        gst_pad_alloc_buffer_and_set_caps (mix->srcpad, GST_BUFFER_OFFSET_NONE,
        outsize, newcaps, &outbuf);
    gst_caps_unref (newcaps);
  } else {                      /* Otherwise we just allocate a buffer from current caps */
    ret =
        gst_pad_alloc_buffer_and_set_caps (mix->srcpad, GST_BUFFER_OFFSET_NONE,
        outsize, GST_PAD_CAPS (mix->srcpad), &outbuf);
  }

  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  switch (mix->background) {
    case VIDEO_MIXER_BACKGROUND_CHECKER:
      gst_videomixer_fill_checker (GST_BUFFER_DATA (outbuf),
          mix->out_width, mix->out_height);
      break;
    case VIDEO_MIXER_BACKGROUND_BLACK:
      gst_videomixer_fill_color (GST_BUFFER_DATA (outbuf),
          mix->out_width, mix->out_height, 16, 128, 128);
      break;
    case VIDEO_MIXER_BACKGROUND_WHITE:
      gst_videomixer_fill_color (GST_BUFFER_DATA (outbuf),
          mix->out_width, mix->out_height, 240, 128, 128);
      break;
  }

  gst_videomixer_blend_buffers (mix, outbuf);

  gst_videomixer_update_queues (mix);

  ret = gst_pad_push (mix->srcpad, outbuf);

beach:
  return ret;
}

static void
gst_videomixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoMixer *mix = GST_VIDEO_MIXER (object);

  switch (prop_id) {
    case ARG_BACKGROUND:
      g_value_set_enum (value, mix->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videomixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideoMixer *mix = GST_VIDEO_MIXER (object);

  switch (prop_id) {
    case ARG_BACKGROUND:
      mix->background = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_videomixer_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoMixer *mix;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (element), GST_STATE_CHANGE_FAILURE);

  mix = GST_VIDEO_MIXER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG ("starting collectpads");
      gst_collect_pads_start (mix->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG ("stopping collectpads");
      gst_collect_pads_stop (mix->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_videomixer_debug, "videomixer", 0,
      "video mixer");

  return gst_element_register (plugin, "videomixer", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_MIXER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videomixer",
    "Video mixer", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
