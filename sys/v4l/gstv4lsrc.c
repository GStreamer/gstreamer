/* GStreamer
 *
 * gstv4lsrc.c: BT8x8/V4L source element
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 e Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include "v4lsrc_calls.h"
#include <sys/ioctl.h>

/* elementfactory information */
static GstElementDetails gst_v4lsrc_details =
GST_ELEMENT_DETAILS ("Video (video4linux/raw) Source",
    "Source/Video",
    "Reads raw frames from a video4linux (BT8x8) device",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

GST_DEBUG_CATEGORY (v4lsrc_debug);
#define GST_CAT_DEFAULT v4lsrc_debug

/* V4lSrc signals and args */
enum
{
  /* FILL ME */
  SIGNAL_FRAME_CAPTURE,
  SIGNAL_FRAME_DROP,
  SIGNAL_FRAME_INSERT,
  LAST_SIGNAL
};

#define DEFAULT_SYNC_MODE GST_V4LSRC_SYNC_MODE_CLOCK
#define DEFAULT_COPY_MODE FALSE
/* arguments */
enum
{
  ARG_0,
  ARG_NUMBUFS,
  ARG_BUFSIZE,
  ARG_SYNC_MODE,
  ARG_COPY_MODE,
  ARG_AUTOPROBE,
  ARG_AUTOPROBE_FPS,
  ARG_LATENCY_OFFSET
};

GST_FORMATS_FUNCTION (GstPad *, gst_v4lsrc_get_formats,
    GST_FORMAT_TIME, GST_FORMAT_DEFAULT);
GST_QUERY_TYPE_FUNCTION (GstPad *, gst_v4lsrc_get_query_types,
    GST_QUERY_POSITION);

#define GST_TYPE_V4LSRC_SYNC_MODE (gst_v4lsrc_sync_mode_get_type())
static GType
gst_v4lsrc_sync_mode_get_type (void)
{
  static GType v4lsrc_sync_mode_type = 0;
  static GEnumValue v4lsrc_sync_mode[] = {
    {GST_V4LSRC_SYNC_MODE_CLOCK, "0", "Sync to the pipeline clock"},
    {GST_V4LSRC_SYNC_MODE_PRIVATE_CLOCK, "1", "Sync to a private clock"},
    {GST_V4LSRC_SYNC_MODE_FIXED_FPS, "2", "Use Fixed fps"},
    {GST_V4LSRC_SYNC_MODE_NONE, "3", "No Sync"},
    {0, NULL, NULL},
  };

  if (!v4lsrc_sync_mode_type) {
    v4lsrc_sync_mode_type =
        g_enum_register_static ("GstV4lSrcSyncMode", v4lsrc_sync_mode);
  }
  return v4lsrc_sync_mode_type;
}

/* structure for buffer private data referencing element and frame number */
typedef struct
{
  GstV4lSrc *v4lsrc;
  int num;
}
v4lsrc_private_t;

/* init functions */
static void gst_v4lsrc_base_init (gpointer g_class);
static void gst_v4lsrc_class_init (GstV4lSrcClass * klass);
static void gst_v4lsrc_init (GstV4lSrc * v4lsrc);

/* parent class virtual functions */
static void gst_v4lsrc_open (GstElement * element, const gchar * device);
static void gst_v4lsrc_close (GstElement * element, const gchar * device);

/* pad/info functions */
static gboolean gst_v4lsrc_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_v4lsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

/* buffer functions */
static GstPadLinkReturn gst_v4lsrc_src_link (GstPad * pad,
    const GstCaps * caps);
static GstCaps *gst_v4lsrc_fixate (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_v4lsrc_getcaps (GstPad * pad);
static GstData *gst_v4lsrc_get (GstPad * pad);

/* get/set params */
static void gst_v4lsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* state handling */
static GstElementStateReturn gst_v4lsrc_change_state (GstElement * element);

/* set_clock function for a/V sync */
static void gst_v4lsrc_set_clock (GstElement * element, GstClock * clock);

/* requeue buffer if it's back available */
static void gst_v4lsrc_buffer_free (GstBuffer * buffer);

static GstElementClass *parent_class = NULL;
static guint gst_v4lsrc_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lsrc_get_type (void)
{
  static GType v4lsrc_type = 0;

  if (!v4lsrc_type) {
    static const GTypeInfo v4lsrc_info = {
      sizeof (GstV4lSrcClass),
      gst_v4lsrc_base_init,
      NULL,
      (GClassInitFunc) gst_v4lsrc_class_init,
      NULL,
      NULL,
      sizeof (GstV4lSrc),
      0,
      (GInstanceInitFunc) gst_v4lsrc_init,
      NULL
    };

    v4lsrc_type =
        g_type_register_static (GST_TYPE_V4LELEMENT, "GstV4lSrc", &v4lsrc_info,
        0);
  }
  return v4lsrc_type;
}

static void
gst_v4lsrc_base_init (gpointer g_class)
{
  GstPadTemplate *src_template;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_v4lsrc_details);

  src_template = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_new_any ());

  gst_element_class_add_pad_template (gstelement_class, src_template);
}

static void
gst_v4lsrc_class_init (GstV4lSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstV4lElementClass *v4lelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  v4lelement_class = (GstV4lElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_V4LELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUMBUFS,
      g_param_spec_int ("num_buffers", "Num Buffers", "Number of buffers",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
      g_param_spec_int ("buffer_size", "Buffer Size", "Size of buffers",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC_MODE,
      g_param_spec_enum ("sync_mode", "Sync mode",
          "Method to use for timestamping captured frames",
          GST_TYPE_V4LSRC_SYNC_MODE, DEFAULT_SYNC_MODE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COPY_MODE,
      g_param_spec_boolean ("copy_mode", "Copy mode",
          "Don't send out HW buffers, send copy instead", DEFAULT_COPY_MODE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_AUTOPROBE,
      g_param_spec_boolean ("autoprobe", "Autoprobe",
          "Whether the device should be probed for all possible features",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_AUTOPROBE_FPS,
      g_param_spec_boolean ("autoprobe-fps", "Autoprobe FPS",
          "Whether the device should be probed for framerates",
          TRUE, G_PARAM_READWRITE));
  /* FIXME: this should have been a 64 bit int instead, because 3 seconds
   * overflows 32 bit nanosecond values.  However, this is an ABI change,
   * so we're saving it for 0.9, and then we can also rename the property
   * to something that makes more sense, like, timestamp-offset */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LATENCY_OFFSET,
      g_param_spec_int ("latency-offset", "Latency offset",
          "A latency offset subtracted from timestamps set on buffers (in ns)",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));

  /* signals */
  gst_v4lsrc_signals[SIGNAL_FRAME_CAPTURE] =
      g_signal_new ("frame-capture", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4lSrcClass, frame_capture), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lsrc_signals[SIGNAL_FRAME_DROP] =
      g_signal_new ("frame-drop", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstV4lSrcClass, frame_drop), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_v4lsrc_signals[SIGNAL_FRAME_INSERT] =
      g_signal_new ("frame-insert", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstV4lSrcClass, frame_insert), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  GST_DEBUG_CATEGORY_INIT (v4lsrc_debug, "v4lsrc", 0, "V4L source element");
  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  gstelement_class->change_state = gst_v4lsrc_change_state;

  gstelement_class->set_clock = gst_v4lsrc_set_clock;

  v4lelement_class->open = gst_v4lsrc_open;
  v4lelement_class->close = gst_v4lsrc_close;
}

static void
gst_v4lsrc_init (GstV4lSrc * v4lsrc)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (v4lsrc);

  GST_FLAG_SET (GST_ELEMENT (v4lsrc), GST_ELEMENT_THREAD_SUGGESTED);

  v4lsrc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (v4lsrc), v4lsrc->srcpad);

  gst_pad_set_get_function (v4lsrc->srcpad, gst_v4lsrc_get);
  gst_pad_set_getcaps_function (v4lsrc->srcpad, gst_v4lsrc_getcaps);
  gst_pad_set_fixate_function (v4lsrc->srcpad, gst_v4lsrc_fixate);
  gst_pad_set_link_function (v4lsrc->srcpad, gst_v4lsrc_src_link);
  gst_pad_set_convert_function (v4lsrc->srcpad, gst_v4lsrc_src_convert);
  gst_pad_set_formats_function (v4lsrc->srcpad, gst_v4lsrc_get_formats);
  gst_pad_set_query_function (v4lsrc->srcpad, gst_v4lsrc_src_query);
  gst_pad_set_query_type_function (v4lsrc->srcpad, gst_v4lsrc_get_query_types);

  v4lsrc->buffer_size = 0;

  /* no clock */
  v4lsrc->clock = NULL;

  /* no colourspaces */
  v4lsrc->colourspaces = NULL;

  v4lsrc->syncmode = DEFAULT_SYNC_MODE;
  v4lsrc->copy_mode = DEFAULT_COPY_MODE;

  v4lsrc->is_capturing = FALSE;
  v4lsrc->autoprobe = TRUE;
  v4lsrc->autoprobe_fps = TRUE;

  v4lsrc->latency_offset = 0;

  v4lsrc->fps_list = NULL;
}

static gint all_palettes[] = {
  VIDEO_PALETTE_YUV422,
  VIDEO_PALETTE_YUV420P,
  VIDEO_PALETTE_UYVY,
  VIDEO_PALETTE_YUV411P,
  VIDEO_PALETTE_YUV422P,
  VIDEO_PALETTE_YUV410P,
  VIDEO_PALETTE_YUV411,
  VIDEO_PALETTE_RGB555,
  VIDEO_PALETTE_RGB565,
  VIDEO_PALETTE_RGB24,
  VIDEO_PALETTE_RGB32,
  -1
};

static void
gst_v4lsrc_open (GstElement * element, const gchar * device)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (element);
  GstV4lElement *v4l = GST_V4LELEMENT (v4lsrc);
  gint width = v4l->vcap.minwidth;
  gint height = v4l->vcap.minheight;
  gint i;

  GST_DEBUG_OBJECT (v4lsrc, "Checking supported palettes");
  for (i = 0; all_palettes[i] != -1; i++) {
    /* try palette out */
    if (!gst_v4lsrc_try_capture (v4lsrc, width, height, all_palettes[i]))
      continue;
    GST_DEBUG_OBJECT (v4lsrc, "Added palette %d (%s) to supported list",
        all_palettes[i], gst_v4lsrc_palette_name (all_palettes[i]));
    v4lsrc->colourspaces = g_list_append (v4lsrc->colourspaces,
        GINT_TO_POINTER (all_palettes[i]));
  }
  GST_DEBUG_OBJECT (v4lsrc, "%d palette(s) supported",
      g_list_length (v4lsrc->colourspaces));
}

static void
gst_v4lsrc_close (GstElement * element, const gchar * device)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (element);

  g_list_free (v4lsrc->colourspaces);
  v4lsrc->colourspaces = NULL;
}

/* get a list of possible framerates
 * this is only done for webcams;
 * other devices return NULL here.
 * this function takes a LONG time to execute.
 */
static GValue *
gst_v4lsrc_get_fps_list (GstV4lSrc * v4lsrc)
{
  gint fps_index;
  gfloat fps;
  struct video_window *vwin = &GST_V4LELEMENT (v4lsrc)->vwin;
  GstV4lElement *v4lelement = GST_V4LELEMENT (v4lsrc);

  /* check if we have vwin window properties giving a framerate,
   * as is done for webcams
   * See http://www.smcc.demon.nl/webcam/api.html
   * which is used for the Philips and qce-ga drivers */
  fps_index = (vwin->flags >> 16) & 0x3F;       /* 6 bit index for framerate */

  /* webcams have a non-zero fps_index */
  if (fps_index == 0) {
    GST_DEBUG_OBJECT (v4lsrc, "fps_index is 0, no webcam");
    return NULL;
  }
  GST_DEBUG_OBJECT (v4lsrc, "fps_index is %d, so webcam", fps_index);

  {
    gfloat current_fps;
    int i;
    GValue *list = NULL;
    GValue value = { 0 };

    /* webcam detected, so try all framerates and return a list */

    list = g_new0 (GValue, 1);
    g_value_init (list, GST_TYPE_LIST);

    /* index of 16 corresponds to 15 fps */
    current_fps = fps_index * 15.0 / 16;
    GST_DEBUG_OBJECT (v4lsrc, "device reports fps of %.4f", current_fps);
    for (i = 0; i < 63; ++i) {
      /* set bits 16 to 21 to 0 */
      vwin->flags &= (0x3F00 - 1);
      /* set bits 16 to 21 to the index */
      vwin->flags |= i << 16;
      if (gst_v4l_set_window_properties (v4lelement)) {
        /* setting it succeeded.  FIXME: get it and check. */
        fps = i * 15.0 / 16;
        g_value_init (&value, G_TYPE_DOUBLE);
        g_value_set_double (&value, fps);
        gst_value_list_append_value (list, &value);
        g_value_unset (&value);
      }
    }
    /* FIXME: set back the original fps_index */
    vwin->flags &= (0x3F00 - 1);
    vwin->flags |= fps_index << 16;
    gst_v4l_set_window_properties (v4lelement);
    return list;
  }
  return NULL;
}

static gboolean
gst_v4lsrc_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstV4lSrc *v4lsrc;
  gdouble fps;

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if ((fps = gst_v4lsrc_get_fps (v4lsrc)) == 0)
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
gst_v4lsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  gdouble fps;

  if ((fps = gst_v4lsrc_get_fps (v4lsrc)) == 0)
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = v4lsrc->handled * GST_SECOND / fps;
          break;
        case GST_FORMAT_DEFAULT:
          *value = v4lsrc->handled;
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
gst_v4lsrc_palette_to_caps (int palette)
{
  guint32 fourcc;
  GstCaps *caps;

  switch (palette) {
    case VIDEO_PALETTE_YUV422:
    case VIDEO_PALETTE_YUYV:
      fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case VIDEO_PALETTE_YUV420P:
      fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case VIDEO_PALETTE_UYVY:
      fourcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      break;
    case VIDEO_PALETTE_YUV411P:
      fourcc = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
      break;
    case VIDEO_PALETTE_YUV411:
      fourcc = GST_MAKE_FOURCC ('Y', '4', '1', 'P');
      break;
    case VIDEO_PALETTE_YUV422P:
      fourcc = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
      break;
    case VIDEO_PALETTE_YUV410P:
      fourcc = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
      break;
    case VIDEO_PALETTE_RGB555:
    case VIDEO_PALETTE_RGB565:
    case VIDEO_PALETTE_RGB24:
    case VIDEO_PALETTE_RGB32:
      fourcc = GST_MAKE_FOURCC ('R', 'G', 'B', ' ');
      break;
    default:
      return NULL;
  }

  if (fourcc == GST_MAKE_FOURCC ('R', 'G', 'B', ' ')) {
    switch (palette) {
      case VIDEO_PALETTE_RGB555:
        caps = gst_caps_from_string ("video/x-raw-rgb, "
            "bpp = (int) 16, "
            "depth = (int) 15, "
            "endianness = (int) BYTE_ORDER, "
            "red_mask = 0x7c00, " "green_mask = 0x03e0, " "blue_mask = 0x001f");
        break;
      case VIDEO_PALETTE_RGB565:
        caps = gst_caps_from_string ("video/x-raw-rgb, "
            "bpp = (int) 16, "
            "depth = (int) 16, "
            "endianness = (int) BYTE_ORDER, "
            "red_mask = 0xf800, " "green_mask = 0x07f0, " "blue_mask = 0x001f");
        break;
      case VIDEO_PALETTE_RGB24:
        caps = gst_caps_from_string ("video/x-raw-rgb, "
            "bpp = (int) 24, "
            "depth = (int) 24, "
            "endianness = (int) BIG_ENDIAN, "
            "red_mask = 0x0000FF, "
            "green_mask = 0x00FF00, " "blue_mask = 0xFF0000");
        break;
      case VIDEO_PALETTE_RGB32:
        caps = gst_caps_from_string ("video/x-raw-rgb, "
            "bpp = (int) 24, "
            "depth = (int) 32, "
            "endianness = (int) BIG_ENDIAN, "
            "red_mask = 0xFF000000, "
            "green_mask = 0x00FF0000, " "blue_mask = 0x0000FF00");
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }
  } else {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  }

  return caps;
}

static GstCaps *
gst_v4lsrc_get_any_caps ()
{
  gint i;
  GstCaps *caps = gst_caps_new_empty (), *one;

  for (i = 0; all_palettes[i] != -1; i++) {
    one = gst_v4lsrc_palette_to_caps (all_palettes[i]);
    gst_caps_append (caps, one);
  }

  return caps;
}

static GstPadLinkReturn
gst_v4lsrc_src_link (GstPad * pad, const GstCaps * vscapslist)
{
  GstV4lSrc *v4lsrc;
  guint32 fourcc;
  gint bpp, depth, w, h, palette = -1;
  gdouble fps;
  GstStructure *structure;
  gboolean was_capturing;
  struct video_window *vwin;

  /* if your fourcc stays sexy then something is wrong */
  fourcc = GST_MAKE_FOURCC ('S', 'E', 'X', 'Y');

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));
  vwin = &GST_V4LELEMENT (v4lsrc)->vwin;
  was_capturing = v4lsrc->is_capturing;

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc))) {
    if (was_capturing) {
      if (!gst_v4lsrc_capture_stop (v4lsrc))
        return GST_PAD_LINK_REFUSED;
    }
    if (!gst_v4lsrc_capture_deinit (v4lsrc))
      return GST_PAD_LINK_REFUSED;
  } else if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    return GST_PAD_LINK_DELAYED;
  }

  /* FIXME: setting the first one is just stupid.  We should loop */
  structure = gst_caps_get_structure (vscapslist, 0);

  if (!strcmp (gst_structure_get_name (structure), "video/x-raw-yuv"))
    gst_structure_get_fourcc (structure, "format", &fourcc);
  else
    fourcc = GST_MAKE_FOURCC ('R', 'G', 'B', ' ');

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  gst_structure_get_double (structure, "framerate", &fps);
  GST_DEBUG_OBJECT (v4lsrc, "linking with %dx%d at %f fps", w, h, fps);

  /* set framerate if it's not already correct */
  if (fps != gst_v4lsrc_get_fps (v4lsrc)) {
    int fps_index = fps / 15.0 * 16;

    GST_DEBUG_OBJECT (v4lsrc, "Trying to set fps index %d", fps_index);
    /* set bits 16 to 21 to 0 */
    vwin->flags &= (0x3F00 - 1);
    /* set bits 16 to 21 to the index */
    vwin->flags |= fps_index << 16;
    if (!gst_v4l_set_window_properties (GST_V4LELEMENT (v4lsrc))) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      palette = VIDEO_PALETTE_YUV420P;
      v4lsrc->buffer_size = ((w + 1) & ~1) * ((h + 1) & ~1) * 1.5;
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      palette = VIDEO_PALETTE_YUV422;
      v4lsrc->buffer_size = ((w + 1) & ~1) * h * 2;
      break;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      palette = VIDEO_PALETTE_UYVY;
      v4lsrc->buffer_size = ((w + 1) & ~1) * h * 2;
      break;
    case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
      palette = VIDEO_PALETTE_YUV411P;
      v4lsrc->buffer_size = ((w + 3) & ~3) * h * 1.5;
      break;
    case GST_MAKE_FOURCC ('Y', '4', '1', 'P'):
      palette = VIDEO_PALETTE_YUV411;
      v4lsrc->buffer_size = ((w + 3) & ~3) * h * 1.5;
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
      palette = VIDEO_PALETTE_YUV410P;
      v4lsrc->buffer_size = ((w + 3) & ~3) * ((h + 3) & ~3) * 1.125;
      break;
    case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
      palette = VIDEO_PALETTE_YUV422P;
      v4lsrc->buffer_size = ((w + 1) & ~1) * h * 2;
      break;
    case GST_MAKE_FOURCC ('R', 'G', 'B', ' '):
      gst_structure_get_int (structure, "depth", &depth);
      switch (depth) {
        case 15:
          palette = VIDEO_PALETTE_RGB555;
          v4lsrc->buffer_size = w * h * 2;
          break;
        case 16:
          palette = VIDEO_PALETTE_RGB565;
          v4lsrc->buffer_size = w * h * 2;
          break;
        case 24:
          gst_structure_get_int (structure, "bpp", &bpp);
          switch (bpp) {
            case 24:
              palette = VIDEO_PALETTE_RGB24;
              v4lsrc->buffer_size = w * h * 3;
              break;
            case 32:
              palette = VIDEO_PALETTE_RGB32;
              v4lsrc->buffer_size = w * h * 4;
              break;
            default:
              break;
          }
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  if (palette == -1) {
    GST_WARNING_OBJECT (v4lsrc, "palette for fourcc " GST_FOURCC_FORMAT
        " is -1, refusing link", GST_FOURCC_ARGS (fourcc));
    return GST_PAD_LINK_REFUSED;
  }

  GST_DEBUG_OBJECT (v4lsrc, "trying to set_capture %dx%d, palette %d",
      w, h, palette);
  /* this only fills in v4lsrc->mmap values */
  if (!gst_v4lsrc_set_capture (v4lsrc, w, h, palette)) {
    GST_WARNING_OBJECT (v4lsrc, "could not set_capture %dx%d, palette %d",
        w, h, palette);
    return GST_PAD_LINK_REFUSED;
  }

  /* first try the negotiated settings using try_capture */
  if (!gst_v4lsrc_try_capture (v4lsrc, w, h, palette)) {
    GST_DEBUG_OBJECT (v4lsrc, "failed trying palette %d for %dx%d", palette,
        w, h);
    return GST_PAD_LINK_REFUSED;
  }

  if (!gst_v4lsrc_capture_init (v4lsrc))
    return GST_PAD_LINK_REFUSED;

  if (was_capturing || GST_STATE (v4lsrc) == GST_STATE_PLAYING) {
    if (!gst_v4lsrc_capture_start (v4lsrc))
      return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_v4lsrc_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;
  int i;
  int targetwidth, targetheight;
  GstV4lSrc *v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));
  struct video_capability *vcap = &GST_V4LELEMENT (v4lsrc)->vcap;
  struct video_window *vwin = &GST_V4LELEMENT (v4lsrc)->vwin;

  /* if device not yet open, no way to fixate */
  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    GST_DEBUG_OBJECT (v4lsrc, "device closed, cannot fixate");
    return NULL;
  }

  GST_DEBUG_OBJECT (v4lsrc, "device reported w: %d-%d, h: %d-%d",
      vcap->minwidth, vcap->maxwidth, vcap->minheight, vcap->maxheight);
  targetwidth = vcap->minwidth;
  targetheight = vcap->minheight;

  /* if we can get the current vwin settings, we use those to fixate */
  if (!gst_v4l_get_capabilities (GST_V4LELEMENT (v4lsrc)))
    GST_DEBUG_OBJECT (v4lsrc, "failed getting capabilities");
  else {
    targetwidth = vwin->width;
    targetheight = vwin->height;
  }
  GST_DEBUG_OBJECT (v4lsrc, "targetting %dx%d", targetwidth, targetheight);

  newcaps = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (newcaps, i);

    if (gst_caps_structure_fixate_field_nearest_int (structure, "width",
            targetwidth)) {
      return newcaps;
    }
    if (gst_caps_structure_fixate_field_nearest_int (structure, "height",
            targetheight)) {
      return newcaps;
    }
  }
  gst_caps_free (newcaps);
  return NULL;
}

static GstCaps *
gst_v4lsrc_getcaps (GstPad * pad)
{
  GstCaps *list;
  GstV4lSrc *v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));
  struct video_capability *vcap = &GST_V4LELEMENT (v4lsrc)->vcap;
  gdouble fps;
  GList *item;

  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    return gst_v4lsrc_get_any_caps ();
  }
  if (!v4lsrc->autoprobe) {
    /* FIXME: query current caps and return those, with _any appended */
    return gst_v4lsrc_get_any_caps ();
  }
  fps = gst_v4lsrc_get_fps (v4lsrc);

  list = gst_caps_new_empty ();
  for (item = v4lsrc->colourspaces; item != NULL; item = item->next) {
    GstCaps *one;

    one = gst_v4lsrc_palette_to_caps (GPOINTER_TO_INT (item->data));
    if (!one) {
      GST_WARNING_OBJECT (v4lsrc, "Palette %d gave no caps\n",
          GPOINTER_TO_INT (item->data));
      continue;
    }

    GST_DEBUG_OBJECT (v4lsrc,
        "Device reports w: %d-%d, h: %d-%d, fps: %f for palette %d",
        vcap->minwidth, vcap->maxwidth, vcap->minheight, vcap->maxheight, fps,
        GPOINTER_TO_INT (item->data));

    if (vcap->minwidth < vcap->maxwidth) {
      gst_caps_set_simple (one, "width", GST_TYPE_INT_RANGE, vcap->minwidth,
          vcap->maxwidth, NULL);
    } else {
      gst_caps_set_simple (one, "width", G_TYPE_INT, vcap->minwidth, NULL);
    }
    if (vcap->minheight < vcap->maxheight) {
      gst_caps_set_simple (one, "height", GST_TYPE_INT_RANGE, vcap->minheight,
          vcap->maxheight, NULL);
    } else {
      gst_caps_set_simple (one, "height", G_TYPE_INT, vcap->minheight, NULL);
    }

    if (v4lsrc->autoprobe_fps) {
      if (v4lsrc->fps_list) {
        GstStructure *structure = gst_caps_get_structure (one, 0);

        gst_structure_set_value (structure, "framerate", v4lsrc->fps_list);
      } else {
        gst_caps_set_simple (one, "framerate", G_TYPE_DOUBLE, fps, NULL);
      }
    } else {
      gst_caps_set_simple (one, "framerate", GST_TYPE_DOUBLE_RANGE,
          (gdouble) 1.0, (gdouble) 100.0, NULL);
    }

    GST_DEBUG_OBJECT (v4lsrc, "caps: %" GST_PTR_FORMAT, one);
    gst_caps_append (list, one);
  }

  return list;
}


static GstData *
gst_v4lsrc_get (GstPad * pad)
{
  GstV4lSrc *v4lsrc;
  GstBuffer *buf;
  gint num;
  gdouble fps = 0.;
  v4lsrc_private_t *v4lsrc_private = NULL;
  GstClockTime now, until;
  gboolean fixed_fps;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));
  fps = gst_v4lsrc_get_fps (v4lsrc);

  fixed_fps = v4lsrc->syncmode == GST_V4LSRC_SYNC_MODE_FIXED_FPS;

  if (fixed_fps && fps == 0.0)
    return NULL;

  if (v4lsrc->need_writes > 0) {
    /* use last frame */
    num = v4lsrc->last_frame;
    v4lsrc->need_writes--;
  } else if (v4lsrc->clock && fixed_fps) {
    GstClockTime time;
    gboolean have_frame = FALSE;

    do {
      /* by default, we use the frame once */
      v4lsrc->need_writes = 1;

      /* grab a frame from the device */
      if (!gst_v4lsrc_grab_frame (v4lsrc, &num))
        return NULL;

      v4lsrc->last_frame = num;
      time = v4lsrc->timestamp_sync - v4lsrc->substract_time;

      /* decide how often we're going to write the frame - set
       * v4lsrc->need_writes to (that-1) and have_frame to TRUE
       * if we're going to write it - else, just continue.
       * 
       * time is generally the system or audio clock. Let's
       * say that we've written one second of audio, then we want
       * to have written one second of video too, within the same
       * timeframe. This means that if time - begin_time = X sec,
       * we want to have written X*fps frames. If we've written
       * more - drop, if we've written less - dup... */
      if (v4lsrc->handled * (GST_SECOND / fps) - time >
          1.5 * (GST_SECOND / fps)) {
        /* yo dude, we've got too many frames here! Drop! DROP! */
        v4lsrc->need_writes--;  /* -= (v4lsrc->handled - (time / fps)); */
        g_signal_emit (G_OBJECT (v4lsrc),
            gst_v4lsrc_signals[SIGNAL_FRAME_DROP], 0);
      } else if (v4lsrc->handled * (GST_SECOND / fps) - time <
          -1.5 * (GST_SECOND / fps)) {
        /* this means we're lagging far behind */
        v4lsrc->need_writes++;  /* += ((time / fps) - v4lsrc->handled); */
        g_signal_emit (G_OBJECT (v4lsrc),
            gst_v4lsrc_signals[SIGNAL_FRAME_INSERT], 0);
      }

      if (v4lsrc->need_writes > 0) {
        have_frame = TRUE;
        v4lsrc->use_num_times[num] = v4lsrc->need_writes;
        v4lsrc->need_writes--;
      } else {
        gst_v4lsrc_requeue_frame (v4lsrc, num);
      }
    } while (!have_frame);
  } else {
    /* grab a frame from the device */
    if (!gst_v4lsrc_grab_frame (v4lsrc, &num))
      return NULL;

    v4lsrc->use_num_times[num] = 1;
  }

  buf = gst_buffer_new ();
  GST_BUFFER_FREE_DATA_FUNC (buf) = gst_v4lsrc_buffer_free;
  /* to requeue buffers, we need a ref to the element, as well as the frame
     number we got this buffer from */
  v4lsrc_private = g_malloc (sizeof (v4lsrc_private_t));
  v4lsrc_private->v4lsrc = v4lsrc;
  v4lsrc_private->num = num;
  GST_BUFFER_PRIVATE (buf) = v4lsrc_private;

  /* don't | the flags, they are integers, not bits!! */
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_READONLY);
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);
  GST_BUFFER_DATA (buf) = gst_v4lsrc_get_buffer (v4lsrc, num);
  GST_BUFFER_MAXSIZE (buf) = v4lsrc->mbuf.size / v4lsrc->mbuf.frames;
  GST_BUFFER_SIZE (buf) = v4lsrc->buffer_size;

  if (v4lsrc->copy_mode) {
    GstBuffer *copy = gst_buffer_copy (buf);

    gst_buffer_unref (buf);
    buf = copy;
  }

  switch (v4lsrc->syncmode) {
    case GST_V4LSRC_SYNC_MODE_FIXED_FPS:
      GST_BUFFER_TIMESTAMP (buf) = v4lsrc->handled * GST_SECOND / fps;
      break;
    case GST_V4LSRC_SYNC_MODE_PRIVATE_CLOCK:
      /* calculate time based on our own clock */
      GST_BUFFER_TIMESTAMP (buf) =
          v4lsrc->timestamp_sync - v4lsrc->substract_time;
      break;
    case GST_V4LSRC_SYNC_MODE_CLOCK:
      if (v4lsrc->clock) {
        GstClockTime time = gst_element_get_time (GST_ELEMENT (v4lsrc));
        GstClockTimeDiff target_ts = 0;

        /* we can't go negative for timestamps.  FIXME: latency querying
         * in the core generally should solve this */
        target_ts = GST_CLOCK_DIFF (time, v4lsrc->latency_offset);
        if (target_ts < 0)
          target_ts = 0;
        /* FIXME: create GST_TIME_DIFF_ARGS for target_ts */
        GST_LOG_OBJECT (v4lsrc, "time: %" GST_TIME_FORMAT
            ", latency-offset: %" GST_TIME_FORMAT
            ", timestamp: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (time), GST_TIME_ARGS (v4lsrc->latency_offset),
            GST_TIME_ARGS (target_ts));
        GST_BUFFER_TIMESTAMP (buf) = target_ts;
      } else {
        GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
      }
      break;
    case GST_V4LSRC_SYNC_MODE_NONE:
      GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }
  GST_BUFFER_DURATION (buf) = GST_SECOND / fps;

  GST_LOG_OBJECT (v4lsrc, "outgoing buffer duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  v4lsrc->handled++;
  g_signal_emit (G_OBJECT (v4lsrc),
      gst_v4lsrc_signals[SIGNAL_FRAME_CAPTURE], 0);

  /* this is the current timestamp */
  now = gst_element_get_time (GST_ELEMENT (v4lsrc));

  until = GST_BUFFER_TIMESTAMP (buf);

  GST_LOG_OBJECT (v4lsrc, "Current time %" GST_TIME_FORMAT
      ", buffer timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (now), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  if (now < until) {
    GST_LOG_OBJECT (v4lsrc, "waiting until %" GST_TIME_FORMAT,
        GST_TIME_ARGS (until));
    if (!gst_element_wait (GST_ELEMENT (v4lsrc), until))
      GST_WARNING_OBJECT (v4lsrc, "waiting from now %" GST_TIME_FORMAT
          " until %" GST_TIME_FORMAT " failed",
          GST_TIME_ARGS (now), GST_TIME_ARGS (until));
    GST_LOG_OBJECT (v4lsrc, "wait done.");
  }
  /* check for discont; we do it after grabbing so that we drop the
   * first frame grabbed, but get an accurate discont event  */
  if (v4lsrc->need_discont) {
    GstEvent *event;

    v4lsrc->need_discont = FALSE;

    /* drop the buffer we made */
    gst_buffer_unref (buf);

    /* get time for discont */
    now = gst_element_get_time (GST_ELEMENT (v4lsrc));
    GST_DEBUG_OBJECT (v4lsrc, "sending time discont with %" GST_TIME_FORMAT,
        GST_TIME_ARGS (now));

    /* store discont internally so we can wait when sending buffers too soon */
    v4lsrc->last_discont = now;

    /* return event */
    event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, now, NULL);
    return GST_DATA (event);
  }
  return GST_DATA (buf);
}


static void
gst_v4lsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail (GST_IS_V4LSRC (object));
  v4lsrc = GST_V4LSRC (object);

  switch (prop_id) {
    case ARG_SYNC_MODE:
      if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc))) {
        v4lsrc->syncmode = g_value_get_enum (value);
      }
      break;

    case ARG_COPY_MODE:
      v4lsrc->copy_mode = g_value_get_boolean (value);
      break;

    case ARG_AUTOPROBE:
      if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc))) {
        v4lsrc->autoprobe = g_value_get_boolean (value);
      }
      break;

    case ARG_AUTOPROBE_FPS:
      if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc))) {
        v4lsrc->autoprobe_fps = g_value_get_boolean (value);
      }
      break;


    case ARG_LATENCY_OFFSET:
      v4lsrc->latency_offset = g_value_get_int (value);
      break;


    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail (GST_IS_V4LSRC (object));
  v4lsrc = GST_V4LSRC (object);

  switch (prop_id) {
    case ARG_NUMBUFS:
      g_value_set_int (value, v4lsrc->mbuf.frames);
      break;

    case ARG_BUFSIZE:
      if (v4lsrc->mbuf.frames == 0)
        g_value_set_int (value, 0);
      else
        g_value_set_int (value,
            v4lsrc->mbuf.size / (v4lsrc->mbuf.frames * 1024));
      break;

    case ARG_SYNC_MODE:
      g_value_set_enum (value, v4lsrc->syncmode);
      break;

    case ARG_COPY_MODE:
      g_value_set_boolean (value, v4lsrc->copy_mode);
      break;

    case ARG_AUTOPROBE:
      g_value_set_boolean (value, v4lsrc->autoprobe);
      break;

    case ARG_AUTOPROBE_FPS:
      g_value_set_boolean (value, v4lsrc->autoprobe_fps);
      break;

    case ARG_LATENCY_OFFSET:
      g_value_set_int (value, v4lsrc->latency_offset);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lsrc_change_state (GstElement * element)
{
  GstV4lSrc *v4lsrc;
  GTimeVal time;
  gint transition = GST_STATE_TRANSITION (element);
  GstElementStateReturn parent_ret = GST_STATE_SUCCESS;

  g_return_val_if_fail (GST_IS_V4LSRC (element), GST_STATE_FAILURE);

  v4lsrc = GST_V4LSRC (element);

  /* pre-parent state change */
  switch (transition) {
    case GST_STATE_READY_TO_PAUSED:
      v4lsrc->handled = 0;
      v4lsrc->need_discont = TRUE;
      v4lsrc->last_discont = 0;
      v4lsrc->need_writes = 0;
      v4lsrc->last_frame = 0;
      v4lsrc->substract_time = 0;
      /* buffer setup used to be done here, but I moved it to
       * capsnego */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (GST_V4LELEMENT (v4lsrc)->buffer != NULL &&
          !gst_v4lsrc_capture_start (v4lsrc))
        return GST_STATE_FAILURE;
      g_get_current_time (&time);
      v4lsrc->substract_time =
          GST_TIMEVAL_TO_TIME (time) - v4lsrc->substract_time;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (v4lsrc->is_capturing && !gst_v4lsrc_capture_stop (v4lsrc))
        return GST_STATE_FAILURE;
      gst_v4lsrc_capture_stop (v4lsrc);
      g_get_current_time (&time);
      v4lsrc->substract_time =
          GST_TIMEVAL_TO_TIME (time) - v4lsrc->substract_time;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (GST_V4LELEMENT (v4lsrc)->buffer != NULL &&
          !gst_v4lsrc_capture_deinit (v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    parent_ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  /* post-parent change_state */
  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      GST_DEBUG_OBJECT (v4lsrc,
          "Doing post-parent NULL_TO_READY, checking probes");
      if (v4lsrc->autoprobe_fps) {
        GST_DEBUG_OBJECT (v4lsrc, "autoprobing framerates");
        v4lsrc->fps_list = gst_v4lsrc_get_fps_list (v4lsrc);
      }
    default:
      break;
  }

  return parent_ret;
}


#if 0
static GstBuffer *
gst_v4lsrc_buffer_new (GstBufferPool * pool,
    guint64 offset, guint size, gpointer user_data)
{
  GstBuffer *buffer;
  GstV4lSrc *v4lsrc = GST_V4LSRC (user_data);

  if (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc)))
    return NULL;

  buffer = gst_buffer_new ();
  if (!buffer)
    return NULL;

  /* TODO: add interlacing info to buffer as metadata
   * (height>288 or 240 = topfieldfirst, else noninterlaced) */
  GST_BUFFER_MAXSIZE (buffer) = v4lsrc->mbuf.size / v4lsrc->mbuf.frames;
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_DONTFREE);

  return buffer;
}
#endif

static void
gst_v4lsrc_buffer_free (GstBuffer * buf)
{
  v4lsrc_private_t *v4lsrc_private;
  GstV4lSrc *v4lsrc;
  int num;

  v4lsrc_private = GST_BUFFER_PRIVATE (buf);
  g_assert (v4lsrc_private);
  v4lsrc = v4lsrc_private->v4lsrc;
  num = v4lsrc_private->num;
  g_free (v4lsrc_private);
  GST_BUFFER_PRIVATE (buf) = 0;

  GST_LOG_OBJECT (v4lsrc, "freeing buffer %p with refcount %d for frame %d",
      buf, GST_BUFFER_REFCOUNT_VALUE (buf), num);
  if (gst_element_get_state (GST_ELEMENT (v4lsrc)) != GST_STATE_PLAYING)
    return;                     /* we've already cleaned up ourselves */

  v4lsrc->use_num_times[num]--;
  if (v4lsrc->use_num_times[num] <= 0) {
    GST_LOG_OBJECT (v4lsrc, "requeueing frame %d", num);
    gst_v4lsrc_requeue_frame (v4lsrc, num);
  }
}


static void
gst_v4lsrc_set_clock (GstElement * element, GstClock * clock)
{
  GST_V4LSRC (element)->clock = clock;
}
