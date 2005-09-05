/*
    Copyright (C) 2005 Edward Hervey (edward@fluendo.com)

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
#include <gst/gstcpu.h>
#include <gst/video/video.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#include <libpostproc/postprocess.h>
#else
#include <ffmpeg/avcodec.h>
#include <ffmpeg/libpostproc/postprocess.h>
#endif

#include "gstpostproc.h"

typedef struct _PostProcDetails PostProcDetails;

struct _PostProcDetails {
  char	*shortname;
  char	*longname;
  char	*description;
};

static PostProcDetails filterdetails[] = {
  {"hb", "hdeblock",		"horizontal deblocking filter"},
  {"vb", "vdeblock",		"vertical deblocking filter"},
  {"h1", "x1hdeblock",		"experimental horizontal deblocking filter 1"},
  {"v1", "x1vdeblock",		"experimental vertical deblocking filter 1"},
  {"ha", "ahdeblock",		"another horizontal deblocking filter"},
  {"va", "avdeblock",		"another vertical deblocking filter"},
  {"dr", "dering",		"deringing filter"},
  {"al", "autolevels",		"automatic brightness/contrast filter"},
  {"lb", "linblenddeint",	"linear blend interpolater"},
  {"li", "linipoldeint",	"linear interpolation deinterlacer"},
  {"ci", "cubicipoldeint",	"cubic interpolation deinterlacer"},
  {"md", "mediandeint",		"median deinterlacer"},
  {"fd", "ffmpegdeint",		"ffmpeg deinterlacer"},
  {"l5", "lowpass5",		"FIR lowpass deinterlacer"},
  {"tn", "tmpnoise",		"temporal noise reducer"},
  {"fq", "forcequant",		"force quantizer"},
  {"de", "default",		"default filters"},
  {NULL, NULL,			NULL}
};

typedef struct	_GstPostProc GstPostProc;

struct	_GstPostProc {
  GstElement	element;

  GstPad	*sinkpad, *srcpad;
  guint		quality;
  gint		width, height;

  gint		ystride, ustride, vstride;
  gint		ysize, usize, vsize;

  pp_mode_t	*mode;
  pp_context_t	*context;
};

typedef struct	_GstPostProcClass GstPostProcClass;

struct	_GstPostProcClass {
  GstElementClass	parent_class;

  gint		filterid;
};

enum {
  ARG_0,
  ARG_QUALITY
};

/* hashtable, key = gtype, value = filterdetails index */
static GHashTable	*global_plugins;

/* TODO : add support for the other format supported by libpostproc */

static GstStaticPadTemplate gst_postproc_src_template = 
GST_STATIC_PAD_TEMPLATE ("src",
			 GST_PAD_SRC,
			 GST_PAD_ALWAYS,
			 GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("I420"))
			 );
			 
static GstStaticPadTemplate gst_postproc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
			 GST_PAD_SINK,
			 GST_PAD_ALWAYS,
			 GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("I420"))
			 );
			 
GST_DEBUG_CATEGORY (postproc_debug);
#define GST_CAT_DEFAULT postproc_debug

static void	gst_postproc_class_init (GstPostProcClass * klass);
static void	gst_postproc_base_init (GstPostProcClass * klass);
static void	gst_postproc_init (GstPostProc * pproc);
static void	gst_postproc_dispose (GObject * object);

static GstPadLinkReturn	gst_postproc_link (GstPad  * pad, const GstCaps * caps);

static void	gst_postproc_chain (GstPad * pad, GstData * data);

static GstStateChangeReturn	gst_postproc_change_state (GstElement * element,
    GstStateChange transition);

static void	gst_postproc_set_property ( GObject * object, guint prop_id,
					    const GValue * value,
					    GParamSpec *pspec );
static void	gst_postproc_get_property ( GObject * object, guint prop_id,
					    GValue * value, GParamSpec *pspec );

static GstElementClass	*parent_class = NULL;

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_ffmpeg_log_callback (void * ptr, int level, const char * fmt, va_list vl)
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
change_context ( GstPostProc * postproc , gint width, gint height )
{
  GstCPUFlags	flags;
  int		ppflags;
  /*
    TODO : We need to find out what CPU flags we have in order to set
    MMX/MMX2/3DNOW optimizations
  */

  GST_DEBUG ("change_context, width:%d, height:%d",
	     width, height);

  if ((width != postproc->width) && (height != postproc->height)) {
    if (postproc->context)
      pp_free_context (postproc->context);
    flags = gst_cpu_get_flags();
    ppflags = (flags & GST_CPU_FLAG_MMX ? PP_CPU_CAPS_MMX : 0)
      | (flags & GST_CPU_FLAG_MMXEXT ? PP_CPU_CAPS_MMX2 : 0)
      | (flags & GST_CPU_FLAG_3DNOW ? PP_CPU_CAPS_3DNOW : 0);
    postproc->context = pp_get_context (width, height, PP_FORMAT_420 | ppflags);
    postproc->width = width;
    postproc->height = height;
    postproc->ystride = ROUND_UP_4 (width);
    postproc->ustride = ROUND_UP_8 (width) / 2;
    postproc->vstride = ROUND_UP_8 (postproc->ystride) / 2;
    postproc->ysize = postproc->ystride * ROUND_UP_2 (height);
    postproc->usize = postproc->ustride * ROUND_UP_2 (height) / 2;
    postproc->vsize = postproc->vstride * ROUND_UP_2 (height) / 2;
    GST_DEBUG ("new strides are (YUV) : %d %d %d",
	       postproc->ystride, postproc->ustride, postproc->vstride);
  }
}

static void
change_mode ( GstPostProc * postproc )
{
  GstPostProcClass * klass = (GstPostProcClass *) G_OBJECT_GET_CLASS (G_OBJECT (postproc));

  if (postproc->mode)
    pp_free_mode (postproc->mode);
  postproc->mode = pp_get_mode_by_name_and_quality (filterdetails[klass->filterid].shortname,
						    postproc->quality);
}

static void
gst_postproc_base_init ( GstPostProcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  gint	ppidx;
  
  ppidx = GPOINTER_TO_INT (g_hash_table_lookup (global_plugins,
        GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class))));

  details.longname = g_strdup_printf ("LibPostProc %s filter", filterdetails[ppidx].longname);
  details.klass = "Filter/Video";
  details.description = g_strdup_printf ("LibPostProc %s", filterdetails[ppidx].description);
  details.author = "Edward Hervey <edward@fluendo.com>";
  gst_element_class_set_details (element_class, &details);
  g_free(details.longname);
  g_free(details.description);

  gst_element_class_add_pad_template (element_class, 
				      gst_static_pad_template_get (&gst_postproc_src_template));
  gst_element_class_add_pad_template (element_class, 
				      gst_static_pad_template_get (&gst_postproc_sink_template));

  klass->filterid = ppidx;
}

static void
gst_postproc_class_init (GstPostProcClass * klass)
{
  GObjectClass	*gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_uint ("quality", "Quality",
			 "Quality level of filter (6:best)",
			 0, 6, 6, G_PARAM_READWRITE));

  gobject_class->dispose = gst_postproc_dispose;
  gobject_class->set_property = gst_postproc_set_property;
  gobject_class->get_property = gst_postproc_get_property;
  gstelement_class->change_state = gst_postproc_change_state;
}

static void
gst_postproc_init (GstPostProc * postproc)
{
  GST_FLAG_SET (postproc, GST_ELEMENT_WORK_IN_PLACE);

  postproc->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get
						 (&gst_postproc_sink_template),
						 "sink");
  gst_pad_set_link_function (postproc->sinkpad, gst_postproc_link);
  gst_pad_set_chain_function (postproc->sinkpad, gst_postproc_chain);
  gst_element_add_pad (GST_ELEMENT (postproc), postproc->sinkpad);

  postproc->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
						(&gst_postproc_src_template),
						"src");
  gst_element_add_pad (GST_ELEMENT (postproc), postproc->srcpad);

  postproc->quality = 6;
  postproc->mode = NULL;
  change_mode (postproc);
  postproc->context = NULL;
  postproc->width = 0;
  postproc->height = 0;
  postproc->ystride = 0;
  postproc->ustride = 0;
  postproc->vstride = 0;
  postproc->ysize = 0;
  postproc->usize = 0;
  postproc->vsize = 0;
}

static void
gst_postproc_dispose (GObject * object)
{
  GstPostProc * postproc = (GstPostProc *) object;
  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (postproc->mode)
    pp_free_mode(postproc->mode);
  if (postproc->context)
    pp_free_context(postproc->context);
}

static GstPadLinkReturn
gst_postproc_link (GstPad * pad, const GstCaps * caps)
{
  GstPostProc	*postproc;
  GstStructure	*structure;
  GstPad	*otherpad;
  gboolean	res;
  GstPadLinkReturn	ret;
  gint		width, height;
  /* create/replace pp_context here */

  postproc = (GstPostProc *) gst_pad_get_parent (pad);
  otherpad = (pad == postproc->sinkpad) ? postproc->srcpad : postproc->sinkpad;

  structure = gst_caps_get_structure (caps, 0);
  res = gst_structure_get_int (structure, "width", &width);
  res &= gst_structure_get_int (structure, "height", &height);

  if (!res)
    return GST_PAD_LINK_REFUSED;

  ret = gst_pad_try_set_caps (otherpad, caps);

  if (GST_PAD_LINK_FAILED (ret))
    return ret;

  change_context (postproc, width, height);

  return GST_PAD_LINK_OK;
}

static void
gst_postproc_chain (GstPad * pad, GstData * data)
{
  GstPostProc	*postproc;
  GstBuffer	*in, *out;
  int		stride[3];
  unsigned char	* outplane[3];

  GST_DEBUG("chaining");

  /* postprocess the buffer !*/
  postproc = (GstPostProc *) gst_pad_get_parent (pad);

  g_return_if_fail(GST_IS_BUFFER (data));
  g_return_if_fail(postproc->mode != NULL);
  g_return_if_fail(postproc->context != NULL);

  in = GST_BUFFER (data);
  out = gst_buffer_copy_on_write (in);

  stride[0] = postproc->ystride;
  stride[1] = postproc->ustride;
  stride[2] = postproc->vstride;
/*   inplane[0] = GST_BUFFER_DATA(in); */
/*   inplane[1] = inplane[0] + postproc->ysize; */
/*   inplane[2] = inplane[1] + postproc->usize; */
  outplane[0] = GST_BUFFER_DATA(out);
  outplane[1] = outplane[0] + postproc->ysize;
  outplane[2] = outplane[1] + postproc->usize;

  GST_DEBUG ("calling pp_postprocess, width:%d, height:%d",
	     postproc->width, postproc->height);

  pp_postprocess (outplane, stride,
		  outplane, stride,
		  postproc->width,
		  postproc->height,
		  "", 0,
		  postproc->mode, postproc->context, 1);

  gst_buffer_stamp (out, in);

  gst_pad_push (postproc->srcpad, GST_DATA (out));
  /*
    void  pp_postprocess(uint8_t * src[3], int srcStride[3],
	uint8_t * dst[3], int dstStride[3],
	int horizontalSize, int verticalSize,
	QP_STORE_T *QP_store,  int QP_stride,
	pp_mode_t *mode, pp_context_t *ppContext, int pict_type);

	src is the src buffer data
	srcStride is ->ystride, ->ustride, ->vstride
	dst same as src
	dstStride same as srcStride
	horizontalSize and VerticalsSize are obvious
	QP_store can be null and qp_stride too
	mode = mode
	context = context
	pict_type = 0
  */

}

static GstStateChangeReturn
gst_postproc_change_state (GstElement * element, GstStateChange transition)
{
  GstPostProc	*postproc = (GstPostProc *) element;
  /* don't go to play if we don't have mode and context */

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    if ((!postproc->mode) && (!postproc->context))
      return GST_STATE_CHANGE_FAILURE;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_postproc_set_property ( GObject * object, guint prop_id,
			    const GValue * value,
			    GParamSpec *pspec )
{
  GstPostProc	*postproc = (GstPostProc *) object;
  gint quality;

  switch (prop_id) {
  case ARG_QUALITY:
    quality = g_value_get_uint (value);
    if (quality != postproc->quality) {
      postproc->quality = quality;
      change_mode (postproc);
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_postproc_get_property ( GObject * object, guint prop_id,
			    GValue * value, GParamSpec *pspec )
{
  GstPostProc	*postproc = (GstPostProc *) object;

  switch (prop_id) {
  case ARG_QUALITY:
    g_value_set_uint (value, postproc->quality);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

gboolean
gst_postproc_register(GstPlugin * plugin)
{
  GTypeInfo	typeinfo = {
    sizeof (GstPostProcClass),
    (GBaseInitFunc) gst_postproc_base_init,
    NULL,
    (GClassInitFunc) gst_postproc_class_init,
    NULL,
    NULL,
    sizeof (GstPostProc),
    0,
    (GInstanceInitFunc) gst_postproc_init,
  };
  GType type;
  int	i;

  global_plugins = g_hash_table_new (NULL, NULL);
  for (i = 0; filterdetails[i].shortname; i++) {
    gchar	*type_name;

    g_hash_table_insert (global_plugins,
			 GINT_TO_POINTER (0),
			 GINT_TO_POINTER (i));

    /* create type_name */
    type_name = g_strdup_printf("postproc_%s", filterdetails[i].longname);
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      continue;
    }

    /* create gtype */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    g_hash_table_insert (global_plugins,
			 GINT_TO_POINTER (type),
			 GINT_TO_POINTER (i));

    /* register element */
    if (!gst_element_register (plugin, type_name, GST_RANK_PRIMARY, type)) {
      g_free(type_name);
      return FALSE;
    }

    g_free(type_name);
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));
  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (postproc_debug, "postproc", 0, "video postprocessing elements");
#ifndef GST_DISABLE_GST_DEBUG
  av_log_set_callback (gst_ffmpeg_log_callback);
#endif

  /* Register the filters */
  gst_postproc_register( plugin );

  /* Now we can return the pointer to the newly created Plugin object. */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "postproc",
    "postprocessing elements",
    plugin_init,
    FFMPEG_VERSION, "GPL", "FFMpeg", "http://ffmpeg.sourceforge.net/")


