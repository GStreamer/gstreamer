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
#include <gstvideotestsrc.h>

#include <string.h>
#include <stdlib.h>

typedef struct paintinfo_struct paintinfo;
struct paintinfo_struct
{
  unsigned char *dest;
  unsigned char *yp, *up, *vp;
  int width;
  int height;
  int Y, U, V;
  void (*paint_hline) (paintinfo * p, int x, int y, int w);
};

struct fourcc_list_struct
{
  char *fourcc;
  int bitspp;
  void (*paint_setup) (paintinfo * p, char *dest);
  void (*paint_hline) (paintinfo * p, int x, int y, int w);
};
static struct fourcc_list_struct fourcc_list[];
static int n_fourccs;

static int paintrect_find_fourcc (int find_fourcc);


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
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (videotestsrc_src_template_factory,
			  "src",
			  GST_PAD_SRC,
			  GST_PAD_ALWAYS,
			  GST_CAPS_NEW ("videotestsrc_src",
					"video/raw",
					"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
					"height", GST_PROPS_INT_RANGE (0, G_MAXINT)
			  )
  );

static void gst_videotestsrc_class_init (GstVideotestsrcClass * klass);
static void gst_videotestsrc_init (GstVideotestsrc * videotestsrc);
static GstElementStateReturn gst_videotestsrc_change_state (GstElement * element);
static void gst_videotestsrc_set_clock (GstElement *element, GstClock *clock);

static void gst_videotestsrc_set_property (GObject * object, guint prop_id,
					   const GValue * value, GParamSpec * pspec);
static void gst_videotestsrc_get_property (GObject * object, guint prop_id, GValue * value,
					   GParamSpec * pspec);

static GstBuffer *gst_videotestsrc_get (GstPad * pad);

static GstElementClass *parent_class = NULL;

static void gst_videotestsrc_setup (GstVideotestsrc * v);
static void random_chars (unsigned char *dest, int nbytes);
static void gst_videotestsrc_smpte_yuv (GstVideotestsrc * v, unsigned char *dest, int w, int h);
static void gst_videotestsrc_smpte_RGB (GstVideotestsrc * v, unsigned char *dest, int w, int h);
#if 0
static void gst_videotestsrc_colors_yuv (GstVideotestsrc * v, unsigned char *dest, int w, int h);
#endif


static GType
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
      g_param_spec_int ("rate", "Rate", "Frame rate",
        1, 100, 30, G_PARAM_READWRITE));

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

  gst_object_replace ((GstObject **)&v->clock, (GstObject *)clock);
}

static GstPadLinkReturn
gst_videotestsrc_srcconnect (GstPad * pad, GstCaps * caps)
{
  GstVideotestsrc *videotestsrc;

  GST_DEBUG (0, "gst_videotestsrc_srcconnect");
  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

#if 0
  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }
#endif

  gst_caps_get_fourcc_int (caps, "format", &videotestsrc->format);
  gst_caps_get_int (caps, "width", &videotestsrc->width);
  gst_caps_get_int (caps, "height", &videotestsrc->height);

  GST_DEBUG (0, "format is 0x%08x\n", videotestsrc->format);

  printf ("videotestsrc: caps FOURCC 0x%08x, forced FOURCC 0x%08x\n",
	  videotestsrc->format, videotestsrc->forced_format);

  if (videotestsrc->forced_format && videotestsrc->format != videotestsrc->forced_format) {
    return GST_PAD_LINK_REFUSED;
  }

  printf ("videotestsrc: using FOURCC 0x%08x\n", videotestsrc->format);

  if (videotestsrc->format == GST_MAKE_FOURCC ('R', 'G', 'B', ' ')) {
    videotestsrc->make_image = gst_videotestsrc_smpte_RGB;
    videotestsrc->bpp = 16;
  } else {
    int index;

    index = paintrect_find_fourcc (videotestsrc->format);
    videotestsrc->make_image = gst_videotestsrc_smpte_yuv;
    /* videotestsrc->make_image = gst_videotestsrc_colors_yuv; */
    videotestsrc->bpp = fourcc_list[index].bitspp;
  }

  GST_DEBUG (0, "size %d x %d", videotestsrc->width, videotestsrc->height);

  return GST_PAD_LINK_OK;
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
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstCaps *
gst_videotestsrc_get_capslist (void)
{
  static GstCaps *capslist = NULL;
  GstCaps *caps;
  int i;

  if (capslist)
    return capslist;

  for (i = 0; i < n_fourccs; i++) {
    char *s = fourcc_list[i].fourcc;
    int fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);

    caps = GST_CAPS_NEW ("videotestsrc_filter",
			 "video/raw",
			 "format", GST_PROPS_FOURCC (fourcc),
			 "width", GST_PROPS_INT (640),
			 "height", GST_PROPS_INT (480));
    capslist = gst_caps_append (capslist, caps);
  }

  return capslist;
}

static GstCaps *
gst_videotestsrc_getcaps (GstPad * pad, GstCaps * caps)
{
  GstVideotestsrc *vts;

  vts = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  if (vts->forced_format != 0) {
    return GST_CAPS_NEW ("videotestsrc_filter",
			 "video/raw",
			 "format", GST_PROPS_FOURCC (vts->forced_format),
			 "width", GST_PROPS_INT (640),
			 "height", GST_PROPS_INT (480));
  } else {
    return gst_caps_ref (gst_videotestsrc_get_capslist ());
  }
}

static void
gst_videotestsrc_init (GstVideotestsrc * videotestsrc)
{
  GST_DEBUG (0, "gst_videotestsrc_init");

  videotestsrc->srcpad =
    gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (videotestsrc_src_template_factory), "src");
  /*gst_pad_set_negotiate_function(videotestsrc->srcpad,videotestsrc_negotiate_src); */
  gst_pad_set_getcaps_function (videotestsrc->srcpad, gst_videotestsrc_getcaps);
  gst_element_add_pad (GST_ELEMENT (videotestsrc), videotestsrc->srcpad);
  gst_pad_set_get_function (videotestsrc->srcpad, gst_videotestsrc_get);
  gst_pad_set_link_function (videotestsrc->srcpad, gst_videotestsrc_srcconnect);

  videotestsrc->width = 640;
  videotestsrc->height = 480;

  videotestsrc->rate = 30;
  videotestsrc->timestamp = 0;
  videotestsrc->interval = GST_SECOND / videotestsrc->rate;

  videotestsrc->pool = NULL;
}


static GstBuffer *
gst_videotestsrc_get (GstPad * pad)
{
  GstVideotestsrc *videotestsrc;
  gulong newsize;
  GstBuffer *buf;
  GstClockTimeDiff jitter = 0;

  GST_DEBUG (0, "gst_videotestsrc_get");

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  newsize = (videotestsrc->width * videotestsrc->height * videotestsrc->bpp) >> 3;

  GST_DEBUG (0, "size=%ld %dx%d", newsize, videotestsrc->width, videotestsrc->height);

  buf = NULL;
  if (videotestsrc->pool) {
    buf = gst_buffer_new_from_pool (videotestsrc->pool, 0, 0);
  }
  if (!buf) {
    buf = gst_buffer_new ();
    GST_BUFFER_SIZE (buf) = newsize;
    GST_BUFFER_DATA (buf) = g_malloc (newsize);
  }
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  videotestsrc->make_image (videotestsrc, (void *) GST_BUFFER_DATA (buf),
			    videotestsrc->width, videotestsrc->height);
  
  do {
    GstClockID id;

    videotestsrc->timestamp += videotestsrc->interval;
    GST_BUFFER_TIMESTAMP (buf) = videotestsrc->timestamp;

    if (videotestsrc->clock) {
      id = gst_clock_new_single_shot_id (videotestsrc->clock, GST_BUFFER_TIMESTAMP (buf));
      gst_element_clock_wait (GST_ELEMENT (videotestsrc), id, &jitter);
      gst_clock_id_free (id);
    }
  }
  while (jitter > 100 * GST_MSECOND);

  return buf;
}

static void
gst_videotestsrc_set_property (GObject * object, guint prop_id, const GValue * value,
			       GParamSpec * pspec)
{
  GstVideotestsrc *src;
  const gchar *s;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEOTESTSRC (object));
  src = GST_VIDEOTESTSRC (object);

  GST_DEBUG (0, "gst_videotestsrc_set_property");
  switch (prop_id) {
    case ARG_WIDTH:
      src->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->height = g_value_get_int (value);
      break;
    case ARG_FOURCC:
      s = g_value_get_string (value);
      if (strlen (s) == 4) {
	src->forced_format = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
	printf ("forcing FOURCC to 0x%08x\n", src->forced_format);
      }
      break;
    case ARG_RATE:
      src->rate = g_value_get_int (value);
      src->interval = GST_SECOND/src->rate;
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
      /* FIXME */
      /* g_value_set_int (value, src->forced_format); */
      break;
    case ARG_RATE:
      g_value_set_int (value, src->rate);
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



/* Non-GST specific stuff */

static void
gst_videotestsrc_setup (GstVideotestsrc * v)
{

}

static unsigned char
random_char (void)
{
  static unsigned int state;

  state *= 1103515245;
  state += 12345;
  return (state >> 16);
}

static void
random_chars (unsigned char *dest, int nbytes)
{
  int i;
  static unsigned int state;

  for (i = 0; i < nbytes; i++) {
    state *= 1103515245;
    state += 12345;
    dest[i] = (state >> 16);
  }
}

static void
memset_str2 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 2;
  }
}

static void
memset_str3 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 3;
  }
}

static void
memset_str4 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 4;
  }
}

static void
paint_rect_random (unsigned char *dest, int stride, int x, int y, int w, int h)
{
  unsigned char *d = dest + stride * y + x;
  int i;

  for (i = 0; i < h; i++) {
    random_chars (d, w);
    d += stride;
  }
}

#if 0
static void
paint_rect (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char color)
{
  unsigned char *d = dest + stride * y + x;
  int i;

  for (i = 0; i < h; i++) {
    memset (d, color, w);
    d += stride;
  }
}
#endif

static void
paint_rect_s2 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char col)
{
  unsigned char *d = dest + stride * y + x * 2;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp = col;
      dp += 2;
    }
    d += stride;
  }
}

static void
paint_rect2 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
{
  unsigned char *d = dest + stride * y + x * 2;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp++ = col[0];
      *dp++ = col[1];
    }
    d += stride;
  }
}
static void
paint_rect3 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
{
  unsigned char *d = dest + stride * y + x * 3;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp++ = col[0];
      *dp++ = col[1];
      *dp++ = col[2];
    }
    d += stride;
  }
}

static void
paint_rect_s4 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char col)
{
  unsigned char *d = dest + stride * y + x * 4;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp = col;
      dp += 4;
    }
    d += stride;
  }
}

enum {
	COLOR_WHITE = 0,
	COLOR_YELLOW,
	COLOR_CYAN,
	COLOR_GREEN,
	COLOR_MAGENTA,
	COLOR_RED,
	COLOR_BLUE,
	COLOR_BLACK,
	COLOR_NEG_I,
	COLOR_POS_Q,
	COLOR_SUPER_BLACK,
	COLOR_DARK_GREY,
};

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q, superblack, dark grey */
static int y_colors[] = { 255, 226, 179, 150, 105, 76, 29, 16, 16, 16, 0, 32 };
static int u_colors[] = { 128, 0, 170, 46, 212, 85, 255, 128, 198, 235, 128, 128 };
static int v_colors[] = { 128, 155, 0, 21, 235, 255, 107, 128, 21, 198, 128, 128 };

static void paint_setup_I420 (paintinfo * p, char *dest);
static void paint_setup_YV12 (paintinfo * p, char *dest);
static void paint_setup_YUY2 (paintinfo * p, char *dest);
static void paint_setup_UYVY (paintinfo * p, char *dest);
static void paint_setup_YVYU (paintinfo * p, char *dest);
static void paint_setup_Y800 (paintinfo * p, char *dest);
static void paint_setup_IMC1 (paintinfo * p, char *dest);
static void paint_setup_IMC2 (paintinfo * p, char *dest);
static void paint_setup_IMC3 (paintinfo * p, char *dest);
static void paint_setup_IMC4 (paintinfo * p, char *dest);

static void paint_hline_I420 (paintinfo * p, int x, int y, int w);
static void paint_hline_YUY2 (paintinfo * p, int x, int y, int w);
static void paint_hline_Y800 (paintinfo * p, int x, int y, int w);
static void paint_hline_IMC1 (paintinfo * p, int x, int y, int w);

static struct fourcc_list_struct fourcc_list[] = {
/* packed */
  {"YUY2", 16, paint_setup_YUY2, paint_hline_YUY2},
  {"UYVY", 16, paint_setup_UYVY, paint_hline_YUY2},
  {"Y422", 16, paint_setup_UYVY, paint_hline_YUY2},
  {"UYNV", 16, paint_setup_UYVY, paint_hline_YUY2},
  {"YVYU", 16, paint_setup_YVYU, paint_hline_YUY2},

  /* interlaced */
  /*{ "IUYV", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* inverted */
  /*{ "cyuv", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41P", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* interlaced */
  /*{ "IY41", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y211", 8, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41T", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "Y42P", 16, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "CLJR", 8, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "IYU1", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "IYU2", 24, paint_setup_YVYU, paint_hline_YUY2 }, */

/* planar */
  /* YVU9 */
  /* YUV9 */
  /* IF09 */
  /* YV12 */
  {"YV12", 12, paint_setup_YV12, paint_hline_I420},
  /* I420 */
  {"I420", 12, paint_setup_I420, paint_hline_I420},
  /* IYUV (same as I420) */
  {"IYUV", 12, paint_setup_I420, paint_hline_I420},
  /* NV12 */
  /* NV21 */
  /* IMC1 */
  {"IMC1", 16, paint_setup_IMC1, paint_hline_IMC1},
  /* IMC2 */
  {"IMC2", 12, paint_setup_IMC2, paint_hline_IMC1},
  /* IMC3 */
  {"IMC3", 16, paint_setup_IMC3, paint_hline_IMC1},
  /* IMC4 */
  {"IMC4", 12, paint_setup_IMC4, paint_hline_IMC1},
  /* CLPL */
  /* Y41B */
  /* Y42B */
  /* Y800 grayscale */
  {"Y800", 8, paint_setup_Y800, paint_hline_Y800},
  /* Y8   same as Y800 */
  {"Y8  ", 8, paint_setup_Y800, paint_hline_Y800},

  /*{ "IYU2", 24, paint_setup_YVYU, paint_hline_YUY2 }, */
};
static int n_fourccs = sizeof (fourcc_list) / sizeof (fourcc_list[0]);

static int
paintrect_find_fourcc (int find_fourcc)
{
  int i;

  for (i = 0; i < n_fourccs; i++) {
    char *s;
    int fourcc;

    s = fourcc_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    if (find_fourcc == fourcc) {
      return i;
    }
  }
  return -1;
}

static void
gst_videotestsrc_smpte_yuv (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int index;
  int i;
  int y1, y2;
  int j;
  paintinfo pi;
  paintinfo *p = &pi;

  p->width = w;
  p->height = h;
  index = paintrect_find_fourcc (v->format);
  if (index < 0)
    return;

  fourcc_list[index].paint_setup (p, dest);
  p->paint_hline = fourcc_list[index].paint_hline;

  y1 = 2 * h / 3;
  y2 = h * 0.75;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;

    p->Y = y_colors[i];
    p->U = u_colors[i];
    p->V = v_colors[i];
    for (j = 0; j < y1; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* inverse blue bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    int k;

    if (i & 1) {
      k = 7;
    } else {
      k = 6 - i;
    }
    p->Y = y_colors[k];
    p->U = u_colors[k];
    p->V = v_colors[k];
    for (j = y1; j < y2; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* -I, white, Q regions */
  for (i = 0; i < 3; i++) {
    int x1 = i * w / 6;
    int x2 = (i + 1) * w / 6;
    int k;

    if (i == 0) {
      k = 8;
    } else if (i == 1) {
      k = 0;
    } else
      k = 9;

    p->Y = y_colors[k];
    p->U = u_colors[k];
    p->V = v_colors[k];

    for (j = y2; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* superblack, black, dark grey */
  for (i = 0; i < 3; i++) {
    int x1 = w/2 + i * w / 12;
    int x2 = w/2 + (i + 1) * w / 12;
    int k;

    if (i == 0) {
      k = COLOR_SUPER_BLACK;
    } else if (i == 1) {
      k = COLOR_BLACK;
    } else
      k = COLOR_DARK_GREY;

    p->Y = y_colors[k];
    p->U = u_colors[k];
    p->V = v_colors[k];

    for (j = y2; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  {
    int x1 = w*3 / 4;

    p->U = u_colors[0];
    p->V = v_colors[0];

    for (i = x1; i < w; i++) {
      for (j = y2; j < h; j++) {
	p->Y = random_char ();
	p->paint_hline (p, i, j, 1);
      }
    }

  }
}

static void
paint_setup_I420 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + p->width * p->height / 4;
}

static void
paint_hline_I420 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->width;
  int offset1 = (y / 2) * (p->width / 2);

  memset (p->yp + offset + x, p->Y, w);
  memset (p->up + offset1 + x1, p->U, x2 - x1);
  memset (p->vp + offset1 + x1, p->V, x2 - x1);
}

static void
paint_setup_YV12 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height + p->width * p->height / 4;
  p->vp = dest + p->width * p->height;
}

static void
paint_setup_YUY2 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 1;
  p->vp = dest + 3;
}

static void
paint_setup_UYVY (paintinfo * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest;
  p->vp = dest + 2;
}

static void
paint_setup_YVYU (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 3;
  p->vp = dest + 1;
}

static void
paint_hline_YUY2 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset;

  offset = y * p->width * 2;
  memset_str2 (p->yp + offset + x * 2, p->Y, w);
  memset_str4 (p->up + offset + x1 * 4, p->U, x2 - x1);
  memset_str4 (p->vp + offset + x1 * 4, p->V, x2 - x1);
}

static void
paint_setup_Y800 (paintinfo * p, char *dest)
{
  p->yp = dest;
}

static void
paint_hline_Y800 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->width;

  memset (p->yp + offset + x, p->Y, w);
}

static void
paint_setup_IMC1 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + p->width * p->height / 2;
}

static void
paint_setup_IMC2 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height;
  p->up = dest + p->width * p->height + p->width / 2;
}

static void
paint_setup_IMC3 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height + p->width * p->height / 2;
  p->vp = dest + p->width * p->height;
}

static void
paint_setup_IMC4 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height + p->width / 2;
  p->up = dest + p->width * p->height;
}

static void
paint_hline_IMC1 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->width;
  int offset1 = (y / 2) * p->width;

  memset (p->yp + offset + x, p->Y, w);
  memset (p->up + offset1 + x1, p->U, x2 - x1);
  memset (p->vp + offset1 + x1, p->V, x2 - x1);
}



/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q */
static int r_colors[] = { 255, 255, 0, 0, 255, 255, 0, 0, 0, 0 };
static int g_colors[] = { 255, 255, 255, 255, 0, 0, 0, 0, 0, 128 };
static int b_colors[] = { 255, 0, 255, 0, 255, 0, 255, 0, 128, 255 };

static void
gst_videotestsrc_smpte_RGB (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int y1, y2;

  y1 = h * 2 / 3;
  y2 = h * 0.75;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];

    col[0] = (g_colors[i] & 0xe0) | (b_colors[i] >> 3);
    col[1] = (r_colors[i] & 0xf8) | (g_colors[i] >> 5);
    paint_rect2 (dest, w * 2, x1, 0, x2 - x1, y1, col);
  }

  /* inverse blue bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];
    int k;

    if (i & 1) {
      k = 7;
    } else {
      k = 6 - i;
    }
    col[0] = (g_colors[k] & 0xe0) | (b_colors[k] >> 3);
    col[1] = (r_colors[k] & 0xf8) | (g_colors[k] >> 5);
    paint_rect2 (dest, w * 2, x1, y1, x2 - x1, y2 - y1, col);
  }

  /* -I, white, Q regions */
  for (i = 0; i < 3; i++) {
    int x1 = i * w / 6;
    int x2 = (i + 1) * w / 6;
    unsigned char col[2];
    int k;

    if (i == 0) {
      k = 8;
    } else if (i == 1) {
      k = 0;
    } else
      k = 9;

    col[0] = (g_colors[k] & 0xe0) | (b_colors[k] >> 3);
    col[1] = (r_colors[k] & 0xf8) | (g_colors[k] >> 5);
    paint_rect2 (dest, w * 2, x1, y2, x2 - x1, h - y2, col);
  }

  {
    int x1 = w / 2;
    int x2 = w - 1;

    paint_rect_random (dest, w * 2, x1 * 2, y2, (x2 - x1) * 2, h - y2);
  }
}

#ifdef unused
static void
gst_videotestsrc_colors_yuv (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int index;
  int i;
  int j;
  int k;
  paintinfo pi;
  paintinfo *p = &pi;
  static int static_y = 0;

  p->width = w;
  p->height = h;
  index = paintrect_find_fourcc (v->format);
  if (index < 0)
    return;

  fourcc_list[index].paint_setup (p, dest);
  p->paint_hline = fourcc_list[index].paint_hline;

  /* color bars */
  for (i = 0; i < 16; i++) {
    int x1 = (i * w) >> 4;
    int x2 = ((i + 1) * w) >> 4;

    p->Y = static_y;
    p->U = i * 17;
    for (j = 0; j < 16; j++) {
      int y1 = (j * h) >> 4;
      int y2 = ((j + 1) * h) >> 4;

      p->V = j * 17;
      for (k = y1; k < y2; k++) {
	p->paint_hline (p, x1, k, (x2 - x1));
      }
    }
  }

  static_y += 17;
  if (static_y >= 256)
    static_y = 0;
}
#endif

