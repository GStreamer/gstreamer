/*
    Copyright (C) 2005 Edward Hervey (edward@fluendo.com)
    Copyright (C) 2006 Mark Nauwelaerts (manauw@skynet.be)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#ifdef HAVE_ORC
#include <orc/orc.h>
#endif

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#include <postprocess.h>
#else
#include <libavcodec/avcodec.h>
#include <libpostproc/postprocess.h>
#endif


typedef struct _PostProcDetails PostProcDetails;

struct _PostProcDetails
{
  const char *shortname;
  const char *longname;
  const char *description;
};

static const PostProcDetails filterdetails[] = {
  {"hb", "hdeblock", "horizontal deblocking filter"},
  {"vb", "vdeblock", "vertical deblocking filter"},
  {"h1", "x1hdeblock", "experimental horizontal deblocking filter 1"},
  {"v1", "x1vdeblock", "experimental vertical deblocking filter 1"},
  {"ha", "ahdeblock", "another horizontal deblocking filter"},
  {"va", "avdeblock", "another vertical deblocking filter"},
  {"dr", "dering", "deringing filter"},
  {"al", "autolevels", "automatic brightness/contrast filter"},
  {"lb", "linblenddeint", "linear blend interpolater"},
  {"li", "linipoldeint", "linear interpolation deinterlacer"},
  {"ci", "cubicipoldeint", "cubic interpolation deinterlacer"},
  {"md", "mediandeint", "median deinterlacer"},
  {"fd", "ffmpegdeint", "ffmpeg deinterlacer"},
  {"l5", "lowpass5", "FIR lowpass deinterlacer"},
  {"tn", "tmpnoise", "temporal noise reducer"},
  {"fq", "forcequant", "force quantizer"},
  {"de", "default", "default filters"},
  {NULL, NULL, NULL}
};

typedef struct _GstPostProc GstPostProc;

struct _GstPostProc
{
  GstVideoFilter element;

  GstPad *sinkpad, *srcpad;
  guint quality;
  gint width, height;

  pp_mode *mode;
  pp_context *context;

  /* props of various filters */
  gboolean autoq;
  guint scope;
  /* though not all needed at once,
   * this avoids union or ugly re-use for simplicity */
  gint diff, flat;
  gint t1, t2, t3;
  gboolean range;
  gint quant;

  /* argument string for pp */
  gchar *cargs, *args;
};

typedef struct _GstPostProcClass GstPostProcClass;

struct _GstPostProcClass
{
  GstVideoFilterClass parent_class;

  gint filterid;
};

/* properties for the various pp filters */
/* common props */
enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_AUTOQ,
  PROP_SCOPE,
  PROP_MAX
};

/* possible filter scopes */
enum
{
  SCOPE_BOTH,
  SCOPE_CHROMA,
  SCOPE_LUMA
};

#define DEFAULT_QUALITY   PP_QUALITY_MAX
#define DEFAULT_AUTOQ     FALSE
#define DEFAULT_SCOPE     SCOPE_BOTH

/* deblocking props */
enum
{
  PROP_DIFF = PROP_MAX,
  PROP_FLAT
};

#define DEFAULT_DIFF    -1
#define DEFAULT_FLAT    -1

/* denoise props */
enum
{
  PROP_T1 = PROP_MAX,
  PROP_T2,
  PROP_T3
};

#define DEFAULT_T1    -1
#define DEFAULT_T2    -1
#define DEFAULT_T3    -1

/* autolevels */
enum
{
  PROP_RANGE = PROP_MAX
};

#define DEFAULT_RANGE FALSE

/* forceq props */
enum
{
  PROP_QUANT = PROP_MAX
};

#define DEFAULT_QUANT  -1


/* hashtable, key = gtype, value = filterdetails index */
static GHashTable *global_plugins;

/* TODO : add support for the other format supported by libpostproc */

static GstStaticPadTemplate gst_post_proc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ IYUV, I420, YV12, Y42B, Y41B }"))
    );

static GstStaticPadTemplate gst_post_proc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ IYUV, I420, YV12, Y42B, Y41B }"))
    );

GST_DEBUG_CATEGORY (postproc_debug);
#define GST_CAT_DEFAULT postproc_debug

static void gst_post_proc_class_init (GstPostProcClass * klass);
static void gst_post_proc_base_init (GstPostProcClass * klass);
static void gst_post_proc_init (GstPostProc * pproc);
static void gst_post_proc_dispose (GObject * object);

static gboolean gst_post_proc_set_info (GstVideoFilter * vfilter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_post_proc_transform_frame_ip (GstVideoFilter * vfilter,
    GstVideoFrame * frame);

/* static GstStateChangeReturn gst_post_proc_change_state (GstElement * element, */
/*     GstStateChange transition); */

static void gst_post_proc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_post_proc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_post_proc_deblock_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_post_proc_deblock_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_post_proc_autolevels_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_post_proc_autolevels_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_post_proc_tmpnoise_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_post_proc_tmpnoise_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_post_proc_forcequant_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_post_proc_forcequant_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

#define GST_TYPE_PP_SCOPE (gst_pp_scope_get_type())
static GType
gst_pp_scope_get_type (void)
{
  static GType pp_scope_type = 0;

  static const GEnumValue pp_scope[] = {
    {0, "Chrominance and Luminance filtering", "both"},
    {1, "Chrominance only filtering", "chroma"},
    {2, "Luminance only filtering", "luma"},
    {0, NULL, NULL},
  };

  if (!pp_scope_type) {
    pp_scope_type = g_enum_register_static ("GstPostProcPPScope", pp_scope);
  }
  return pp_scope_type;
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_ffmpeg_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
  GstDebugLevel gst_level;

  switch (level) {
    case AV_LOG_QUIET:
      gst_level = GST_LEVEL_NONE;
      break;
    case AV_LOG_ERROR:
      gst_level = GST_LEVEL_ERROR;
      break;
    case AV_LOG_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    case AV_LOG_DEBUG:
      gst_level = GST_LEVEL_DEBUG;
      break;
    default:
      gst_level = GST_LEVEL_INFO;
      break;
  }

  gst_debug_log_valist (postproc_debug, gst_level, "", "", 0, NULL, fmt, vl);
}
#endif

#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

static void
change_context (GstPostProc * postproc, gint width, gint height)
{
  guint mmx_flags;
  guint altivec_flags;
  gint ppflags;

  GST_DEBUG_OBJECT (postproc, "change_context, width:%d, height:%d",
      width, height);

  if ((width != postproc->width) && (height != postproc->height)) {
    if (postproc->context)
      pp_free_context (postproc->context);

#ifdef HAVE_ORC
    mmx_flags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
    altivec_flags =
        orc_target_get_default_flags (orc_target_get_by_name ("altivec"));
    ppflags = (mmx_flags & ORC_TARGET_MMX_MMX ? PP_CPU_CAPS_MMX : 0)
        | (mmx_flags & ORC_TARGET_MMX_MMXEXT ? PP_CPU_CAPS_MMX2 : 0)
        | (mmx_flags & ORC_TARGET_MMX_3DNOW ? PP_CPU_CAPS_3DNOW : 0)
        | (altivec_flags & ORC_TARGET_ALTIVEC_ALTIVEC ? PP_CPU_CAPS_ALTIVEC :
        0);
#else
    mmx_flags = 0;
    altivec_flags = 0;
    ppflags = 0;
#endif

    postproc->context = pp_get_context (width, height, PP_FORMAT_420 | ppflags);
    postproc->width = width;
    postproc->height = height;
  }
}

/* append app to *base, and places result in *base */
/* all input strings are free'd */
static void inline
append (gchar ** base, gchar * app)
{
  gchar *res;
  const gchar *sep;

  if (**base && *app)
    sep = ":";
  else
    sep = "";
  res = g_strconcat (*base, sep, app, NULL);
  g_free (*base);
  g_free (app);
  *base = res;
}

static void
change_mode (GstPostProc * postproc)
{
  GstPostProcClass *klass;
  gchar *name;

  klass = (GstPostProcClass *) G_OBJECT_GET_CLASS (G_OBJECT (postproc));

  if (postproc->mode)
    pp_free_mode (postproc->mode);

  name = g_strdup (filterdetails[klass->filterid].shortname);
  append (&name, g_strdup (postproc->cargs));
  append (&name, g_strdup (postproc->args));
  GST_DEBUG_OBJECT (postproc, "requesting pp %s", name);
  postproc->mode = pp_get_mode_by_name_and_quality (name, postproc->quality);
  g_free (name);

  g_assert (postproc->mode);
}

static void
gst_post_proc_base_init (GstPostProcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gint ppidx;
  gchar *longname, *description;

  ppidx = GPOINTER_TO_INT (g_hash_table_lookup (global_plugins,
          GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class))));

  longname = g_strdup_printf ("LibPostProc %s filter",
      filterdetails[ppidx].longname);
  description = g_strdup_printf ("LibPostProc %s",
      filterdetails[ppidx].description);
  gst_element_class_set_details_simple (element_class, longname, "Filter/Video",
      description,
      "Edward Hervey <edward@fluendo.com>, Mark Nauwelaerts (manauw@skynet.be)");
  g_free (longname);
  g_free (description);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_post_proc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_post_proc_sink_template));

  klass->filterid = ppidx;
}

static void
gst_post_proc_class_init (GstPostProcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
/*   GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass); */
  GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS (klass);
  gint ppidx;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_post_proc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_post_proc_get_property);

  /* common props */
  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_uint ("quality", "Quality",
          "Quality level of filter (higher is better)",
          0, PP_QUALITY_MAX, DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTOQ,
      g_param_spec_boolean ("autoq", "AutoQ",
          "Automatically switch filter off if CPU too slow",
          DEFAULT_AUTOQ, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCOPE,
      g_param_spec_enum ("scope", "Scope",
          "Operate on chrominance and/or luminance",
          GST_TYPE_PP_SCOPE, DEFAULT_SCOPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  ppidx = klass->filterid;
  /* per filter props */
  if (g_strrstr (filterdetails[ppidx].longname, "deblock") != NULL &&
      filterdetails[ppidx].longname[0] != 'x') {
    /* deblocking */
    g_object_class_install_property (gobject_class, PROP_DIFF,
        g_param_spec_int ("difference", "Difference Factor",
            "Higher values mean more deblocking (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_DIFF,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_FLAT,
        g_param_spec_int ("flatness", "Flatness Threshold",
            "Lower values mean more deblocking (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_FLAT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_deblock_set_property);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_deblock_get_property);
  } else if (!(g_ascii_strcasecmp (filterdetails[ppidx].shortname, "tn"))) {
    /* tmpnoise */
    g_object_class_install_property (gobject_class, PROP_T1,
        g_param_spec_int ("threshold-1", "Threshold One",
            "Higher values mean stronger filtering (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_T1,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_T2,
        g_param_spec_int ("threshold-2", "Threshold Two",
            "Higher values mean stronger filtering (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_T2,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_T3,
        g_param_spec_int ("threshold-3", "Threshold Three",
            "Higher values mean stronger filtering (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_T3,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_tmpnoise_set_property);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_tmpnoise_get_property);
  } else if (!(g_ascii_strcasecmp (filterdetails[ppidx].shortname, "al"))) {
    /* autolevels */
    g_object_class_install_property (gobject_class, PROP_RANGE,
        g_param_spec_boolean ("fully-range", "Fully Range",
            "Stretch luminance to (0-255)", DEFAULT_RANGE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_autolevels_set_property);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_autolevels_get_property);

  } else if (!(g_ascii_strcasecmp (filterdetails[ppidx].shortname, "fq"))) {
    /* forcequant */
    g_object_class_install_property (gobject_class, PROP_QUANT,
        g_param_spec_int ("quantizer", "Force Quantizer",
            "Quantizer to use (-1 = pp default)",
            -1, G_MAXINT, DEFAULT_QUANT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_forcequant_set_property);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_post_proc_forcequant_get_property);
  }

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_post_proc_dispose);

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_post_proc_set_info);
  vfilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_post_proc_transform_frame_ip);
}

static void
gst_post_proc_init (GstPostProc * postproc)
{
  /* properties */
  postproc->quality = DEFAULT_QUALITY;
  postproc->autoq = DEFAULT_AUTOQ;
  postproc->scope = DEFAULT_SCOPE;
  postproc->diff = DEFAULT_DIFF;
  postproc->flat = DEFAULT_FLAT;
  postproc->quant = DEFAULT_QUANT;
  postproc->t1 = DEFAULT_T1;
  postproc->t2 = DEFAULT_T2;
  postproc->t3 = DEFAULT_T3;
  postproc->range = DEFAULT_RANGE;
  postproc->mode = NULL;
  postproc->cargs = g_strdup ("");
  postproc->args = g_strdup ("");
  change_mode (postproc);

  postproc->context = NULL;
  postproc->width = 0;
  postproc->height = 0;
}

static void
gst_post_proc_dispose (GObject * object)
{
  GstPostProc *postproc = (GstPostProc *) object;

  if (postproc->mode)
    pp_free_mode (postproc->mode);
  if (postproc->context)
    pp_free_context (postproc->context);

  g_free (postproc->cargs);
  postproc->cargs = NULL;
  g_free (postproc->args);
  postproc->args = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_post_proc_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstPostProc *postproc = (GstPostProc *) (vfilter);

  change_context (postproc, in_info->width, in_info->height);

  return TRUE;
}

static GstFlowReturn
gst_post_proc_transform_frame_ip (GstVideoFilter * vfilter,
    GstVideoFrame * frame)
{
  GstPostProc *postproc;
  gint stride[3];
  guint8 *outplane[3];
  guint8 *inplane[3];
  gint width, height;

  /* postprocess the buffer ! */
  postproc = (GstPostProc *) vfilter;

  stride[0] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  stride[1] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 1);
  stride[2] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 2);
  outplane[0] = inplane[0] = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  outplane[1] = inplane[1] = GST_VIDEO_FRAME_COMP_DATA (frame, 1);
  outplane[2] = inplane[2] = GST_VIDEO_FRAME_COMP_DATA (frame, 2);

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  GST_DEBUG_OBJECT (postproc, "calling pp_postprocess, width:%d, height:%d",
      width, height);

  pp_postprocess ((const guint8 **) inplane, stride, outplane, stride,
      width, height, (int8_t *) "", 0, postproc->mode, postproc->context, 0);

  return GST_FLOW_OK;
}


static void
gst_post_proc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;
  gchar *args;

  switch (prop_id) {
    case PROP_QUALITY:
      postproc->quality = g_value_get_uint (value);
      break;
    case PROP_AUTOQ:
      postproc->autoq = g_value_get_boolean (value);
      break;
    case PROP_SCOPE:
      postproc->scope = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  /* construct common args */
  args = postproc->autoq ? g_strdup ("autoq") : g_strdup ("");
  switch (postproc->scope) {
    case SCOPE_BOTH:
      break;
    case SCOPE_CHROMA:
      append (&args, g_strdup ("noluma"));
      break;
    case SCOPE_LUMA:
      append (&args, g_strdup ("nochrom"));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  g_free (postproc->cargs);
  postproc->cargs = args;

  change_mode (postproc);
}

static void
gst_post_proc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_uint (value, postproc->quality);
      break;
    case PROP_AUTOQ:
      g_value_set_boolean (value, postproc->autoq);
      break;
    case PROP_SCOPE:
      g_value_set_enum (value, postproc->scope);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_post_proc_deblock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_DIFF:
      postproc->diff = g_value_get_int (value);
      break;
    case PROP_FLAT:
      postproc->flat = g_value_get_int (value);
      break;
    default:
      gst_post_proc_set_property (object, prop_id, value, pspec);
      break;
  }

  /* construct args */
  g_free (postproc->args);
  if (postproc->diff >= 0) {
    postproc->args = g_strdup_printf ("%d", postproc->diff);
    if (postproc->flat >= 0)
      append (&postproc->args, g_strdup_printf ("%d", postproc->flat));
  } else
    postproc->args = g_strdup ("");
  change_mode (postproc);
}

static void
gst_post_proc_deblock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_DIFF:
      g_value_set_int (value, postproc->diff);
      break;
    case PROP_FLAT:
      g_value_set_int (value, postproc->flat);
      break;
    default:
      gst_post_proc_get_property (object, prop_id, value, pspec);
      break;
  }
}

static void
gst_post_proc_tmpnoise_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_T1:
      postproc->t1 = g_value_get_int (value);
      break;
    case PROP_T2:
      postproc->t2 = g_value_get_int (value);
      break;
    case PROP_T3:
      postproc->t3 = g_value_get_int (value);
      break;
    default:
      gst_post_proc_set_property (object, prop_id, value, pspec);
      break;
  }

  /* construct args */
  g_free (postproc->args);
  if (postproc->t1 >= 0) {
    postproc->args = g_strdup_printf ("%d", postproc->t1);
    if (postproc->t2 >= 0) {
      append (&postproc->args, g_strdup_printf ("%d", postproc->t2));
      if (postproc->t3 >= 0)
        append (&postproc->args, g_strdup_printf ("%d", postproc->t3));
    }
  } else
    postproc->args = g_strdup ("");
  change_mode (postproc);
}

static void
gst_post_proc_tmpnoise_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_T1:
      g_value_set_int (value, postproc->t1);
      break;
    case PROP_T2:
      g_value_set_int (value, postproc->t2);
      break;
    case PROP_T3:
      g_value_set_int (value, postproc->t3);
      break;
    default:
      gst_post_proc_get_property (object, prop_id, value, pspec);
      break;
  }
}

static void
gst_post_proc_autolevels_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_RANGE:
      postproc->range = g_value_get_boolean (value);
      break;
    default:
      gst_post_proc_set_property (object, prop_id, value, pspec);
      break;
  }

  /* construct args */
  g_free (postproc->args);
  if (postproc->range)
    postproc->args = g_strdup ("f");
  else
    postproc->args = g_strdup ("");
  change_mode (postproc);
}

static void
gst_post_proc_autolevels_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_RANGE:
      g_value_set_boolean (value, postproc->range);
      break;
    default:
      gst_post_proc_get_property (object, prop_id, value, pspec);
      break;
  }
}

static void
gst_post_proc_forcequant_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_QUANT:
      postproc->quant = g_value_get_int (value);
      break;
    default:
      gst_post_proc_set_property (object, prop_id, value, pspec);
      break;
  }

  /* construct args */
  g_free (postproc->args);
  if (postproc->quant >= 0)
    postproc->args = g_strdup_printf ("%d", postproc->quant);
  else
    postproc->args = g_strdup ("");
  change_mode (postproc);
}

static void
gst_post_proc_forcequant_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPostProc *postproc = (GstPostProc *) object;

  switch (prop_id) {
    case PROP_QUANT:
      g_value_set_int (value, postproc->quant);
      break;
    default:
      gst_post_proc_get_property (object, prop_id, value, pspec);
      break;
  }
}


static gboolean
gst_post_proc_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstPostProcClass),
    (GBaseInitFunc) gst_post_proc_base_init,
    NULL,
    (GClassInitFunc) gst_post_proc_class_init,
    NULL,
    NULL,
    sizeof (GstPostProc),
    0,
    (GInstanceInitFunc) gst_post_proc_init,
  };
  GType type;
  int i;

  global_plugins = g_hash_table_new (NULL, NULL);
  for (i = 0; filterdetails[i].shortname; i++) {
    gchar *type_name;

    g_hash_table_insert (global_plugins, GINT_TO_POINTER (0),
        GINT_TO_POINTER (i));

    /* create type_name */
    type_name = g_strdup_printf ("postproc_%s", filterdetails[i].longname);
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      continue;
    }

    /* create gtype */
    type = g_type_register_static (GST_TYPE_VIDEO_FILTER, type_name,
        &typeinfo, 0);

    g_hash_table_insert (global_plugins, GINT_TO_POINTER (type),
        GINT_TO_POINTER (i));

    /* register element */
    if (!gst_element_register (plugin, type_name, GST_RANK_PRIMARY, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (postproc_debug, "postproc", 0,
      "video postprocessing elements");

#ifdef HAVE_ORC
  orc_init ();
#endif

#ifndef GST_DISABLE_GST_DEBUG
  av_log_set_callback (gst_ffmpeg_log_callback);
#endif

  /* Register the filters */
  gst_post_proc_register (plugin);

  /* Now we can return the pointer to the newly created Plugin object. */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "postproc",
    "postprocessing elements (" FFMPEG_SOURCE ")",
    plugin_init,
    PACKAGE_VERSION, "GPL", "FFMpeg", "http://ffmpeg.sourceforge.net/")
