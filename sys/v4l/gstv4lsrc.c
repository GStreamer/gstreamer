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
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include "v4lsrc_calls.h"
#include <sys/ioctl.h>


GST_DEBUG_CATEGORY_STATIC (v4lsrc_debug);
#define GST_CAT_DEFAULT v4lsrc_debug


enum
{
  PROP_0,
  PROP_AUTOPROBE,
  PROP_AUTOPROBE_FPS,
  PROP_COPY_MODE,
  PROP_TIMESTAMP_OFFSET
};


GST_BOILERPLATE (GstV4lSrc, gst_v4lsrc, GstV4lElement, GST_TYPE_V4LELEMENT);

static GstStaticPadTemplate v4l_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

/* basesrc methods */
static gboolean gst_v4lsrc_start (GstBaseSrc * src);
static gboolean gst_v4lsrc_stop (GstBaseSrc * src);
static gboolean gst_v4lsrc_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_v4lsrc_get_caps (GstBaseSrc * src);
static GstFlowReturn gst_v4lsrc_create (GstPushSrc * src, GstBuffer ** out);
static gboolean gst_v4lsrc_query (GstBaseSrc * bsrc, GstQuery * query);
static void gst_v4lsrc_fixate (GstBaseSrc * bsrc, GstCaps * caps);

static void gst_v4lsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_v4lsrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static void
gst_v4lsrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Video (video4linux/raw) Source", "Source/Video",
      "Reads raw frames from a video4linux device",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &v4l_src_template);
}

static void
gst_v4lsrc_class_init (GstV4lSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_AUTOPROBE,
      g_param_spec_boolean ("autoprobe", "Autoprobe",
          "Whether the device should be probed for all possible features",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_AUTOPROBE_FPS,
      g_param_spec_boolean ("autoprobe-fps", "Autoprobe FPS",
          "Whether the device should be probed for framerates",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COPY_MODE,
      g_param_spec_boolean ("copy-mode", "Copy mode",
          "Whether to send out copies of buffers, or direct pointers to the mmap region",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "A time offset subtracted from timestamps set on buffers (in ns)",
          G_MININT64, G_MAXINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (v4lsrc_debug, "v4lsrc", 0, "V4L source element");

  basesrc_class->get_caps = gst_v4lsrc_get_caps;
  basesrc_class->set_caps = gst_v4lsrc_set_caps;
  basesrc_class->start = gst_v4lsrc_start;
  basesrc_class->stop = gst_v4lsrc_stop;
  basesrc_class->fixate = gst_v4lsrc_fixate;
  basesrc_class->query = gst_v4lsrc_query;

  pushsrc_class->create = gst_v4lsrc_create;
}

static void
gst_v4lsrc_init (GstV4lSrc * v4lsrc, GstV4lSrcClass * klass)
{
  v4lsrc->buffer_size = 0;

  /* no colorspaces */
  v4lsrc->colorspaces = NULL;

  v4lsrc->is_capturing = FALSE;
  v4lsrc->autoprobe = TRUE;
  v4lsrc->autoprobe_fps = TRUE;
  v4lsrc->copy_mode = TRUE;

  v4lsrc->timestamp_offset = 0;

  v4lsrc->fps_list = NULL;

  gst_base_src_set_format (GST_BASE_SRC (v4lsrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (v4lsrc), TRUE);
}

static void
gst_v4lsrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (object);

  switch (prop_id) {
    case PROP_AUTOPROBE:
      g_return_if_fail (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc)));
      v4lsrc->autoprobe = g_value_get_boolean (value);
      break;
    case PROP_AUTOPROBE_FPS:
      g_return_if_fail (!GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc)));
      v4lsrc->autoprobe_fps = g_value_get_boolean (value);
      break;
    case PROP_COPY_MODE:
      v4lsrc->copy_mode = g_value_get_boolean (value);
      break;
    case PROP_TIMESTAMP_OFFSET:
      v4lsrc->timestamp_offset = g_value_get_int64 (value);
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
  GstV4lSrc *v4lsrc = GST_V4LSRC (object);

  switch (prop_id) {
    case PROP_AUTOPROBE:
      g_value_set_boolean (value, v4lsrc->autoprobe);
      break;
    case PROP_AUTOPROBE_FPS:
      g_value_set_boolean (value, v4lsrc->autoprobe_fps);
      break;
    case PROP_COPY_MODE:
      g_value_set_boolean (value, v4lsrc->copy_mode);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, v4lsrc->timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* this function is a bit of a last resort */
static void
gst_v4lsrc_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;
  int i;
  int targetwidth, targetheight;
  GstV4lSrc *v4lsrc = GST_V4LSRC (bsrc);
  struct video_capability *vcap = &GST_V4LELEMENT (v4lsrc)->vcap;
  struct video_window *vwin = &GST_V4LELEMENT (v4lsrc)->vwin;

  if (GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
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
  } else {
    GST_DEBUG_OBJECT (v4lsrc, "device closed, guessing");
    targetwidth = 320;
    targetheight = 200;
  }

  GST_DEBUG_OBJECT (v4lsrc, "targetting %dx%d", targetwidth, targetheight);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    const GValue *v;

    structure = gst_caps_get_structure (caps, i);
    gst_structure_fixate_field_nearest_int (structure, "width", targetwidth);
    gst_structure_fixate_field_nearest_int (structure, "height", targetheight);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 15, 2);

    v = gst_structure_get_value (structure, "format");
    if (v && G_VALUE_TYPE (v) != GST_TYPE_FOURCC) {
      guint32 fourcc;

      g_return_if_fail (G_VALUE_TYPE (v) == GST_TYPE_LIST);

      fourcc = gst_value_get_fourcc (gst_value_list_get_value (v, 0));
      gst_structure_set (structure, "format", GST_TYPE_FOURCC, fourcc, NULL);
    }
  }
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
            "red_mask = 0xFF0000, "
            "green_mask = 0x00FF00, " "blue_mask = 0x0000FF");
        break;
      case VIDEO_PALETTE_RGB32:
        caps = gst_caps_from_string ("video/x-raw-rgb, "
            "bpp = (int) 32, "
            "depth = (int) 24, "
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
gst_v4lsrc_get_any_caps (void)
{
  gint i;
  GstCaps *caps = gst_caps_new_empty (), *one;

  for (i = 0; all_palettes[i] != -1; i++) {
    one = gst_v4lsrc_palette_to_caps (all_palettes[i]);
    gst_caps_append (caps, one);
  }

  return caps;
}

static GstCaps *
gst_v4lsrc_get_caps (GstBaseSrc * src)
{
  GstCaps *list;
  GstV4lSrc *v4lsrc = GST_V4LSRC (src);
  struct video_capability *vcap = &GST_V4LELEMENT (v4lsrc)->vcap;
  gint width = GST_V4LELEMENT (src)->vcap.minwidth;
  gint height = GST_V4LELEMENT (src)->vcap.minheight;
  gint i;
  gint fps_n, fps_d;
  GList *item;

  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    return gst_v4lsrc_get_any_caps ();
  }

  if (!v4lsrc->autoprobe) {
    /* FIXME: query current caps and return those, with _any appended */
    return gst_v4lsrc_get_any_caps ();
  }

  if (!v4lsrc->colorspaces) {
    GST_DEBUG_OBJECT (v4lsrc, "Checking supported palettes");
    for (i = 0; all_palettes[i] != -1; i++) {
      /* try palette out */
      if (!gst_v4lsrc_try_capture (v4lsrc, width, height, all_palettes[i]))
        continue;
      GST_DEBUG_OBJECT (v4lsrc, "Added palette %d (%s) to supported list",
          all_palettes[i], gst_v4lsrc_palette_name (all_palettes[i]));
      v4lsrc->colorspaces = g_list_append (v4lsrc->colorspaces,
          GINT_TO_POINTER (all_palettes[i]));
    }
    GST_DEBUG_OBJECT (v4lsrc, "%d palette(s) supported",
        g_list_length (v4lsrc->colorspaces));
    if (v4lsrc->autoprobe_fps) {
      GST_DEBUG_OBJECT (v4lsrc, "autoprobing framerates");
      v4lsrc->fps_list = gst_v4lsrc_get_fps_list (v4lsrc);
    }
  }


  if (!gst_v4lsrc_get_fps (v4lsrc, &fps_n, &fps_d)) {
    fps_n = 0;
    fps_d = 1;
  }

  list = gst_caps_new_empty ();
  for (item = v4lsrc->colorspaces; item != NULL; item = item->next) {
    GstCaps *one;

    one = gst_v4lsrc_palette_to_caps (GPOINTER_TO_INT (item->data));
    if (!one) {
      GST_WARNING_OBJECT (v4lsrc, "Palette %d gave no caps\n",
          GPOINTER_TO_INT (item->data));
      continue;
    }

    GST_DEBUG_OBJECT (v4lsrc,
        "Device reports w: %d-%d, h: %d-%d, fps: %d/%d for palette %d",
        vcap->minwidth, vcap->maxwidth, vcap->minheight, vcap->maxheight,
        fps_n, fps_d, GPOINTER_TO_INT (item->data));

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
      GstStructure *structure = gst_caps_get_structure (one, 0);

      if (v4lsrc->fps_list) {
        gst_structure_set_value (structure, "framerate", v4lsrc->fps_list);
      } else {
        gst_structure_set (structure, "framerate", GST_TYPE_FRACTION,
            fps_n, fps_d, NULL);
      }
    } else {
      gst_caps_set_simple (one, "framerate", GST_TYPE_FRACTION_RANGE,
          1, 1, 100, 1, NULL);
    }

    GST_DEBUG_OBJECT (v4lsrc, "caps: %" GST_PTR_FORMAT, one);
    gst_caps_append (list, one);
  }

  return list;
}

static gboolean
gst_v4lsrc_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstV4lSrc *v4lsrc;
  guint32 fourcc;
  gint bpp, depth, w, h, palette = -1;
  const GValue *new_fps;
  gint cur_fps_n, cur_fps_d;
  GstStructure *structure;
  struct video_window *vwin;

  v4lsrc = GST_V4LSRC (src);
  vwin = &GST_V4LELEMENT (v4lsrc)->vwin;

  /* if we're not open, punt -- we'll get setcaps'd later via negotiate */
  if (!GST_V4L_IS_OPEN (v4lsrc))
    return FALSE;

  /* make sure we stop capturing and dealloc buffers */
  if (GST_V4L_IS_ACTIVE (v4lsrc)) {
    if (!gst_v4lsrc_capture_stop (v4lsrc))
      return FALSE;
    if (!gst_v4lsrc_capture_deinit (v4lsrc))
      return FALSE;
  }

  /* it's fixed, one struct */
  structure = gst_caps_get_structure (caps, 0);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0)
    gst_structure_get_fourcc (structure, "format", &fourcc);
  else
    fourcc = GST_MAKE_FOURCC ('R', 'G', 'B', ' ');

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  new_fps = gst_structure_get_value (structure, "framerate");

  /* set framerate if it's not already correct */
  if (!gst_v4lsrc_get_fps (v4lsrc, &cur_fps_n, &cur_fps_d))
    return FALSE;

  if (new_fps) {
    GST_DEBUG_OBJECT (v4lsrc, "linking with %dx%d at %d/%d fps", w, h,
        gst_value_get_fraction_numerator (new_fps),
        gst_value_get_fraction_denominator (new_fps));

    if (gst_value_get_fraction_numerator (new_fps) != cur_fps_n ||
        gst_value_get_fraction_denominator (new_fps) != cur_fps_d) {
      int fps_index = (gst_value_get_fraction_numerator (new_fps) * 16) /
          (gst_value_get_fraction_denominator (new_fps) * 15);

      GST_DEBUG_OBJECT (v4lsrc, "Trying to set fps index %d", fps_index);
      /* set bits 16 to 21 to 0 */
      vwin->flags &= (0x3F00 - 1);
      /* set bits 16 to 21 to the index */
      vwin->flags |= fps_index << 16;
      if (!gst_v4l_set_window_properties (GST_V4LELEMENT (v4lsrc))) {
        return FALSE;
      }
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
    GST_WARNING_OBJECT (v4lsrc, "palette for fourcc %" GST_FOURCC_FORMAT
        " is -1, refusing link", GST_FOURCC_ARGS (fourcc));
    return FALSE;
  }

  GST_DEBUG_OBJECT (v4lsrc, "trying to set_capture %dx%d, palette %d",
      w, h, palette);
  /* this only fills in v4lsrc->mmap values */
  if (!gst_v4lsrc_set_capture (v4lsrc, w, h, palette)) {
    GST_WARNING_OBJECT (v4lsrc, "could not set_capture %dx%d, palette %d",
        w, h, palette);
    return FALSE;
  }

  /* first try the negotiated settings using try_capture */
  if (!gst_v4lsrc_try_capture (v4lsrc, w, h, palette)) {
    GST_DEBUG_OBJECT (v4lsrc, "failed trying palette %d for %dx%d", palette,
        w, h);
    return FALSE;
  }

  if (!gst_v4lsrc_capture_init (v4lsrc))
    return FALSE;

  if (!gst_v4lsrc_capture_start (v4lsrc))
    return FALSE;

  return TRUE;
}

static gboolean
gst_v4lsrc_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4lSrc *v4lsrc;
  gboolean res = FALSE;

  v4lsrc = GST_V4LSRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;
      gint fps_n, fps_d;

      /* device must be open */
      if (!GST_V4L_IS_OPEN (v4lsrc))
        goto done;

      /* we must have a framerate */
      if (!(res = gst_v4lsrc_get_fps (v4lsrc, &fps_n, &fps_d)))
        goto done;

      /* min latency is the time to capture one frame */
      min_latency = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);

      /* max latency is total duration of the frame buffer */
      max_latency = v4lsrc->mbuf.frames * min_latency;

      GST_DEBUG_OBJECT (bsrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }
done:
  return res;
}

/* start and stop are not symmetric -- start will open the device, but not start
   capture. it's setcaps that will start capture, which is called via basesrc's
   negotiate method. stop will both stop capture and close the device.
 */
static gboolean
gst_v4lsrc_start (GstBaseSrc * src)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (src);

  v4lsrc->offset = 0;

  return TRUE;
}

static gboolean
gst_v4lsrc_stop (GstBaseSrc * src)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (src);

  if (GST_V4L_IS_ACTIVE (v4lsrc) && !gst_v4lsrc_capture_stop (v4lsrc))
    return FALSE;

  if (GST_V4LELEMENT (v4lsrc)->buffer != NULL) {
    if (!gst_v4lsrc_capture_deinit (v4lsrc))
      return FALSE;
  }

  g_list_free (v4lsrc->colorspaces);
  v4lsrc->colorspaces = NULL;

  if (v4lsrc->fps_list) {
    g_value_unset (v4lsrc->fps_list);
    g_free (v4lsrc->fps_list);
    v4lsrc->fps_list = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_v4lsrc_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstV4lSrc *v4lsrc;
  gint num;

  v4lsrc = GST_V4LSRC (src);

  /* grab a frame from the device */
  if (!gst_v4lsrc_grab_frame (v4lsrc, &num))
    return GST_FLOW_ERROR;

  *buf = gst_v4lsrc_buffer_new (v4lsrc, num);

  if (v4lsrc->copy_mode) {
    GstBuffer *copy = gst_buffer_copy (*buf);

    gst_buffer_unref (*buf);
    *buf = copy;
  }

  return GST_FLOW_OK;
}
