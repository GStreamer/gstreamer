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
#include "gstvideoscale.h"
#include "videoscale.h"


/* debug variable definition */
GST_DEBUG_CATEGORY (videoscale_debug);

/* elementfactory information */
static GstElementDetails videoscale_details =
GST_ELEMENT_DETAILS ("Video scaler",
    "Filter/Effect/Video",
    "Resizes video",
    "Wim Taymans <wim.taymans@chello.be>");

/* GstVideoscale signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METHOD
      /* FILL ME */
};

#define GST_TYPE_VIDEOSCALE_METHOD (gst_videoscale_method_get_type())
static GType
gst_videoscale_method_get_type (void)
{
  static GType videoscale_method_type = 0;
  static GEnumValue videoscale_methods[] = {
    {GST_VIDEOSCALE_POINT_SAMPLE, "0", "Point Sample (not implemented)"},
    {GST_VIDEOSCALE_NEAREST, "1", "Nearest"},
    {GST_VIDEOSCALE_BILINEAR, "2", "Bilinear (not implemented)"},
    {GST_VIDEOSCALE_BICUBIC, "3", "Bicubic (not implemented)"},
    {0, NULL, NULL},
  };

  if (!videoscale_method_type) {
    videoscale_method_type =
        g_enum_register_static ("GstVideoscaleMethod", videoscale_methods);
  }
  return videoscale_method_type;
}

static GstCaps *
gst_videoscale_get_capslist (void)
{
  GstCaps *caps;
  int i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < videoscale_n_formats; i++) {
    gst_caps_append_structure (caps,
        videoscale_get_structure (videoscale_formats + i));
  }

  return caps;
}

static GstPadTemplate *
gst_videoscale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_videoscale_get_capslist ());
}

static GstPadTemplate *
gst_videoscale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_videoscale_get_capslist ());
}

static void gst_videoscale_base_init (gpointer g_class);
static void gst_videoscale_class_init (GstVideoscaleClass * klass);
static void gst_videoscale_init (GstVideoscale * videoscale);
static gboolean gst_videoscale_handle_src_event (GstPad * pad,
    GstEvent * event);

static void gst_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videoscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_videoscale_finalize (GObject * object);

static void gst_videoscale_chain (GstPad * pad, GstData * _data);
static GstCaps *gst_videoscale_get_capslist (void);

static GstElementClass *parent_class = NULL;

/*static guint gst_videoscale_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_videoscale_get_type (void)
{
  static GType videoscale_type = 0;

  if (!videoscale_type) {
    static const GTypeInfo videoscale_info = {
      sizeof (GstVideoscaleClass),
      gst_videoscale_base_init,
      NULL,
      (GClassInitFunc) gst_videoscale_class_init,
      NULL,
      NULL,
      sizeof (GstVideoscale),
      0,
      (GInstanceInitFunc) gst_videoscale_init,
    };

    videoscale_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstVideoscale",
        &videoscale_info, 0);
  }
  return videoscale_type;
}

static void
gst_videoscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videoscale_details);

  gst_element_class_add_pad_template (element_class,
      gst_videoscale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_videoscale_src_template_factory ());
}
static void
gst_videoscale_class_init (GstVideoscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD, g_param_spec_enum ("method", "method", "method", GST_TYPE_VIDEOSCALE_METHOD, 0, G_PARAM_READWRITE));     /* CHECKME! */

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_videoscale_finalize;
  gobject_class->set_property = gst_videoscale_set_property;
  gobject_class->get_property = gst_videoscale_get_property;

}

static GstCaps *
gst_videoscale_getcaps (GstPad * pad)
{
  GstVideoscale *videoscale;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;
  int i;

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == videoscale->srcpad) ? videoscale->sinkpad :
      videoscale->srcpad;
  othercaps = gst_pad_get_allowed_caps (otherpad);

  GST_DEBUG_OBJECT (pad, "othercaps of otherpad %s:%s are: %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (otherpad), othercaps);

  caps = gst_caps_copy (othercaps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 16, G_MAXINT, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  GST_DEBUG_OBJECT (pad, "returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}


static GstPadLinkReturn
gst_videoscale_link (GstPad * pad, const GstCaps * caps)
{
  GstVideoscale *videoscale;
  GstPadLinkReturn ret;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstStructure *otherstructure;
  GstStructure *structure;
  struct videoscale_format_struct *format;
  int height = 0, width = 0;
  const GValue *par = NULL;
  const GValue *otherpar;

  GST_DEBUG_OBJECT (pad, "_link with caps %" GST_PTR_FORMAT, caps);
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == videoscale->srcpad) ? videoscale->sinkpad :
      videoscale->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  format = videoscale_find_by_structure (structure);

  if (!ret || format == NULL)
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG_OBJECT (videoscale,
      "trying to set caps %" GST_PTR_FORMAT " on pad %s:%s for passthru",
      caps, GST_DEBUG_PAD_NAME (otherpad));

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (ret == GST_PAD_LINK_OK) {
    /* cool, we can use passthru */
    GST_DEBUG_OBJECT (videoscale, "passthru works");

    videoscale->format = format;
    videoscale->to_width = width;
    videoscale->to_height = height;
    videoscale->from_width = width;
    videoscale->from_height = height;

    goto beach;
  }

  /* no passthru, so try to convert */
  GST_DEBUG_OBJECT (videoscale, "no passthru");

  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *newcaps = gst_caps_copy (caps);

    GST_DEBUG_OBJECT (videoscale, "otherpad %s:%s is negotiated",
        GST_DEBUG_PAD_NAME (otherpad));

    if (pad == videoscale->srcpad) {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, videoscale->from_width,
          "height", G_TYPE_INT, videoscale->from_height, NULL);
      if (videoscale->from_par) {
        GST_DEBUG_OBJECT (videoscale, "from par %d/%d",
            gst_value_get_fraction_numerator (videoscale->from_par),
            gst_value_get_fraction_denominator (videoscale->from_par));
        gst_structure_set (gst_caps_get_structure (newcaps, 0),
            "pixel-aspect-ratio", GST_TYPE_FRACTION,
            gst_value_get_fraction_numerator (videoscale->from_par),
            gst_value_get_fraction_denominator (videoscale->from_par), NULL);
      }
    } else {
      GST_DEBUG_OBJECT (videoscale, "negotiating, checking PAR's");
      /* sink pad is being negotiated, so if there is a PAR,
       * convert w/h/PAR to new src
       * values and try to set them */
      if (par && videoscale->to_par) {
        GValue to_ratio = { 0, };
        gint from_par_n, from_par_d;
        gint to_par_n, to_par_d;
        gint num, den;

        GST_DEBUG_OBJECT (videoscale, "both pads have PAR, calculating");
        from_par_n = gst_value_get_fraction_numerator (par);
        from_par_d = gst_value_get_fraction_denominator (par);
        to_par_n = gst_value_get_fraction_numerator (videoscale->to_par);
        to_par_d = gst_value_get_fraction_denominator (videoscale->to_par);
        g_value_init (&to_ratio, GST_TYPE_FRACTION);
        gst_value_set_fraction (&to_ratio,
            width * from_par_n * to_par_d, height * from_par_d * to_par_n);
        num = gst_value_get_fraction_numerator (&to_ratio);
        den = gst_value_get_fraction_denominator (&to_ratio);
        GST_DEBUG_OBJECT (videoscale, "need to scale to a ratio of %d/%d",
            num, den);
        if (height % den == 0) {
          GST_DEBUG_OBJECT (videoscale, "keeping height %d constant", height);
          videoscale->to_width = height * num / den;
          videoscale->to_height = height;
        } else if (width % num == 0) {
          GST_DEBUG_OBJECT (videoscale, "keeping width %d constant", height);
          videoscale->to_width = width;
          videoscale->to_height = width * den / num;
        } else {
          /* approximate keeping height constant */
          GST_DEBUG_OBJECT (videoscale,
              "approximating while keeping height %d constant", height);
          videoscale->to_width = height * num / den;
          videoscale->to_height = height;
        }
      }

      GST_DEBUG_OBJECT (videoscale, "scaling to %dx%d", videoscale->to_width,
          videoscale->to_height);
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, videoscale->to_width,
          "height", G_TYPE_INT, videoscale->to_height, NULL);
      if (videoscale->to_par) {
        GST_DEBUG_OBJECT (videoscale, "to par %d/%d",
            gst_value_get_fraction_numerator (videoscale->to_par),
            gst_value_get_fraction_denominator (videoscale->to_par));
        gst_structure_set (gst_caps_get_structure (newcaps, 0),
            "pixel-aspect-ratio", GST_TYPE_FRACTION,
            gst_value_get_fraction_numerator (videoscale->to_par),
            gst_value_get_fraction_denominator (videoscale->to_par), NULL);
      }
    }
    GST_DEBUG_OBJECT (videoscale, "trying to set caps %" GST_PTR_FORMAT
        " on pad %s:%s", newcaps, GST_DEBUG_PAD_NAME (otherpad));
    ret = gst_pad_try_set_caps (otherpad, newcaps);
    if (GST_PAD_LINK_FAILED (ret)) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  videoscale->passthru = FALSE;
  GST_DEBUG_OBJECT (videoscale,
      "no passthru, otherpad %s:%s is not negotiated",
      GST_DEBUG_PAD_NAME (otherpad));

beach:
  /* since we're accepting these caps on this pad, we can store the
   * values we're using in conversion, like from/to w,h,PAR */
  othercaps = gst_pad_get_caps (otherpad);
  otherstructure = gst_caps_get_structure (othercaps, 0);
  structure = gst_caps_get_structure (caps, 0);

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  otherpar = gst_structure_get_value (otherstructure, "pixel-aspect-ratio");
  GST_DEBUG ("othercaps: %" GST_PTR_FORMAT, othercaps);
  if (par && otherpar) {
    GST_DEBUG_OBJECT (videoscale, "par %d/%d, otherpar %d/%d",
        gst_value_get_fraction_numerator (par),
        gst_value_get_fraction_denominator (par),
        gst_value_get_fraction_numerator (otherpar),
        gst_value_get_fraction_denominator (otherpar));
  }
  if (pad == videoscale->srcpad) {
    videoscale->to_width = width;
    videoscale->to_height = height;
    if (par) {
      g_free (videoscale->to_par);
      videoscale->to_par = g_new0 (GValue, 1);
      gst_value_init_and_copy (videoscale->to_par, par);
    }
  } else {
    videoscale->from_width = width;
    videoscale->from_height = height;
    if (par) {
      g_free (videoscale->from_par);
      videoscale->from_par = g_new0 (GValue, 1);
      gst_value_init_and_copy (videoscale->from_par, par);
    }
  }
  videoscale->format = format;

  if (gst_pad_is_negotiated (otherpad)) {
    gst_videoscale_setup (videoscale);
  }
  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_videoscale_src_fixate (GstPad * pad, const GstCaps * caps)
{
  GstVideoscale *videoscale;
  GstCaps *newcaps;
  int i;
  gboolean ret = TRUE;

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "asked to fixate caps %" GST_PTR_FORMAT, caps);

  /* don't mess with fixation if we don't have a sink pad PAR */
  if (!videoscale->from_par) {
    GST_DEBUG_OBJECT (videoscale, "no PAR to scale from, not fixating");
    return NULL;
  }

  /* for each structure, if it contains a pixel aspect ratio,
   * fix width and height */

  newcaps = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (newcaps); i++) {
    const GValue *to_par;

    GstStructure *structure = gst_caps_get_structure (newcaps, i);

    to_par = gst_structure_get_value (structure, "pixel-aspect-ratio");
    if (to_par) {
      GValue to_ratio = { 0, }; /* w/h of output video */
      int from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
      int count = 0;

      int w = 0, h = 0;
      int num, den;

      /* if both width and height are already fixed, we can't do anything
       * about it anymore */
      if (gst_structure_get_int (structure, "width", &w))
        ++count;
      if (gst_structure_get_int (structure, "height", &h))
        ++count;
      if (count == 2) {
        GST_DEBUG_OBJECT (videoscale,
            "dimensions already set to %dx%d, not fixating", w, h);
        return NULL;
      }

      from_w = videoscale->from_width;
      from_h = videoscale->from_height;
      from_par_n = gst_value_get_fraction_numerator (videoscale->from_par);
      from_par_d = gst_value_get_fraction_denominator (videoscale->from_par);
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      g_value_init (&to_ratio, GST_TYPE_FRACTION);
      gst_value_set_fraction (&to_ratio, from_w * from_par_n * to_par_d,
          from_h * from_par_d * to_par_n);
      num = gst_value_get_fraction_numerator (&to_ratio);
      den = gst_value_get_fraction_denominator (&to_ratio);
      GST_DEBUG_OBJECT (videoscale,
          "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
          from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
      GST_DEBUG_OBJECT (videoscale,
          "resulting output should respect ratio of %d/%d", num, den);

      /* now find a width x height that respects this display ratio.
       * prefer those that have one of w/h the same as the incoming video
       * using wd / hd = num / den */

      /* start with same height, because of interlaced video */
      /* check hd / den is an integer scale factor, and scale wd with the PAR */
      if (from_h % den == 0) {
        GST_DEBUG_OBJECT (videoscale, "keeping video height");
        h = from_h;
        w = h * num / den;
      } else if (from_w % num == 0) {
        GST_DEBUG_OBJECT (videoscale, "keeping video width");
        w = from_w;
        h = w * den / num;
      } else {
        GST_DEBUG_OBJECT (videoscale, "approximating but keeping video height");
        h = from_h;
        w = h * num / den;
      }
      GST_DEBUG_OBJECT (videoscale, "scaling to %dx%d", w, h);

      /* now fixate */
      ret &=
          gst_caps_structure_fixate_field_nearest_int (structure, "width", w);
      ret &=
          gst_caps_structure_fixate_field_nearest_int (structure, "height", h);
    }
  }

  if (ret)
    return newcaps;

  gst_caps_free (newcaps);
  return NULL;
}

static void
gst_videoscale_init (GstVideoscale * videoscale)
{
  GST_DEBUG_OBJECT (videoscale, "_init");
  videoscale->sinkpad =
      gst_pad_new_from_template (gst_videoscale_sink_template_factory (),
      "sink");
  gst_element_add_pad (GST_ELEMENT (videoscale), videoscale->sinkpad);
  gst_pad_set_chain_function (videoscale->sinkpad, gst_videoscale_chain);
  gst_pad_set_link_function (videoscale->sinkpad, gst_videoscale_link);
  gst_pad_set_getcaps_function (videoscale->sinkpad, gst_videoscale_getcaps);

  videoscale->srcpad =
      gst_pad_new_from_template (gst_videoscale_src_template_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (videoscale), videoscale->srcpad);
  gst_pad_set_event_function (videoscale->srcpad,
      gst_videoscale_handle_src_event);
  gst_pad_set_link_function (videoscale->srcpad, gst_videoscale_link);
  gst_pad_set_getcaps_function (videoscale->srcpad, gst_videoscale_getcaps);
  gst_pad_set_fixate_function (videoscale->srcpad, gst_videoscale_src_fixate);

  videoscale->inited = FALSE;

  videoscale->method = GST_VIDEOSCALE_NEAREST;
  /*videoscale->method = GST_VIDEOSCALE_BILINEAR; */
  /*videoscale->method = GST_VIDEOSCALE_POINT_SAMPLE; */
}

static gboolean
gst_videoscale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoscale *videoscale;
  double a;
  GstStructure *structure;
  GstEvent *new_event;

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      structure = gst_structure_copy (event->event_data.structure.structure);
      if (gst_structure_get_double (event->event_data.structure.structure,
              "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (event->event_data.structure.structure,
              "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
      }
      gst_event_unref (event);
      new_event = gst_event_new (GST_EVENT_NAVIGATION);
      GST_DEBUG_OBJECT (videoscale, "creating new NAVIGATION event %p",
          new_event);
      new_event->event_data.structure.structure = structure;
      return gst_pad_event_default (pad, new_event);
      break;
    default:
      GST_DEBUG_OBJECT (videoscale, "passing on non-NAVIGATION event %p",
          event);
      return gst_pad_event_default (pad, event);
      break;
  }
}

static void
gst_videoscale_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideoscale *videoscale;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;


  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));
  g_return_if_fail (videoscale->inited);

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (videoscale->passthru) {
    GST_LOG_OBJECT (videoscale, "passing through buffer of %ld bytes", size);
    gst_pad_push (videoscale->srcpad, GST_DATA (buf));
    return;
  }

  GST_LOG_OBJECT (videoscale,
      "buffersize=%ld from=%dx%d to=%dx%d fromsize=%ld tosize=%ld",
      size, videoscale->from_width, videoscale->from_height,
      videoscale->to_width, videoscale->to_height,
      videoscale->from_buf_size, videoscale->to_buf_size);

  g_return_if_fail (size == videoscale->from_buf_size);

  outbuf = gst_pad_alloc_buffer (videoscale->srcpad,
      GST_BUFFER_OFFSET_NONE, videoscale->to_buf_size);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  g_return_if_fail (videoscale->format);
  GST_LOG_OBJECT (videoscale, "format " GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (videoscale->format->fourcc));
  g_return_if_fail (videoscale->format->scale);

  videoscale->format->scale (videoscale, GST_BUFFER_DATA (outbuf), data);

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (outbuf));

  gst_pad_push (videoscale->srcpad, GST_DATA (outbuf));

  gst_buffer_unref (buf);
}

static void
gst_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOSCALE (object));
  src = GST_VIDEOSCALE (object);

  GST_DEBUG_OBJECT (src, "gst_videoscale_set_property");
  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      break;
  }
}

static void
gst_videoscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOSCALE (object));
  src = GST_VIDEOSCALE (object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* maybe these free's should go in a state change instead;
 * in that case, we'd probably also want to clear from/to width/height there. */
static void
gst_videoscale_finalize (GObject * object)
{
  GstVideoscale *videoscale;

  videoscale = GST_VIDEOSCALE (object);

  g_free (videoscale->from_par);
  g_free (videoscale->to_par);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEOSCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (videoscale_debug, "videoscale", 0,
      "videoscale element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscale",
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
