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

#include <string.h>
#include <sys/time.h>
#include "v4l2src_calls.h"


static GstElementDetails gst_v4l2src_details = {
	"Video (video4linux2) Source",
	"Source/Video",
	"Reads frames (compressed or uncompressed) from a video4linux2 device",
	VERSION,
	"Ronald Bultje <rbultje@ronald.bitfreak.net>",
	"(C) 2002",
};

/* V4l2Src signals and args */
enum {
	/* FILL ME */
	LAST_SIGNAL
};

/* arguments */
enum {
	ARG_0,
	ARG_WIDTH,
	ARG_HEIGHT,
	ARG_PALETTE,
	ARG_PALETTE_NAMES,
	ARG_FOURCC,
	ARG_FOURCC_LIST,
	ARG_NUMBUFS,
	ARG_BUFSIZE
};


/* init functions */
static void			gst_v4l2src_class_init		(GstV4l2SrcClass *klass);
static void			gst_v4l2src_init		(GstV4l2Src      *v4l2src);

/* pad/buffer functions */
static gboolean			gst_v4l2src_srcconvert		(GstPad          *pad,
								 GstFormat       src_format,
								 gint64          src_value,
								 GstFormat       *dest_format,
								 gint64          *dest_value);
static GstPadConnectReturn	gst_v4l2src_srcconnect		(GstPad          *pad,
								 GstCaps         *caps);
static GstBuffer *		gst_v4l2src_get			(GstPad          *pad);

/* get/set params */
static void			gst_v4l2src_set_property	(GObject         *object,
								 guint           prop_id,
								 const GValue    *value,
								 GParamSpec      *pspec);
static void			gst_v4l2src_get_property	(GObject         *object,
								 guint           prop_id,
								 GValue          *value,
								 GParamSpec      *pspec);

/* state handling */
static GstElementStateReturn	gst_v4l2src_change_state	(GstElement      *element);

/* bufferpool functions */
static GstBuffer *		gst_v4l2src_buffer_new		(GstBufferPool   *pool,
								 guint64         offset,
								 guint           size,
								 gpointer        user_data);
static GstBuffer *		gst_v4l2src_buffer_copy		(GstBufferPool   *pool,
								 const GstBuffer *srcbuf,
								 gpointer        user_data);
static void			gst_v4l2src_buffer_free		(GstBufferPool   *pool,
								 GstBuffer       *buf,
								 gpointer        user_data);


static GstPadTemplate *src_template;

static GstElementClass *parent_class = NULL;
/*static guint gst_v4l2src_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_v4l2src_get_type (void)
{
	static GType v4l2src_type = 0;

	if (!v4l2src_type) {
		static const GTypeInfo v4l2src_info = {
			sizeof(GstV4l2SrcClass),
			NULL,
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
gst_v4l2src_class_init (GstV4l2SrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;

	parent_class = g_type_class_ref(GST_TYPE_V4L2ELEMENT);

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
		g_param_spec_int("width","width","width",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
		g_param_spec_int("height","height","height",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PALETTE,
		g_param_spec_int("palette","palette","palette",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PALETTE_NAMES,
		g_param_spec_pointer("palette_name","palette_name","palette_name",
		G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FOURCC,
		g_param_spec_string("fourcc","fourcc","fourcc",
		NULL,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FOURCC_LIST,
		g_param_spec_pointer("fourcc_list","fourcc_list","fourcc_list",
		G_PARAM_READABLE));

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
		g_param_spec_int("num_buffers","num_buffers","num_buffers",
		G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
		g_param_spec_int("buffer_size","buffer_size","buffer_size",
		G_MININT,G_MAXINT,0,G_PARAM_READABLE));

	gobject_class->set_property = gst_v4l2src_set_property;
	gobject_class->get_property = gst_v4l2src_get_property;

	gstelement_class->change_state = gst_v4l2src_change_state;
}


static void
gst_v4l2src_init (GstV4l2Src *v4l2src)
{
	v4l2src->srcpad = gst_pad_new_from_template(src_template, "src");
	gst_element_add_pad(GST_ELEMENT(v4l2src), v4l2src->srcpad);

	gst_pad_set_get_function(v4l2src->srcpad, gst_v4l2src_get);
	gst_pad_set_connect_function(v4l2src->srcpad, gst_v4l2src_srcconnect);
	gst_pad_set_convert_function (v4l2src->srcpad, gst_v4l2src_srcconvert);

	v4l2src->bufferpool = gst_buffer_pool_new(NULL, NULL,
					gst_v4l2src_buffer_new,
					gst_v4l2src_buffer_copy,
					gst_v4l2src_buffer_free,
					v4l2src);

	v4l2src->palette = 0; /* means 'any' - user can specify a specific palette */
	v4l2src->width = 160;
	v4l2src->height = 120;
	v4l2src->breq.count = 0;
}


static gboolean
gst_v4l2src_srcconvert (GstPad    *pad,
                        GstFormat  src_format,
                        gint64     src_value,
                        GstFormat *dest_format,
                        gint64    *dest_value)
{
	GstV4l2Src *v4l2src;
	gint norm;
	struct v4l2_standard *std;
	gdouble fps;

	v4l2src = GST_V4L2SRC (gst_pad_get_parent (pad));

	if (!GST_V4L2_IS_OPEN(v4l2src))
		return FALSE;

	if (!gst_v4l2_get_norm(v4l2src, &norm))
		return FALSE;

	std = &((struct v4l2_enumstd *) g_list_nth_data(v4l2src->norms, norm))->std;
	fps = std->framerate.numerator / std->framerate.denominator;

	switch (src_format) {
		case GST_FORMAT_TIME:
			switch (*dest_format) {
				case GST_FORMAT_DEFAULT:
					*dest_format = GST_FORMAT_UNITS;
					/* fall-through */
				case GST_FORMAT_UNITS:
					*dest_value = src_value * fps / GST_SECOND;
					break;
				default:
					return FALSE;
			}
			break;

		case GST_FORMAT_UNITS:
			switch (*dest_format) {
				case GST_FORMAT_DEFAULT:
					*dest_format = GST_FORMAT_TIME;
					/* fall-through */
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


static GstCaps *
gst_v4l2src_v4l2fourcc_to_caps (guint32  fourcc,
                                gint     width,
                                gint     height,
                                gboolean compressed)
{
	GstCaps *capslist = NULL, *caps;

	switch (fourcc) {
		case V4L2_PIX_FMT_MJPEG: /* v4l2_fourcc('M','J','P','G') */
			caps = gst_caps_new("v4l2src_caps",
					"video/jpeg",
					gst_props_new(
						"width",	GST_PROPS_INT(width),
						"height",	GST_PROPS_INT(height),
						NULL));
			capslist = gst_caps_append(capslist, caps);
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
			gint endianness=0;
			gulong r_mask=0, b_mask=0, g_mask=0;
			switch (fourcc) {
				case V4L2_PIX_FMT_RGB332:
					bpp = depth = 8;
					endianness = G_BYTE_ORDER; /* 'like, whatever' */
					r_mask = 0xe0; g_mask = 0x1c; b_mask = 0x03;
					break;
				case V4L2_PIX_FMT_RGB555:
					bpp = 16; depth = 15;
					endianness = G_LITTLE_ENDIAN;
					r_mask = 0x7c00; g_mask = 0x03e0; b_mask = 0x001f;
					break;
				case V4L2_PIX_FMT_RGB555X:
					bpp = 16; depth = 15;
					endianness = G_BIG_ENDIAN;
					r_mask = 0x7c00; g_mask = 0x03e0; b_mask = 0x001f;
					break;
				case V4L2_PIX_FMT_RGB565:
					bpp = depth = 16;
					endianness = G_LITTLE_ENDIAN;
					r_mask = 0xf800; g_mask = 0x07e0; b_mask = 0x001f;
					break;
				case V4L2_PIX_FMT_RGB565X:
					bpp = depth = 16;
					endianness = G_BIG_ENDIAN;
					r_mask = 0xf800; g_mask = 0x07e0; b_mask = 0x001f;
					break;
				case V4L2_PIX_FMT_RGB24:
					bpp = depth = 24;
					endianness = G_BIG_ENDIAN;
					r_mask = 0xff0000; g_mask = 0x00ff00; b_mask = 0x0000ff;
					break;
				case V4L2_PIX_FMT_BGR24:
					bpp = depth = 24;
					endianness = G_LITTLE_ENDIAN;
					r_mask = 0xff0000; g_mask = 0x00ff00; b_mask = 0x0000ff;
					break;
				case V4L2_PIX_FMT_RGB32:
					bpp = depth = 32;
					endianness = G_BIG_ENDIAN;
					r_mask = 0x00ff0000; g_mask = 0x0000ff00; b_mask = 0x000000ff;
					break;
				case V4L2_PIX_FMT_BGR32:
					endianness = G_LITTLE_ENDIAN;
					bpp = depth = 32;
					r_mask = 0x00ff0000; g_mask = 0x0000ff00; b_mask = 0x000000ff;
					break;
			}
			caps = gst_caps_new("v4l2src_caps",
					"video/raw",
					gst_props_new(
						"format",	GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
						"width",	GST_PROPS_INT(width),
						"height",	GST_PROPS_INT(height),
						"bpp",		GST_PROPS_INT(bpp),
						"depth",	GST_PROPS_INT(depth),
						"red_mask",	GST_PROPS_INT(r_mask),
						"green_mask",	GST_PROPS_INT(g_mask),
						"blue_mask",	GST_PROPS_INT(b_mask),
						"endianness",	GST_PROPS_INT(endianness),
						NULL));
			capslist = gst_caps_append(capslist, caps);
			break; }
		case V4L2_PIX_FMT_YUV420: /* I420/IYUV */
			caps = gst_caps_new("v4l2src_caps",
				"video/raw",
				gst_props_new(
					"format",	GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0')),
					"width",	GST_PROPS_INT(width),
					"height",	GST_PROPS_INT(height),
					NULL));
			capslist = gst_caps_append(capslist, caps);
			caps = gst_caps_new("v4l2src_caps",
				"video/raw",
				gst_props_new(
					"format",	GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','Y','U','V')),
					"width",	GST_PROPS_INT(width),
					"height",	GST_PROPS_INT(height),
					NULL));
			capslist = gst_caps_append(capslist, caps);
			break;
		case V4L2_PIX_FMT_YUYV:
			caps = gst_caps_new("v4l2src_caps",
				"video/raw",
				gst_props_new(
					"format",	GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','Y','2')),
					"width",	GST_PROPS_INT(width),
					"height",	GST_PROPS_INT(height),
					NULL));
			capslist = gst_caps_append(capslist, caps);
			break;
		default:
			break;
	}

	/* add the standard one */
	if (compressed) {
		guint32 print_format = GUINT32_FROM_LE(fourcc);
		gchar *print_format_str = (gchar *) &print_format, *string_format;
		gint i;
		for (i=0;i<4;i++)
			print_format_str[i] = g_ascii_tolower(print_format_str[i]);
		string_format = g_strdup_printf("video/%4.4s", print_format_str);
		caps = gst_caps_new("v4l2src_caps",
				string_format,
				gst_props_new(
					"width",	GST_PROPS_INT(width),
					"height",	GST_PROPS_INT(height),
					NULL));
		capslist = gst_caps_append(capslist, caps);
		g_free(string_format);
	} else {
		caps = gst_caps_new("v4l2src_caps",
				"video/raw",
				gst_props_new(
					"format",	GST_PROPS_FOURCC(fourcc),
					"width",	GST_PROPS_INT(width),
					"height",	GST_PROPS_INT(height),
					NULL));
		capslist = gst_caps_append(capslist, caps);
	}

	return capslist;
}


static GList *
gst_v4l2_caps_to_v4l2fourcc (GstV4l2Src *v4l2src,
                             GstCaps    *capslist)
{
	GList *fourcclist = NULL;
	GstCaps *caps;
	guint32 fourcc;
	gint i;

	for (caps = capslist;caps != NULL; caps = caps->next) {
		const gchar *format = gst_caps_get_mime(caps);

		if (!strcmp(format, "video/raw")) {
			/* non-compressed */
			gst_caps_get_fourcc_int(caps, "format", &fourcc);
			switch (fourcc) {
				case GST_MAKE_FOURCC('I','4','2','0'):
				case GST_MAKE_FOURCC('I','Y','U','V'):
					fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_YUV420);
					break;
				case GST_MAKE_FOURCC('Y','U','Y','2'):
					fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_YUYV);
					break;
				case GST_MAKE_FOURCC('R','G','B',' '): {
					gint depth, endianness;
					gst_caps_get_int(caps, "depth", &depth);
					gst_caps_get_int(caps, "endianness", &endianness);
					if (depth == 8) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB332);
					} else if (depth == 15 && endianness == G_LITTLE_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB555);
					} else if (depth == 15 && endianness == G_BIG_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB555X);
					} else if (depth == 16 && endianness == G_LITTLE_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB565);
					} else if (depth == 16 && endianness == G_BIG_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB565X);
					} else if (depth == 24 && endianness == G_LITTLE_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_BGR24);
					} else if (depth == 24 && endianness == G_BIG_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB24);
					} else if (depth == 32 && endianness == G_LITTLE_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_BGR32);
					} else if (depth == 32 && endianness == G_BIG_ENDIAN) {
						fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_RGB32);
					}
					break; }
			}

			for (i=0;i<g_list_length(GST_V4L2ELEMENT(v4l2src)->formats);i++) {
				struct v4l2_fmtdesc *fmt = (struct v4l2_fmtdesc *) g_list_nth_data(GST_V4L2ELEMENT(v4l2src)->formats, i);
				if (fmt->pixelformat == fourcc)
					fourcclist = g_list_append(fourcclist, (gpointer)fourcc);
			}
		} else {
			/* compressed */
			gchar *format_us;
			gint i;
			if (strncmp(format, "video/", 6))
				continue;
			format = &format[6];
			if (strlen(format) != 4)
				continue;
			format_us = g_strdup(format);
			for (i=0;i<4;i++)
				format_us[i] = g_ascii_toupper(format_us[i]);
			fourcc = GST_MAKE_FOURCC(format_us[0],format_us[1],format_us[2],format_us[3]);
			switch (fourcc) {
				case GST_MAKE_FOURCC('J','P','E','G'):
					fourcclist = g_list_append(fourcclist, (gpointer)V4L2_PIX_FMT_MJPEG);
					break;
			}
			fourcclist = g_list_append(fourcclist, (gpointer)fourcc);
		}
	}

	return fourcclist;
}


static GstCaps *
gst_v4l2src_caps_intersect (GstCaps *caps1,
                            GstCaps *_caps2)
{
	GstCaps *_list = NULL;

	if (!_caps2)
		return caps1;

	for (;caps1!=NULL;caps1=caps1->next) {
		GstCaps *caps2 = _caps2;

		for (;caps2!=NULL;caps2=caps2->next) {
			if (!strcmp(gst_caps_get_mime(caps1), gst_caps_get_mime(caps2))) {
				if (!strcmp(gst_caps_get_mime(caps1), "video/raw")) {
					guint32 fmt1, fmt2;
					gst_caps_get_fourcc_int(caps1, "format", &fmt1);
					gst_caps_get_fourcc_int(caps2, "format", &fmt2);
					if (fmt1 == fmt2)
						goto try_it_out;
				} else if (!strncmp(gst_caps_get_mime(caps1), "video/", 6)) {
					goto try_it_out;
				}
				continue;
			}
			continue;

		try_it_out:
			if (!strcmp(gst_caps_get_mime(caps1), "video/raw")) {
				GstCaps *list = _list;
				for (;list!=NULL;list=list->next) {
					if (!strcmp(gst_caps_get_mime(list), gst_caps_get_mime(caps1))) {
						guint32 fmt1, fmt2;
						gst_caps_get_fourcc_int(caps1, "format", &fmt1);
						gst_caps_get_fourcc_int(list, "format", &fmt2);
						if (fmt1 == fmt2)
							break;
					}
				}
				if (list == NULL)
					goto add_it;
			} else {
				GstCaps *list = _list;
				for (;list!=NULL;list=list->next) {
					if (!strcmp(gst_caps_get_mime(list), gst_caps_get_mime(caps1))) {
							break;
					}
				}
				if (list == NULL)
					goto add_it;
			}
			continue;

		add_it:
			_list = gst_caps_append(_list, gst_caps_copy_1(caps1));
			break;
		}
	}

	return _list;
}


static GstPadConnectReturn
gst_v4l2src_srcconnect (GstPad  *pad,
                        GstCaps *vscapslist)
{
	GstV4l2Src *v4l2src;
	GstV4l2Element *v4l2element;
	GstCaps *owncapslist;
	GstCaps *caps;
	GList *fourccs;
	gint i;

	v4l2src = GST_V4L2SRC(gst_pad_get_parent (pad));
	v4l2element = GST_V4L2ELEMENT(v4l2src);

	/* clean up if we still haven't cleaned up our previous
	 * capture session */
	if (GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src)))
		if (!gst_v4l2src_capture_deinit(v4l2src))
			return GST_PAD_CONNECT_REFUSED;

	/* build our own capslist */
	if (v4l2src->palette) {
		struct v4l2_fmtdesc *format = g_list_nth_data(v4l2element->formats, v4l2src->palette);
		owncapslist = gst_v4l2src_v4l2fourcc_to_caps(format->pixelformat,
						v4l2src->width, v4l2src->height,
						format->flags & V4L2_FMT_FLAG_COMPRESSED);
	} else {
		gint i;
		owncapslist = NULL;
		for (i=0;i<g_list_length(v4l2element->formats);i++) {
			struct v4l2_fmtdesc *format = g_list_nth_data(v4l2element->formats, i);
			caps = gst_v4l2src_v4l2fourcc_to_caps(format->pixelformat,
							v4l2src->width, v4l2src->height,
							format->flags & V4L2_FMT_FLAG_COMPRESSED);
			owncapslist = gst_caps_append(owncapslist, caps);
		}
	}

	/* and now, get the caps that we have in common */
	if (!(caps = gst_v4l2src_caps_intersect(owncapslist, vscapslist)))
		return GST_PAD_CONNECT_REFUSED;

	/* and get them back to V4L2-compatible fourcc codes */
	if (!(fourccs = gst_v4l2_caps_to_v4l2fourcc(v4l2src, caps)))
		return GST_PAD_CONNECT_REFUSED;

	/* we now have the fourcc codes. try out each of them */
	for (i=0;i<g_list_length(fourccs);i++) {
		guint32 fourcc = (guint32)g_list_nth_data(fourccs, i);
		gint n;
		for (n=0;n<g_list_length(v4l2element->formats);n++) {
			struct v4l2_fmtdesc *format = g_list_nth_data(v4l2element->formats, n);
			if (format->pixelformat == fourcc) {
				/* we found the pixelformat! - try it out */
				if (gst_v4l2src_set_capture(v4l2src, format,
				            v4l2src->width, v4l2src->height)) {
					/* it fits! Now, get the proper counterpart and retry
					 * it on the other side (again...) - if it works, we're
					 * done -> GST_PAD_CONNECT_OK */
					GstCaps *lastcaps = gst_v4l2src_v4l2fourcc_to_caps(format->pixelformat,
								v4l2src->format.fmt.pix.width, v4l2src->format.fmt.pix.height,
					                        format->flags & V4L2_FMT_FLAG_COMPRESSED);
					GstCaps *onecaps;
					for (;lastcaps != NULL; lastcaps = lastcaps->next) {
						onecaps = gst_caps_copy_1(lastcaps);
						if (gst_pad_try_set_caps(v4l2src->srcpad, onecaps) > 0)
							if (gst_v4l2src_capture_init(v4l2src))
								return GST_PAD_CONNECT_OK;
					}
				}
			}
		}
	}

	return GST_PAD_CONNECT_REFUSED;
}


static GstBuffer*
gst_v4l2src_get (GstPad *pad)
{
	GstV4l2Src *v4l2src;
	GstBuffer *buf;
	gint num;

	g_return_val_if_fail (pad != NULL, NULL);

	v4l2src = GST_V4L2SRC(gst_pad_get_parent (pad));

	buf = gst_buffer_new_from_pool(v4l2src->bufferpool, 0, 0);
	if (!buf) {
		gst_element_error(GST_ELEMENT(v4l2src),
			"Failed to create a new GstBuffer");
		return NULL;
	}

	/* grab a frame from the device */
	if (!gst_v4l2src_grab_frame(v4l2src, &num))
		return NULL;
	GST_BUFFER_DATA(buf) = GST_V4L2ELEMENT(v4l2src)->buffer[num];
	GST_BUFFER_SIZE(buf) = v4l2src->bufsettings.bytesused;
	GST_BUFFER_MAXSIZE(buf) = v4l2src->bufsettings.length;
	if (!v4l2src->first_timestamp)
		v4l2src->first_timestamp = v4l2src->bufsettings.timestamp;
	GST_BUFFER_TIMESTAMP(buf) = v4l2src->bufsettings.length - v4l2src->first_timestamp;

	return buf;
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
		case ARG_WIDTH:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->width = g_value_get_int(value);
			}
			break;

		case ARG_HEIGHT:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->height = g_value_get_int(value);
			}
			break;

		case ARG_PALETTE:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->palette = g_value_get_int(value);
			}
			break;

		case ARG_FOURCC:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				gint i;
				const gchar *formatstr = g_value_get_string(value);
				guint32 fourcc = GST_MAKE_FOURCC(formatstr[0],formatstr[1],formatstr[2],formatstr[3]);
				for (i=0;i<g_list_length(GST_V4L2ELEMENT(v4l2src)->formats);i++) {
					struct v4l2_fmtdesc *fmt = (struct v4l2_fmtdesc *) g_list_nth_data(GST_V4L2ELEMENT(v4l2src)->formats, i);
					if (fmt->pixelformat == fourcc)
						v4l2src->palette = i;
				}
			}
			break;

		case ARG_NUMBUFS:
			if (!GST_V4L2_IS_ACTIVE(GST_V4L2ELEMENT(v4l2src))) {
				v4l2src->breq.count = g_value_get_int(value);
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
		case ARG_WIDTH:
			g_value_set_int(value, v4l2src->width);
			break;

		case ARG_HEIGHT:
			g_value_set_int(value, v4l2src->height);
			break;

		case ARG_PALETTE:
			g_value_set_int(value, v4l2src->palette);
			break;

		case ARG_PALETTE_NAMES:
			g_value_set_pointer(value, gst_v4l2src_get_format_list(v4l2src));
			break;

		case ARG_FOURCC: {
			struct v4l2_fmtdesc *fmt = g_list_nth_data(GST_V4L2ELEMENT(v4l2src)->formats, v4l2src->palette);
			guint32 print_format = GUINT32_FROM_LE(fmt->pixelformat);
			gchar *print_format_str = (gchar *) &print_format;
			g_value_set_string(value, g_strndup(print_format_str, 4));
			break; }

		case ARG_FOURCC_LIST:
			g_value_set_pointer(value, gst_v4l2src_get_fourcc_list(v4l2src));
			break;

		case ARG_NUMBUFS:
			g_value_set_int(value, v4l2src->breq.count);
			break;

		case ARG_BUFSIZE:
			g_value_set_int(value, v4l2src->format.fmt.pix.sizeimage);
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
			v4l2src->first_timestamp = 0;
			/* buffer setup moved to capsnego */
			break;
		case GST_STATE_PAUSED_TO_PLAYING:
			/* queue all buffer, start streaming capture */
			if (!gst_v4l2src_capture_start(v4l2src))
				return GST_STATE_FAILURE;
			break;
		case GST_STATE_PLAYING_TO_PAUSED:
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


static GstBuffer*
gst_v4l2src_buffer_new (GstBufferPool *pool,
                        guint64        offset,
                        guint          size,
                        gpointer       user_data)
{
	GstBuffer *buffer;

	buffer = gst_buffer_new();
	if (!buffer) return NULL;
	/* TODO: add interlacing info to buffer as metadata (height>288 or 240 = topfieldfirst, else noninterlaced) */

	return buffer;
}


static GstBuffer*
gst_v4l2src_buffer_copy (GstBufferPool   *pool,
                         const GstBuffer *srcbuf,
                         gpointer         user_data)
{
	GstBuffer *buffer;

	buffer = gst_buffer_new();
	if (!buffer) return NULL;
	GST_BUFFER_DATA(buffer) = g_malloc(GST_BUFFER_SIZE(srcbuf));
	if (!GST_BUFFER_DATA(buffer)) return NULL;
	GST_BUFFER_MAXSIZE(buffer) = GST_BUFFER_SIZE(srcbuf);
	GST_BUFFER_SIZE(buffer) = GST_BUFFER_SIZE(srcbuf);
	memcpy(GST_BUFFER_DATA(buffer), GST_BUFFER_DATA(srcbuf), GST_BUFFER_SIZE(srcbuf));
	GST_BUFFER_TIMESTAMP(buffer) = GST_BUFFER_TIMESTAMP(srcbuf);

	return buffer;
}


static void
gst_v4l2src_buffer_free (GstBufferPool *pool,
                         GstBuffer     *buf,
                         gpointer      user_data)
{
	GstV4l2Src *v4l2src = GST_V4L2SRC(user_data);
	int n;

	for (n=0;n<v4l2src->breq.count;n++)
		if (GST_BUFFER_DATA(buf) == GST_V4L2ELEMENT(v4l2src)->buffer[n]) {
			gst_v4l2src_requeue_frame(v4l2src, n);
			return;
		}

	gst_element_error(GST_ELEMENT(v4l2src),
		"Couldn\'t find the buffer");
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
	GstElementFactory *factory;

	/* create an elementfactory for the v4l2src */
	factory = gst_element_factory_new("v4l2src", GST_TYPE_V4L2SRC,
				&gst_v4l2src_details);
	g_return_val_if_fail(factory != NULL, FALSE);

	src_template = gst_pad_template_new("src",
				GST_PAD_SRC,
				GST_PAD_ALWAYS,
				NULL);

	gst_element_factory_add_pad_template(factory, src_template);

	gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE(factory));

	return TRUE;
}


GstPluginDesc plugin_desc = {
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"v4l2src",
	plugin_init
};
