/* G-Streamer Video4linux2 video-capture plugin
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>
#include <sys/time.h>
#include "v4l2src_calls.h"
#include "gstv4l2tuner.h"

/* elementfactory details */
static GstElementDetails gst_v4l2src_details = {
	"Video (video4linux2) Source",
	"Source/Video",
	"Reads frames (compressed or uncompressed) from a video4linux2 device",
	"Ronald Bultje <rbultje@ronald.bitfreak.net>"
};

/* V4l2Src signals and args */
enum {
	SIGNAL_FRAME_CAPTURE,
	SIGNAL_FRAME_DROP,
	SIGNAL_FRAME_INSERT,
	SIGNAL_FRAME_LOST,
	LAST_SIGNAL
};

/* arguments */
enum {
	ARG_0,
	ARG_NUMBUFS,
	ARG_BUFSIZE,
	ARG_USE_FIXED_FPS
};

GST_FORMATS_FUNCTION (GstPad *, gst_v4l2src_get_formats,
		      GST_FORMAT_TIME, GST_FORMAT_DEFAULT);
GST_QUERY_TYPE_FUNCTION (GstPad *, gst_v4l2src_get_query_types,
			 GST_QUERY_POSITION);

/* init functions */
static void	gst_v4l2src_class_init		(GstV4l2SrcClass *klass);
static void	gst_v4l2src_base_init		(GstV4l2SrcClass *klass);
static void	gst_v4l2src_init		(GstV4l2Src      *v4l2src);

/* signal functions */
static void	gst_v4l2src_open		(GstElement      *element,
						 const gchar     *device);
static void	gst_v4l2src_close		(GstElement      *element,
						 const gchar     *device);

/* pad/info functions */
static gboolean	gst_v4l2src_src_convert		(GstPad          *pad,
						 GstFormat       src_format,
						 gint64          src_value,
						 GstFormat       *dest_format,
						 gint64          *dest_value);
static gboolean gst_v4l2src_src_query		(GstPad          *pad,
						 GstQueryType    type, 
						 GstFormat       *format,
						 gint64          *value);

/* buffer functions */
static GstPadLinkReturn
		gst_v4l2src_srcconnect		(GstPad          *pad,
						 GstCaps         *caps);
static GstCaps *gst_v4l2src_getcaps		(GstPad          *pad,
						 GstCaps         *caps);
static GstData *gst_v4l2src_get			(GstPad          *pad);

/* get/set params */
static void	gst_v4l2src_set_property	(GObject         *object,
						 guint           prop_id,
						 const GValue    *value,
						 GParamSpec      *pspec);
static void	gst_v4l2src_get_property	(GObject         *object,
						 guint           prop_id,
						 GValue          *value,
						 GParamSpec      *pspec);

/* state handling */
static GstElementStateReturn
		gst_v4l2src_change_state	(GstElement      *element);

/* set_clock function for A/V sync */
static void	gst_v4l2src_set_clock		(GstElement     *element,
						 GstClock       *clock);

/* bufferpool functions */
static GstBuffer *gst_v4l2src_buffer_new	(GstBufferPool   *pool,
						 guint64         offset,
						 guint           size,
						 gpointer        user_data);
static void	gst_v4l2src_buffer_free		(GstBufferPool   *pool,
						 GstBuffer       *buf,
						 gpointer        user_data);


static GstPadTemplate *src_template;

static GstElementClass *parent_class = NULL;
static guint gst_v4l2src_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4l2src_get_type (void)
{
	static GType v4l2src_type = 0;

	if (!v4l2src_type) {
		static const GTypeInfo v4l2src_info = {
			sizeof(GstV4l2SrcClass),
			(GBaseInitFunc) gst_v4l2src_base_init,
			NULL,
			(GClassInitFunc) gst_v4l2src_class_init,
			NULL,
			NULL,
			sizeof(GstV4l2Src),
			0,
			(GInstanceInitFunc) gst_v4l2src_init,
			NULL
		};
		v4l2src_type = g_type_register_static(GST_TYPE_V4L2ELEMENT,
			"GstV4l2Src", &v4l2src_info, 0);
	}
	return v4l2src_type;
}

static void
gst_v4l2src_base_init (GstV4l2SrcClass *klass)
{
	GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  
	gst_element_class_set_details (gstelement_class,
				       &gst_v4l2src_details);

	src_template = gst_pad_template_new ("src",
					     GST_PAD_SRC,
					     GST_PAD_ALWAYS,
					     NULL);

	gst_element_class_add_pad_template (gstelement_class, src_template);
}

static void
gst_v4l2src_class_init (GstV4l2SrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstV4l2ElementClass *v4l2_class;

	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;
	v4l2_class = (GstV4l2ElementClass*)klass;

	parent_class = g_type_class_ref(GST_TYPE_V4L2ELEMENT);

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
		g_param_spec_int("num_buffers","num_buffers","num_buffers",
				 G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
		g_param_spec_int("buffer_size","buffer_size","buffer_size",
				 G_MININT,G_MAXINT,0,G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_USE_FIXED_FPS,
		g_param_spec_boolean("use_fixed_fps", "Use Fixed FPS",
				     "Drop/Insert frames to reach a certain FPS (TRUE) "
				     "or adapt FPS to suit the number of frabbed frames",
				     TRUE, G_PARAM_READWRITE));

	/* signals */
	gst_v4l2src_signals[SIGNAL_FRAME_CAPTURE] =
		g_signal_new("frame_capture", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(GstV4l2SrcClass, frame_capture),
			     NULL, NULL, g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);
	gst_v4l2src_signals[SIGNAL_FRAME_DROP] =
		g_signal_new("frame_drop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(GstV4l2SrcClass, frame_drop),
			     NULL, NULL, g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);
	gst_v4l2src_signals[SIGNAL_FRAME_INSERT] =
		g_signal_new("frame_insert", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(GstV4l2SrcClass, frame_insert),
			     NULL, NULL, g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);
	gst_v4l2src_signals[SIGNAL_FRAME_LOST] =
		g_signal_new("frame_lost", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(GstV4l2SrcClass, frame_lost),
			     NULL, NULL, g_cclosure_marshal_VOID__INT,
			     G_TYPE_NONE, 1, G_TYPE_INT);


	gobject_class->set_property = gst_v4l2src_set_property;
	gobject_class->get_property = gst_v4l2src_get_property;

	gstelement_class->change_state = gst_v4l2src_change_state;

	v4l2_class->open = gst_v4l2src_open;
	v4l2_class->close = gst_v4l2src_close;

	gstelement_class->set_clock = gst_v4l2src_set_clock;
}


static void
gst_v4l2src_init (GstV4l2Src *v4l2src)
{
	GST_FLAG_SET(GST_ELEMENT(v4l2src), GST_ELEMENT_THREAD_SUGGESTED);

	v4l2src->srcpad = gst_pad_new_from_template(src_template, "src");
	gst_element_add_pad(GST_ELEMENT(v4l2src), v4l2src->srcpad);

	gst_pad_set_get_function(v4l2src->srcpad, gst_v4l2src_get);
	gst_pad_set_link_function(v4l2src->srcpad, gst_v4l2src_srcconnect);
	gst_pad_set_getcaps_function (v4l2src->srcpad, gst_v4l2src_getcaps);
	gst_pad_set_convert_function (v4l2src->srcpad, gst_v4l2src_src_convert);
	gst_pad_set_formats_function (v4l2src->srcpad,
				      gst_v4l2src_get_formats);
	gst_pad_set_query_function (v4l2src->srcpad,
				    gst_v4l2src_src_query);
	gst_pad_set_query_type_function (v4l2src->srcpad,
					 gst_v4l2src_get_query_types);

	v4l2src->bufferpool = gst_buffer_pool_new(NULL, NULL,
					gst_v4l2src_buffer_new,
					NULL,
					gst_v4l2src_buffer_free,
					v4l2src);

	v4l2src->breq.count = 0;

	v4l2src->formats = NULL;
	v4l2src->format_list = NULL;

	/* no clock */
	v4l2src->clock = NULL;

	/* fps */
	v4l2src->use_fixed_fps = TRUE;
}


static void
gst_v4l2src_open (GstElement  *element,
                  const gchar *device)
{
	gst_v4l2src_fill_format_list(GST_V4L2SRC(element));
}


static void
gst_v4l2src_close (GstElement  *element,
                   const gchar *device)
{
	gst_v4l2src_empty_format_list(GST_V4L2SRC(element));
}


static gfloat
gst_v4l2src_get_fps (GstV4l2Src *v4l2src)
{
	v4l2_std_id norm;
	const GList *item;

	if (!v4l2src->use_fixed_fps &&
	    v4l2src->clock != NULL &&
	    v4l2src->handled > 0) {
		/* try to get time from clock master and calculate fps */
		GstClockTime time = gst_clock_get_time(v4l2src->clock) -
		                      v4l2src->substract_time;
		return v4l2src->handled * GST_SECOND / time;
	}

	/* if that failed ... */
 
	if (!GST_V4L2_IS_OPEN(GST_V4L2ELEMENT(v4l2src)))
		return 0.;

	if (!gst_v4l2_get_norm(GST_V4L2ELEMENT(v4l2src), &norm))
		return 0.;
	for (item = GST_V4L2ELEMENT(v4l2src)->norms;
	     item != NULL; item = item->next) {
		GstV4l2TunerNorm *v4l2norm = item->data;
		if (v4l2norm->index == norm)
			return GST_TUNER_NORM(v4l2norm)->fps;
	}

	return 0.;
}

static gboolean
gst_v4l2src_src_convert (GstPad    *pad,
                         GstFormat  src_format,
                         gint64     src_value,
                         GstFormat *dest_format,
                         gint64    *dest_value)
{
	GstV4l2Src *v4l2src;
	gdouble fps;

	v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));

	if ((fps = gst_v4l2src_get_fps(v4l2src)) == 0)
		return FALSE;

	switch (src_format) {
		case GST_FORMAT_TIME:
			switch (*dest_format) {
				case GST_FORMAT_DEFAULT:
					*dest_value = src_value * fps / GST_SECOND;
					break;
				default:
					return FALSE;
			}
			break;

		case GST_FORMAT_DEFAULT:
			switch (*dest_format) {
				case GST_FORMAT_TIME:
					*dest_value = src_value * GST_SECOND / fps;
					break;
				default:
					return FALSE;
			}
			break;

		default:
			return FALSE;
	}

	return TRUE;
}

static gboolean
gst_v4l2src_src_query (GstPad      *pad,
                       GstQueryType type, 
                       GstFormat   *format,
                       gint64      *value)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  gdouble fps;

  if ((fps = gst_v4l2src_get_fps(v4l2src)) == 0)
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = v4l2src->handled * GST_SECOND / fps;
          break;
        case GST_FORMAT_DEFAULT:
          *value = v4l2src->handled;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}


static GstCaps *
gst_v4l2src_v4l2fourcc_to_caps (guint32        fourcc,
                                GstPropsEntry *width,
                                GstPropsEntry *height,
                                GstPropsEntry *fps,
				gboolean       compressed)
{
	GstCaps *caps = NULL;

	switch (fourcc) {
	case V4L2_PIX_FMT_MJPEG: /* Motion-JPEG */
	case V4L2_PIX_FMT_JPEG:  /* JFIF JPEG */
		caps = GST_CAPS_NEW("v4l2src_caps",
				    "video/x-jpeg",
				      NULL);
			break;
	case V4L2_PIX_FMT_RGB332:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32: {
		guint depth=0, bpp=0;
		gint endianness = 0;
		guint32 r_mask = 0, b_mask = 0, g_mask = 0;

		switch (fourcc) {
		case V4L2_PIX_FMT_RGB332:
			bpp = depth = 8;
			endianness = G_BYTE_ORDER; /* 'like, whatever' */
			r_mask = 0xe0; g_mask = 0x1c; b_mask = 0x03;
			break;
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_RGB555X:
			bpp = 16; depth = 15;
			endianness = G_BYTE_ORDER;
			if ((fourcc == V4L2_PIX_FMT_RGB555 &&
			     G_BYTE_ORDER == G_LITTLE_ENDIAN) ||
			    (fourcc == V4L2_PIX_FMT_RGB555X &&
			     G_BYTE_ORDER == G_BIG_ENDIAN)) {
				r_mask = 0x7c00;
				g_mask = 0x03e0;
				b_mask = 0x001f;
			} else {
				r_mask = 0x007c;
				g_mask = 0xe003;
				b_mask = 0x1f00;
			}
			break;
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			bpp = depth = 16;
			endianness = G_BYTE_ORDER;
			if ((fourcc == V4L2_PIX_FMT_RGB565 &&
			     G_BYTE_ORDER == G_LITTLE_ENDIAN) ||
			    (fourcc == V4L2_PIX_FMT_RGB565X &&
			     G_BYTE_ORDER == G_BIG_ENDIAN)) {
				r_mask = 0xf800;
				g_mask = 0x07e0;
				b_mask = 0x001f;
			} else {
				r_mask = 0x00f8;
				g_mask = 0xe007;
				b_mask = 0x1f00;
			}
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_BGR24:
			bpp = depth = 24;
			endianness = G_BIG_ENDIAN;
			if (fourcc == V4L2_PIX_FMT_RGB24) {
				r_mask = 0xff0000;
				g_mask = 0x00ff00;
				b_mask = 0x0000ff;
			} else {
				r_mask = 0x0000ff;
				g_mask = 0x00ff00;
				b_mask = 0xff0000;
			}
			break;
		case V4L2_PIX_FMT_RGB32:
		case V4L2_PIX_FMT_BGR32:
			bpp = depth = 32;
			endianness = G_BIG_ENDIAN;
			if (fourcc == V4L2_PIX_FMT_RGB32) {
				r_mask = 0xff000000;
				g_mask = 0x00ff0000;
				b_mask = 0x0000ff00;
			} else {
				r_mask = 0x000000ff;
				g_mask = 0x0000ff00;
				b_mask = 0x00ff0000;
			}
			break;
		default:
			g_assert_not_reached();
			break;
		}

		caps = GST_CAPS_NEW("v4l2src_caps",
				    "video/x-raw-rgb",
				      "bpp",        GST_PROPS_INT(bpp),
				      "depth",      GST_PROPS_INT(depth),
				      "red_mask",   GST_PROPS_INT(r_mask),
				      "green_mask", GST_PROPS_INT(g_mask),
				      "blue_mask",  GST_PROPS_INT(b_mask),
				      "endianness", GST_PROPS_INT(endianness),
				      NULL);
		break;
	}
	case V4L2_PIX_FMT_YUV420: /* I420/IYUV */
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_Y41P: {
		guint32 fcc = 0;

		switch (fourcc) {
		case V4L2_PIX_FMT_YUV420:
			fcc = GST_MAKE_FOURCC('I','4','2','0');
			break;
		case V4L2_PIX_FMT_YUYV:
			fcc = GST_MAKE_FOURCC('Y','U','Y','2');
			break;
		case V4L2_PIX_FMT_YVU420:
			fcc = GST_MAKE_FOURCC('Y','V','1','2');
			break;
		case V4L2_PIX_FMT_UYVY:
			fcc = GST_MAKE_FOURCC('U','Y','V','Y');
			break;
		case V4L2_PIX_FMT_Y41P:
			fcc = GST_MAKE_FOURCC('Y','4','1','P');
			break;
		default:
			g_assert_not_reached();
			break;
		}

		caps = GST_CAPS_NEW("v4l2src_caps",
				    "video/x-raw-yuv",
				      "format", GST_PROPS_FOURCC(fcc),
				      NULL);
		break;
	}
	default:
		GST_DEBUG (
			  "Unknown fourcc 0x%08x " GST_FOURCC_FORMAT ", trying default",
			  fourcc, GST_FOURCC_ARGS(fourcc));

		/* add the standard one */
		if (compressed) {
			guint32 print_format = GUINT32_FROM_LE(fourcc);
			gchar *print_format_str = (gchar *) &print_format, *string_format;
			gint i;

			for (i=0;i<4;i++) {
				print_format_str[i] =
					g_ascii_tolower(print_format_str[i]);
			}
			string_format = g_strdup_printf("video/%4.4s",
							print_format_str);
			caps = GST_CAPS_NEW("v4l2src_caps",
					    string_format,
					      NULL);
			g_free(string_format);
		} else {
			caps = GST_CAPS_NEW("v4l2src_caps",
					    "video/x-raw-yuv",
					      "format",GST_PROPS_FOURCC(fourcc),
					      NULL);
		}
		break;
	}

	if (!caps->properties)
		caps->properties = gst_props_empty_new();
	gst_props_add_entry(caps->properties, width);
	gst_props_add_entry(caps->properties, height);
	gst_props_add_entry(caps->properties, fps);

	return caps;
}

#define gst_v4l2src_v4l2fourcc_to_caps_fixed(f, width, height, fps, c) \
	gst_v4l2src_v4l2fourcc_to_caps(f, \
				       gst_props_entry_new("width", \
							   GST_PROPS_INT(width)), \
				       gst_props_entry_new("height", \
							   GST_PROPS_INT(height)), \
				       gst_props_entry_new("framerate", \
							   GST_PROPS_FLOAT(fps)), \
				       c)

#define gst_v4l2src_v4l2fourcc_to_caps_range(f, min_w, max_w, min_h, max_h, c) \
	gst_v4l2src_v4l2fourcc_to_caps(f, \
				       gst_props_entry_new("width", \
							   GST_PROPS_INT_RANGE(min_w, max_w)), \
				       gst_props_entry_new("height", \
                                                           GST_PROPS_INT_RANGE(min_h, max_h)), \
				       gst_props_entry_new("framerate", \
                                                           GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT)), \
				       c)

static struct v4l2_fmtdesc *
gst_v4l2_caps_to_v4l2fourcc (GstV4l2Src *v4l2src,
                             GstCaps    *caps)
{
	gint i;
	guint32 fourcc = 0;
	struct v4l2_fmtdesc *end_fmt = NULL;
	const gchar *format = gst_caps_get_mime(caps);

	if (!strcmp(format, "video/x-raw-yuv") ||
	    !strcmp(format, "video/x-raw-rgb")) {
		if (!strcmp(format, "video/x-raw-rgb"))
			fourcc = GST_MAKE_FOURCC('R','G','B',' ');
		else
			gst_caps_get_fourcc_int(caps, "format", &fourcc);

		switch (fourcc) {
		case GST_MAKE_FOURCC('I','4','2','0'):
		case GST_MAKE_FOURCC('I','Y','U','V'):
			fourcc = V4L2_PIX_FMT_YUV420;
			break;
		case GST_MAKE_FOURCC('Y','U','Y','2'):
			fourcc = V4L2_PIX_FMT_YUYV;
			break;
		case GST_MAKE_FOURCC('Y','4','1','P'):
			fourcc = V4L2_PIX_FMT_Y41P;
			break;
		case GST_MAKE_FOURCC('U','Y','V','Y'):
			fourcc = V4L2_PIX_FMT_UYVY;
			break;
		case GST_MAKE_FOURCC('Y','V','1','2'):
			fourcc = V4L2_PIX_FMT_YVU420;
			break;
		case GST_MAKE_FOURCC('R','G','B',' '): {
			gint depth, endianness;

			gst_caps_get_int(caps, "depth", &depth);
			gst_caps_get_int(caps, "endianness", &endianness);

			switch (depth) {
			case 8:
				fourcc = V4L2_PIX_FMT_RGB332;
				break;
			case 15:
				fourcc = (endianness == G_LITTLE_ENDIAN) ?
					 V4L2_PIX_FMT_RGB555 :
					 V4L2_PIX_FMT_RGB555X;
				break;
			case 16:
				fourcc = (endianness == G_LITTLE_ENDIAN) ?
					 V4L2_PIX_FMT_RGB565 :
					 V4L2_PIX_FMT_RGB565X;
				break;
			case 24:
				fourcc = (endianness == G_LITTLE_ENDIAN) ?
					 V4L2_PIX_FMT_BGR24 :
					 V4L2_PIX_FMT_RGB24;
				break;
			case 32:
				fourcc = (endianness == G_LITTLE_ENDIAN) ?
					 V4L2_PIX_FMT_BGR32 :
					 V4L2_PIX_FMT_RGB32;
				break;
			}
		}
		default:
			break;
		}
		for (i=0;i<g_list_length(v4l2src->formats);i++) {
			struct v4l2_fmtdesc *fmt;
			fmt = (struct v4l2_fmtdesc *)
				g_list_nth_data(v4l2src->formats, i);
			if (fmt->pixelformat == fourcc) {
				end_fmt = fmt;
				break;
			}
		}
	} else {
		/* compressed */
		if (strncmp(format, "video/", 6))
			return NULL;
		format = &format[6];
		if (strlen(format) != 4)
			return NULL;
		fourcc = GST_MAKE_FOURCC(g_ascii_toupper(format[0]),
					 g_ascii_toupper(format[1]),
					 g_ascii_toupper(format[2]),
					 g_ascii_toupper(format[3]));

		switch (fourcc) {
		case GST_MAKE_FOURCC('J','P','E','G'): {
			struct v4l2_fmtdesc *fmt;
			for (i=0;i<g_list_length(v4l2src->formats);i++) {
				fmt = g_list_nth_data(v4l2src->formats, i);
				if (fmt->pixelformat == V4L2_PIX_FMT_MJPEG ||
				    fmt->pixelformat == V4L2_PIX_FMT_JPEG) {
					end_fmt = fmt;
					break;
				}
			}
			break;
		}
		default: {
			/* FIXME: check for fourcc in list */
			struct v4l2_fmtdesc *fmt;
			for (i=0;i<g_list_length(v4l2src->formats);i++) {
				fmt = g_list_nth_data(v4l2src->formats, i);
				if (fourcc == fmt->pixelformat) {
					end_fmt = fmt;
					break;
				}
			}
			break;
		}
		}
	}

	return end_fmt;
}

#define gst_caps_get_int_range(caps, name, min, max) \
	gst_props_entry_get_int_range(gst_props_get_entry((caps)->properties, \
							  name), \
				      min, max)



static GstPadLinkReturn
gst_v4l2src_srcconnect (GstPad  *pad,
                        GstCaps *vscapslist)
{
	GstV4l2Src *v4l2src;
	GstV4l2Element *v4l2element;
	GstCaps *caps;
	struct v4l2_fmtdesc *format;
	int w, h;

	v4l2src = GST_V4L2SRC(gst_pad_get_parent (pad));
	v4l2element = GST_V4L2ELEMENT(v4l2src);

	/* clean up if we still haven't cleaned up our previous
	 * capture session */
	if (GST_V4L2_IS_ACTIVE(v4l2element)) {
		if (!gst_v4l2src_capture_deinit(v4l2src))
			return GST_PAD_LINK_REFUSED;
	} else if (!GST_V4L2_IS_OPEN(v4l2element)) {
		return GST_PAD_LINK_DELAYED;
	}

	for (caps = vscapslist; caps != NULL; caps = caps->next) {
		/* we want our own v4l2 type of fourcc codes */
		if (!(format = gst_v4l2_caps_to_v4l2fourcc(v4l2src, caps))) {
			continue;
		}
		if (gst_caps_has_property(caps, "width")) {
			if (gst_caps_has_fixed_property(caps, "width")) {
				gst_caps_get_int(caps, "width", &w);
			} else {
				int max;
				gst_caps_get_int_range(caps, "width", &max, &w);
			}
		}
		if (gst_caps_has_property(caps, "height")) {
			if (gst_caps_has_fixed_property(caps, "height")) {
				gst_caps_get_int(caps, "height", &h);
			} else {
				int max;
				gst_caps_get_int_range(caps, "height", &max, &h);
			}
		}

		/* we found the pixelformat! - try it out */
		if (gst_v4l2src_set_capture(v4l2src, format, w, h)) {
			/* it fits! Now, get the proper counterpart and retry
			 * it on the other side (again...) - if it works, we're
			 * done -> GST_PAD_LINK_OK */
			GstCaps *lastcaps;
			GstPadLinkReturn ret_val;

			lastcaps = gst_v4l2src_v4l2fourcc_to_caps_fixed(format->pixelformat,
						v4l2src->format.fmt.pix.width,
						v4l2src->format.fmt.pix.height,
						gst_v4l2src_get_fps(v4l2src),
			                        format->flags & V4L2_FMT_FLAG_COMPRESSED);

			ret_val = gst_pad_try_set_caps(v4l2src->srcpad,
						       lastcaps);

			if (ret_val > 0) {
				if (gst_v4l2src_capture_init(v4l2src)) {
					return GST_PAD_LINK_DONE;
				}
			} else if (ret_val == GST_PAD_LINK_DELAYED) {
				return GST_PAD_LINK_DELAYED;
			}
		}
	}

	return GST_PAD_LINK_REFUSED;
}


static GstCaps *
gst_v4l2src_getcaps (GstPad  *pad,
                     GstCaps *caps)
{
	GstV4l2Src *v4l2src = GST_V4L2SRC(gst_pad_get_parent (pad));
	GstCaps *list = NULL;
	gint i;
	struct v4l2_fmtdesc *format;
	int min_w, max_w, min_h, max_h;

	if (!GST_V4L2_IS_OPEN(GST_V4L2ELEMENT(v4l2src))) {
		return NULL;
	}

	/* build our own capslist */
	for (i=0;i<g_list_length(v4l2src->formats);i++) {
		format = g_list_nth_data(v4l2src->formats, i);

		/* get size delimiters */
		if (!gst_v4l2src_get_size_limits(v4l2src, format,
						 &min_w, &max_w,
						 &min_h, &max_h)) {
			continue;
		}

		/* add to list */
		caps = gst_v4l2src_v4l2fourcc_to_caps_range(format->pixelformat,
							    min_w, max_w,
							    min_h, max_h,
							    format->flags & V4L2_FMT_FLAG_COMPRESSED);

		list = gst_caps_append(list, caps);
	}

	return list;
}


static GstData*
gst_v4l2src_get (GstPad *pad)
{
	GstV4l2Src *v4l2src;
	GstBuffer *buf;
	gint num;
	gdouble fps = 0;

	g_return_val_if_fail (pad != NULL, NULL);

	v4l2src = GST_V4L2SRC(gst_pad_get_parent (pad));

	if (v4l2src->use_fixed_fps &&
	    (fps = gst_v4l2src_get_fps(v4l2src)) == 0)
		return NULL;

	buf = gst_buffer_new_from_pool(v4l2src->bufferpool, 0, 0);
	if (!buf) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Failed to create a new GstBuffer");
		return NULL;
	}

	if (v4l2src->need_writes > 0) {
		/* use last frame */
		num = v4l2src->last_frame;
		v4l2src->need_writes--;
	} else if (v4l2src->clock && v4l2src->use_fixed_fps) {
		GstClockTime time;
		gboolean have_frame = FALSE;

		do {
			/* by default, we use the frame once */
			v4l2src->need_writes = 1;

			/* grab a frame from the device */
			if (!gst_v4l2src_grab_frame(v4l2src, &num))
				return NULL;

			v4l2src->last_frame = num;
			time = GST_TIMEVAL_TO_TIME(v4l2src->bufsettings.timestamp) -
			         v4l2src->substract_time;

			/* first check whether we lost any frames according to the device */
			if (v4l2src->last_seq != 0) {
				if (v4l2src->bufsettings.sequence - v4l2src->last_seq > 1) {
					v4l2src->need_writes = v4l2src->bufsettings.sequence -
					                         v4l2src->last_seq;
					g_signal_emit(G_OBJECT(v4l2src),
					              gst_v4l2src_signals[SIGNAL_FRAME_LOST],
					              0,
					              v4l2src->bufsettings.sequence -
					                v4l2src->last_seq - 1);
				}
			}
			v4l2src->last_seq = v4l2src->bufsettings.sequence;

			/* decide how often we're going to write the frame - set
			 * v4lmjpegsrc->need_writes to (that-1) and have_frame to TRUE
			 * if we're going to write it - else, just continue.
			 * 
			 * time is generally the system or audio clock. Let's
			 * say that we've written one second of audio, then we want
			 * to have written one second of video too, within the same
			 * timeframe. This means that if time - begin_time = X sec,
			 * we want to have written X*fps frames. If we've written
			 * more - drop, if we've written less - dup... */
			if (v4l2src->handled * (GST_SECOND/fps) - time >
			    1.5 * (GST_SECOND/fps)) {
				/* yo dude, we've got too many frames here! Drop! DROP! */
				v4l2src->need_writes--; /* -= (v4l2src->handled - (time / fps)); */
				g_signal_emit(G_OBJECT(v4l2src),
				              gst_v4l2src_signals[SIGNAL_FRAME_DROP], 0);
			} else if (v4l2src->handled * (GST_SECOND/fps) - time <
			             -1.5 * (GST_SECOND/fps)) {
				/* this means we're lagging far behind */
				v4l2src->need_writes++; /* += ((time / fps) - v4l2src->handled); */
				g_signal_emit(G_OBJECT(v4l2src),
				              gst_v4l2src_signals[SIGNAL_FRAME_INSERT], 0);
			}

			if (v4l2src->need_writes > 0) {
				have_frame = TRUE;
				v4l2src->use_num_times[num] = v4l2src->need_writes;
				v4l2src->need_writes--;
			} else {
				gst_v4l2src_requeue_frame(v4l2src, num);
			}
		} while (!have_frame);
	} else {
		/* grab a frame from the device */
		if (!gst_v4l2src_grab_frame(v4l2src, &num))
			return NULL;

		v4l2src->use_num_times[num] = 1;
	}

	GST_BUFFER_DATA(buf) = gst_v4l2src_get_buffer(v4l2src, num);
	GST_BUFFER_SIZE(buf) = v4l2src->bufsettings.bytesused;
	if (v4l2src->use_fixed_fps)
		GST_BUFFER_TIMESTAMP(buf) = v4l2src->handled * GST_SECOND / fps;
	else /* calculate time based on our own clock */
		GST_BUFFER_TIMESTAMP(buf) = GST_TIMEVAL_TO_TIME(v4l2src->bufsettings.timestamp) -
		                              v4l2src->substract_time;

	v4l2src->handled++;
	g_signal_emit(G_OBJECT(v4l2src),
		      gst_v4l2src_signals[SIGNAL_FRAME_CAPTURE], 0);

	return GST_DATA (buf);
}


static void
gst_v4l2src_set_property (GObject      *object,
                          guint        prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	GstV4l2Src *v4l2src;

	g_return_if_fail(GST_IS_V4L2SRC(object));
	v4l2src = GST_V4L2SRC(object);

	switch (prop_id) {
		case ARG_NUMBUFS:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->breq.count = g_value_get_int(value);
			}
			break;

		case ARG_USE_FIXED_FPS:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->use_fixed_fps = g_value_get_boolean(value);
			}
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


static void
gst_v4l2src_get_property (GObject    *object,
                          guint      prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	GstV4l2Src *v4l2src;

	g_return_if_fail(GST_IS_V4L2SRC(object));
	v4l2src = GST_V4L2SRC(object);

	switch (prop_id) {
		case ARG_NUMBUFS:
			g_value_set_int(value, v4l2src->breq.count);
			break;

		case ARG_BUFSIZE:
			g_value_set_int(value, v4l2src->format.fmt.pix.sizeimage);
			break;

		case ARG_USE_FIXED_FPS:
			g_value_set_boolean(value, v4l2src->use_fixed_fps);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


static GstElementStateReturn
gst_v4l2src_change_state (GstElement *element)
{
	GstV4l2Src *v4l2src;
	gint transition = GST_STATE_TRANSITION (element);
	GstElementStateReturn parent_return;
	GTimeVal time;

	g_return_val_if_fail(GST_IS_V4L2SRC(element), GST_STATE_FAILURE);
	v4l2src = GST_V4L2SRC(element);

	if (GST_ELEMENT_CLASS (parent_class)->change_state) {
		parent_return = GST_ELEMENT_CLASS (parent_class)->change_state (element);
		if (parent_return != GST_STATE_SUCCESS)
			return parent_return;
	}

	switch (transition) {
		case GST_STATE_NULL_TO_READY:
			if (!gst_v4l2src_get_capture(v4l2src))
				return GST_STATE_FAILURE;
			break;
		case GST_STATE_READY_TO_PAUSED:
			v4l2src->handled = 0;
			v4l2src->need_writes = 0;
			v4l2src->last_frame = 0;
			v4l2src->substract_time = 0;
			/* buffer setup moved to capsnego */
			break;
		case GST_STATE_PAUSED_TO_PLAYING:
			/* queue all buffer, start streaming capture */
			if (!gst_v4l2src_capture_start(v4l2src))
				return GST_STATE_FAILURE;
			g_get_current_time(&time);
			v4l2src->substract_time = GST_TIMEVAL_TO_TIME(time) -
			                            v4l2src->substract_time;
			v4l2src->last_seq = 0;
			break;
		case GST_STATE_PLAYING_TO_PAUSED:
			g_get_current_time(&time);
			v4l2src->substract_time = GST_TIMEVAL_TO_TIME(time) -
			                            v4l2src->substract_time;
			/* de-queue all queued buffers */
			if (!gst_v4l2src_capture_stop(v4l2src))
				return GST_STATE_FAILURE;
			break;
		case GST_STATE_PAUSED_TO_READY:
			/* stop capturing, unmap all buffers */
			if (!gst_v4l2src_capture_deinit(v4l2src))
				return GST_STATE_FAILURE;
			break;
		case GST_STATE_READY_TO_NULL:
			break;
	}

	return GST_STATE_SUCCESS;
}


static void
gst_v4l2src_set_clock (GstElement *element,
                           GstClock   *clock)
{
	GST_V4L2SRC(element)->clock = clock;
}


static GstBuffer*
gst_v4l2src_buffer_new (GstBufferPool *pool,
                        guint64        offset,
                        guint          size,
                        gpointer       user_data)
{
	GstBuffer *buffer;
	GstV4l2Src *v4l2src = GST_V4L2SRC(user_data);

	if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src)))
		return NULL;

	buffer = gst_buffer_new();
	if (!buffer)
		return NULL;

	/* TODO: add interlacing info to buffer as metadata
	 * (height>288 or 240 = topfieldfirst, else noninterlaced) */
	GST_BUFFER_MAXSIZE(buffer) = v4l2src->bufsettings.length;
	GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_DONTFREE);

	return buffer;
}


static void
gst_v4l2src_buffer_free (GstBufferPool *pool,
                         GstBuffer     *buf,
                         gpointer      user_data)
{
	GstV4l2Src *v4l2src = GST_V4L2SRC(user_data);
	int n;

	if (gst_element_get_state(GST_ELEMENT(v4l2src)) != GST_STATE_PLAYING)
		return; /* we've already cleaned up ourselves */

	for (n=0;n<v4l2src->breq.count;n++)
		if (GST_BUFFER_DATA(buf) == gst_v4l2src_get_buffer(v4l2src, n)) {
			v4l2src->use_num_times[n]--;
			if (v4l2src->use_num_times[n] <= 0) {
				gst_v4l2src_requeue_frame(v4l2src, n);
			}
			break;
		}

	if (n == v4l2src->breq.count)
		gst_element_error(GST_ELEMENT(v4l2src),
			"Couldn\'t find the buffer");

	/* free the buffer itself */
	gst_buffer_default_free(buf);
}
