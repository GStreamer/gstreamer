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

/* all information needed for one video stream */
struct _GstVideoMixerPad
{
  GstRealPad parent;            /* subclass the pad */

  GstBuffer *buffer;            /* the queued buffer for this pad */
  gboolean eos;

  gint64 queued;

  guint in_width, in_height;
  gdouble in_framerate;

  gint xpos, ypos;
  guint zorder;
  gint blend_mode;
  gdouble alpha;
};

struct _GstVideoMixerPadClass
{
  GstRealPadClass parent_class;
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

    videomixer_pad_type =
        g_type_register_static (GST_TYPE_REAL_PAD,
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

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAD_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture",
          0.0, 1.0, DEFAULT_PAD_ALPHA, G_PARAM_READWRITE));
}

static const GstEventMask *
gst_videomixer_pad_get_sink_event_masks (GstPad * pad)
{
  static const GstEventMask gst_videomixer_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {0,}
  };

  return gst_videomixer_sink_event_masks;
}

static void
gst_videomixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoMixerPad *pad;

  g_return_if_fail (GST_IS_VIDEO_MIXER_PAD (object));

  pad = GST_VIDEO_MIXER_PAD (object);

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

  g_return_if_fail (GST_IS_PAD (object));

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
}

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

  /* sinkpads, a GSList of GstVideoMixerPads */
  GSList *sinkpads;
  gint numpads;

  /* the master pad */
  GstVideoMixerPad *master;

  gint in_width, in_height;
  gint out_width, out_height;

  GstVideoMixerBackground background;

  gdouble in_framerate;
};

struct _GstVideoMixerClass
{
  GstElementClass parent_class;
};

static GstPadLinkReturn
gst_videomixer_pad_sinkconnect (GstPad * pad, const GstCaps * vscaps)
{
  GstVideoMixer *mix;
  GstVideoMixerPad *mixpad;
  GstStructure *structure;

  mix = GST_VIDEO_MIXER (gst_pad_get_parent (pad));
  mixpad = GST_VIDEO_MIXER_PAD (pad);

  GST_DEBUG ("videomixer: sinkconnect triggered on %s", gst_pad_get_name (pad));

  structure = gst_caps_get_structure (vscaps, 0);

  gst_structure_get_int (structure, "width", &mixpad->in_width);
  gst_structure_get_int (structure, "height", &mixpad->in_height);
  gst_structure_get_double (structure, "framerate", &mixpad->in_framerate);

  mixpad->xpos = 0;
  mixpad->ypos = 0;

  mix->in_width = MAX (mix->in_width, mixpad->in_width);
  mix->in_height = MAX (mix->in_height, mixpad->in_height);
  if (mix->in_framerate < mixpad->in_framerate) {
    mix->in_framerate = mixpad->in_framerate;
    mix->master = mixpad;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_videomixer_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstVideoMixer *videomixer = GST_VIDEO_MIXER (data);
  const gchar *padname = gst_pad_get_name (pad);

  GST_DEBUG ("pad '%s' connected", padname);
}

static void
gst_videomixer_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  //GstVideoMixer *videomixer = GST_VIDEO_MIXER (data);
  const gchar *padname = gst_pad_get_name (pad);

  GST_DEBUG ("pad '%s' unlinked", padname);
}

static void
gst_videomixer_pad_init (GstVideoMixerPad * mixerpad)
{
  g_signal_connect (mixerpad, "linked",
      G_CALLBACK (gst_videomixer_pad_link), (gpointer) mixerpad);
  g_signal_connect (mixerpad, "unlinked",
      G_CALLBACK (gst_videomixer_pad_unlink), (gpointer) mixerpad);

  /* setup some pad functions */
  gst_pad_set_link_function (GST_PAD (mixerpad),
      gst_videomixer_pad_sinkconnect);
  gst_pad_set_event_mask_function (GST_PAD (mixerpad),
      gst_videomixer_pad_get_sink_event_masks);

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
    {VIDEO_MIXER_BACKGROUND_CHECKER, "0", "Checker pattern"},
    {VIDEO_MIXER_BACKGROUND_BLACK, "1", "Black"},
    {VIDEO_MIXER_BACKGROUND_WHITE, "2", "White"},
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
        "format = (fourcc) I420,"
        "width = (int) [ 16, 4096 ],"
        "height = (int) [ 16, 4096 ]," "framerate = (double) [ 0, max ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-yuv,"
        "format = (fourcc) AYUV,"
        "width = (int) [ 16, 4096 ],"
        "height = (int) [ 16, 4096 ]," "framerate = (double) [ 0, max ]")
    );

static void gst_videomixer_base_init (gpointer g_class);
static void gst_videomixer_class_init (GstVideoMixerClass * klass);
static void gst_videomixer_init (GstVideoMixer * videomixer);

static void gst_videomixer_loop (GstElement * element);
static gboolean gst_videomixer_handle_src_event (GstPad * pad,
    GstEvent * event);
static GstPad *gst_videomixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_videomixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videomixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_videomixer_change_state (GstElement * element);

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

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_videomixer_request_new_pad;

  gstelement_class->change_state = gst_videomixer_change_state;

  gstelement_class->get_property = gst_videomixer_get_property;
  gstelement_class->set_property = gst_videomixer_set_property;
}

static void
gst_videomixer_init (GstVideoMixer * mix)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mix);

  mix->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (mix->srcpad, gst_videomixer_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (mix), mix->srcpad);

  GST_FLAG_SET (GST_ELEMENT (mix), GST_ELEMENT_EVENT_AWARE);

  mix->sinkpads = NULL;
  mix->background = DEFAULT_BACKGROUND;
  mix->in_width = 0;
  mix->in_height = 0;
  mix->out_width = 0;
  mix->out_height = 0;

  gst_element_set_loop_function (GST_ELEMENT (mix), gst_videomixer_loop);
}

static GstPad *
gst_videomixer_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstVideoMixer *mix;
  GstPad *newpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("videomixer: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (element), NULL);

  mix = GST_VIDEO_MIXER (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%d")) {
    gchar *name;
    GstVideoMixerPad *mixpad;

    /* create new pad with the name */
    name = g_strdup_printf ("sink_%02d", mix->numpads);
    newpad =
        gst_pad_custom_new_from_template (GST_TYPE_VIDEO_MIXER_PAD, templ,
        name);
    g_free (name);

    mixpad = GST_VIDEO_MIXER_PAD (newpad);

    mixpad->zorder = mix->numpads;
    mix->numpads++;
    mix->sinkpads = g_slist_append (mix->sinkpads, newpad);
  } else {
    g_warning ("videomixer: this is not our template!\n");
    return NULL;
  }

  /* dd the pad to the element */
  gst_element_add_pad (element, newpad);

  return newpad;
}

/* handle events */
static gboolean
gst_videomixer_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoMixer *mix;
  GstEventType type;

  mix = GST_VIDEO_MIXER (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      /* disable seeking for now */
      return FALSE;
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

#define BLEND_NORMAL(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;		\
	U = ((U1*(255-alpha))+(U2*alpha))>>8;		\
	V = ((V1*(255-alpha))+(V2*alpha))>>8;

#define BLEND_ADD(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  		\
	Y = Y1+((Y2*alpha)>>8);					\
	U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;		\
	V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;		\
        if (Y>255) {						\
	  gint mult = MAX (0, 288-Y);				\
	  U = ((U*mult) + (127*(32-mult)))>>5;			\
	  V = ((V*mult) + (127*(32-mult)))>>5;			\
	  Y = 255;						\
	}							\
	U = MIN (U,255);					\
	V = MIN (V,255);

#define BLEND_SUBTRACT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  		\
	Y = Y1-((Y2*alpha)>>8);					\
	U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;		\
	V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;		\
        if (Y<0) {						\
	  gint mult = MIN (32, -Y);				\
	  U = ((U*(32-mult)) + (127*mult))>>5;			\
	  V = ((V*(32-mult)) + (127*mult))>>5;			\
	  Y = 0;						\
	}

#define BLEND_DARKEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	if (Y1 < Y2) {					\
	  Y = Y1; U = U1; V = V1;			\
	}						\
	else {						\
	  Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;		\
	  U = ((U1*(255-alpha))+(U2*alpha))>>8;		\
	  V = ((V1*(255-alpha))+(V2*alpha))>>8;		\
	}

#define BLEND_LIGHTEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	if (Y1 > Y2) {					\
	  Y = Y1; U = U1; V = V1;			\
	}						\
	else {						\
	  Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;		\
	  U = ((U1*(255-alpha))+(U2*alpha))>>8;		\
	  V = ((V1*(255-alpha))+(V2*alpha))>>8;		\
	}

#define BLEND_MULTIPLY(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  			\
	Y = (Y1*(256*(255-alpha) +(Y2*alpha)))>>16;			\
	U = ((U1*(255-alpha)*256)+(alpha*(U1*Y2+128*(256-Y2))))>>16;	\
	V = ((V1*(255-alpha)*256)+(alpha*(V1*Y2+128*(256-Y2))))>>16;

#define BLEND_DIFFERENCE(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	Y = ABS((gint)Y1-(gint)Y2)+127;				\
	U = ABS((gint)U1-(gint)U2)+127;				\
	V = ABS((gint)V1-(gint)V2)+127;				\
	Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;			\
	U = ((U*alpha)+(U1*(255-alpha)))>>8;			\
	V = ((V*alpha)+(V1*(255-alpha)))>>8;			\
        if (Y>255) {						\
	  gint mult = MAX (0, 288-Y);				\
	  U = ((U*mult) + (127*(32-mult)))>>5;			\
	  V = ((V*mult) + (127*(32-mult)))>>5;			\
	  Y = 255;						\
	} else if (Y<0) {					\
	  gint mult = MIN (32, -Y);				\
	  U = ((U*(32-mult)) + (127*mult))>>5;			\
	  V = ((V*(32-mult)) + (127*mult))>>5;			\
	  Y = 0;						\
	}							\
        U = CLAMP(U, 0, 255);					\
        V = CLAMP(V, 0, 255);

#define BLEND_EXCLUSION(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	Y = ((gint)(Y1^0xff)*Y2+(gint)(Y2^0xff)*Y1)>>8;		\
	U = ((gint)(U1^0xff)*Y2+(gint)(Y2^0xff)*U1)>>8;		\
	V = ((gint)(V1^0xff)*Y2+(gint)(Y2^0xff)*V1)>>8;		\
	Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;			\
	U = ((U*alpha)+(U1*(255-alpha)))>>8;			\
	V = ((V*alpha)+(V1*(255-alpha)))>>8;			\
        if (Y>255) {						\
	  gint mult = MAX (0, 288-Y);				\
	  U = ((U*mult) + (127*(32-mult)))>>5;			\
	  V = ((V*mult) + (127*(32-mult)))>>5;			\
	  Y = 255;						\
	} else if (Y<0) {					\
	  gint mult = MIN (32, -Y);				\
	  U = ((U*(32-mult)) + (127*mult))>>5;			\
	  V = ((V*(32-mult)) + (127*mult))>>5;			\
	  Y = 0;						\
	}							\
        U = CLAMP(U, 0, 255);					\
        V = CLAMP(V, 0, 255);

#define BLEND_SOFTLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	Y = (gint)Y1+(gint)Y2 - 127;				\
	U = (gint)U1+(gint)U2 - 127;				\
	V = (gint)V1+(gint)V2 - 127;				\
	Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;			\
	U = ((U*alpha)+(U1*(255-alpha)))>>8;			\
	V = ((V*alpha)+(V1*(255-alpha)))>>8;			\
        if (Y>255) {						\
	  gint mult = MAX (0, 288-Y);				\
	  U = ((U*mult) + (127*(32-mult)))>>5;			\
	  V = ((V*mult) + (127*(32-mult)))>>5;			\
	  Y = 255;						\
	} else if (Y<0) {					\
	  gint mult = MIN (32, -Y);				\
	  U = ((U*(32-mult)) + (127*mult))>>5;			\
	  V = ((V*(32-mult)) + (127*mult))>>5;			\
	  Y = 0;						\
	}							\

#define BLEND_HARDLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)  	\
	Y = (gint)Y1+(gint)Y2*2 - 255;				\
	U = (gint)U1+(gint)U2 - 127;				\
	V = (gint)V1+(gint)V2 - 127;				\
	Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;			\
	U = ((U*alpha)+(U1*(255-alpha)))>>8;			\
	V = ((V*alpha)+(V1*(255-alpha)))>>8;			\
        if (Y>255) {						\
	  gint mult = MAX (0, 288-Y);				\
	  U = ((U*mult) + (127*(32-mult)))>>5;			\
	  V = ((V*mult) + (127*(32-mult)))>>5;			\
	  Y = 255;						\
	} else if (Y<0) {					\
	  gint mult = MIN (32, -Y);				\
	  U = ((U*(32-mult)) + (127*mult))>>5;			\
	  V = ((V*(32-mult)) + (127*mult))>>5;			\
	  Y = 0;						\
	}							\

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

/* note that this function does packing conversion and blending at the
 * same time */
static void
gst_videomixer_blend_ayuv_i420 (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  gint dest_size;
  gint alpha, b_alpha;
  guint8 *destY1, *destY2, *destU, *destV;
  gint accumU;
  gint accumV;
  gint i, j;
  gint src_stride;
  gint src_add, destY_add, destC_add;
  guint8 *src1, *src2;
  gint Y, U, V;

  src_stride = src_width * 4;
  dest_size = dest_width * dest_height;

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

  src_add = 2 * src_stride - (4 * src_width);
  destY_add = 2 * dest_width - (src_width);
  destC_add = dest_width / 2 - (src_width / 2);

  destY1 = dest + xpos + (ypos * dest_width);
  destY2 = destY1 + dest_width;
  destU = dest + dest_size + xpos / 2 + (ypos / 2 * dest_width / 2);
  destV = destU + dest_size / 4;

  src1 = src;
  src2 = src + src_stride;

  /* we convert a square of 2x2 samples to generate 4 Luma and 2 chroma samples */
  for (i = 0; i < src_height / 2; i++) {
    for (j = 0; j < src_width / 2; j++) {
      alpha = (src1[0] * b_alpha) >> 8;
      BLEND_MODE (destY1[0], destU[0], destV[0], src1[1], src1[2], src1[3],
          alpha, Y, U, V);
      destY1[0] = Y;
      accumU = U;
      accumV = V;
      alpha = (src1[4] * b_alpha) >> 8;
      BLEND_MODE (destY1[1], destU[0], destV[0], src1[5], src1[6], src1[7],
          alpha, Y, U, V);
      destY1[1] = Y;
      accumU += U;
      accumV += V;
      alpha = (src2[0] * b_alpha) >> 8;
      BLEND_MODE (destY2[0], destU[0], destV[0], src2[1], src2[2], src2[3],
          alpha, Y, U, V);
      destY2[0] = Y;
      accumU += U;
      accumV += V;
      alpha = (src2[4] * b_alpha) >> 8;
      BLEND_MODE (destY2[1], destU[0], destV[0], src2[5], src2[6], src2[7],
          alpha, Y, U, V);
      destY2[1] = Y;
      accumU += U;
      accumV += V;

      /* take the average of the 4 chroma samples to get the final value */
      destU[0] = accumU / 4;
      destV[0] = accumV / 4;

      src1 += 8;
      src2 += 8;
      destY1 += 2;
      destY2 += 2;
      destU += 1;
      destV += 1;
    }
    src1 += src_add;
    src2 += src_add;
    destY1 += destY_add;
    destY2 += destY_add;
    destU += destC_add;
    destV += destC_add;
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
  gint size = width * height;
  gint i, j;
  static int tab[] = { 80, 160, 80, 160 };

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];
    }
  }
  memset (dest, 128, size / 2);
}

static void
gst_videomixer_fill_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  gint size = width * height;

  memset (dest, colY, size);
  memset (dest + size, colU, size / 4);
  memset (dest + size + size / 4, colV, size / 4);
}

/* try to get a buffer on all pads. As long as the queued value is
 * negative, we skip buffers */
static gboolean
gst_videomixer_fill_queues (GstVideoMixer * mix)
{
  GSList *walk;
  gboolean eos = TRUE;

  /* loop over all pads and fill it with a buffer */
  walk = mix->sinkpads;
  while (walk) {
    GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (walk->data);

    walk = g_slist_next (walk);

    GST_DEBUG ("looking at pad %s", gst_pad_get_name (GST_PAD (pad)));

    /* don't care about eos pads */
    if (pad->eos) {
      GST_DEBUG ("pad %s in eos, skipping", gst_pad_get_name (GST_PAD (pad)));
      continue;
    }

    GST_DEBUG ("pad %s: buffer %p, queued %lld  ",
        gst_pad_get_name (GST_PAD (pad)), pad->buffer, pad->queued);

    /* this pad is in need of a new buffer */
    if (pad->buffer == NULL) {
      GstData *data;
      GstBuffer *buffer;

      /* as long as not enough buffers have been queued */
      while (pad->queued <= 0 && !pad->eos) {
        data = gst_pad_pull (GST_PAD (pad));
        if (GST_IS_EVENT (data)) {
          GstEvent *event = GST_EVENT (data);

          switch (GST_EVENT_TYPE (event)) {
            case GST_EVENT_EOS:
              GST_DEBUG ("videomixer: EOS on pad %s",
                  gst_pad_get_name (GST_PAD (pad)));
              /* mark pad eos */
              pad->eos = TRUE;
              gst_event_unref (event);
              break;
            default:
              gst_pad_event_default (GST_PAD (pad), GST_EVENT (data));
          }
        } else {
          guint64 duration;

          buffer = GST_BUFFER (data);
          duration = GST_BUFFER_DURATION (buffer);
          /* no duration on the buffer, use the framerate */
          if (duration == -1) {
            if (pad->in_framerate == 0.0) {
              duration = G_MAXINT64;
            } else {
              duration = GST_SECOND / pad->in_framerate;
            }
          }
          pad->queued += duration;
          /* this buffer will need to be mixed */
          if (pad->queued > 0) {
            pad->buffer = buffer;
          } else {
            /* skip buffer, it's too old */
            gst_buffer_unref (buffer);
          }
        }
        GST_DEBUG ("pad %s: in loop, buffer %p, queued %lld  ",
            gst_pad_get_name (GST_PAD (pad)), pad->buffer, pad->queued);
      }
    }
    if (pad->buffer != NULL) {
      /* got a buffer somewhere so were not eos */
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
  while (walk) {
    GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (walk->data);

    walk = g_slist_next (walk);

    if (pad->buffer != NULL) {
      gst_videomixer_blend_ayuv_i420 (GST_BUFFER_DATA (pad->buffer),
          pad->xpos, pad->ypos,
          pad->in_width, pad->in_height,
          pad->alpha,
          GST_BUFFER_DATA (outbuf), mix->out_width, mix->out_height);
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
    if (mix->in_framerate == 0.0) {
      interval = G_MAXINT64;
    } else {
      interval = GST_SECOND / mix->in_framerate;
    }
  }

  walk = mix->sinkpads;
  while (walk) {
    GstVideoMixerPad *pad = GST_VIDEO_MIXER_PAD (walk->data);

    walk = g_slist_next (walk);

    if (pad->buffer != NULL) {
      pad->queued -= interval;
      GST_DEBUG ("queued now %s %lld", gst_pad_get_name (GST_PAD (pad)),
          pad->queued);
      if (pad->queued <= 0) {
        gst_buffer_unref (pad->buffer);
        pad->buffer = NULL;
      }
    }
  }
}

/*
 * The basic idea is to get a buffer on all pads and mix them together.
 * Based on the framerate, buffers are removed from the queues to make room
 * for a new buffer. 
 */
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

    newcaps =
        gst_caps_copy (gst_pad_get_negotiated_caps (GST_PAD (mix->master)));
    gst_caps_set_simple (newcaps, "format", GST_TYPE_FOURCC,
        GST_STR_FOURCC ("I420"), "width", G_TYPE_INT, new_width, "height",
        G_TYPE_INT, new_height, NULL);

    if (GST_PAD_LINK_FAILED (gst_pad_try_set_caps (mix->srcpad, newcaps))) {
      GST_ELEMENT_ERROR (mix, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }

    mix->out_width = new_width;
    mix->out_height = new_height;
  }

  outsize = 3 * (mix->out_width * mix->out_height) / 2;
  outbuf = gst_pad_alloc_buffer (mix->srcpad, GST_BUFFER_OFFSET_NONE, outsize);
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

static GstElementStateReturn
gst_videomixer_change_state (GstElement * element)
{
  GstVideoMixer *mix;
  gint transition = GST_STATE_TRANSITION (element);

  g_return_val_if_fail (GST_IS_VIDEO_MIXER (element), GST_STATE_FAILURE);

  mix = GST_VIDEO_MIXER (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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
    "Video mixer", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
