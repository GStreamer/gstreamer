/* GStreamer
 * Copyright (C) 2007 Michael Smith <msmith@xiph.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * This is a decoder for the VMWare VMnc video codec, which VMWare uses for 
 * recording * of virtual machine instances.
 * It's essentially a serialisation of RFB (the VNC protocol) 
 * 'FramebufferUpdate' messages, with some special encoding-types for VMnc
 * extensions. There's some documentation (with fixes from VMWare employees) at:
 *   http://wiki.multimedia.cx/index.php?title=VMware_Video
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <string.h>

#define GST_CAT_DEFAULT vmnc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_VMNC_DEC \
  (gst_vmnc_dec_get_type())
#define GST_VMNC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMNC_DEC,GstVMncDec))

#define RFB_GET_UINT32(ptr) GST_READ_UINT32_BE(ptr)
#define RFB_GET_UINT16(ptr) GST_READ_UINT16_BE(ptr)
#define RFB_GET_UINT8(ptr) GST_READ_UINT8(ptr)

enum
{
  ARG_0,
};

enum
{
  ERROR_INVALID = -1,           /* Invalid data in bitstream */
  ERROR_INSUFFICIENT_DATA = -2  /* Haven't received enough data yet */
};

#define MAKE_TYPE(a,b,c,d) ((a<<24)|(b<<16)|(c<<8)|d)
enum
{
  TYPE_RAW = 0,
  TYPE_COPY = 1,
  TYPE_RRE = 2,
  TYPE_CoRRE = 4,
  TYPE_HEXTILE = 5,

  TYPE_WMVd = MAKE_TYPE ('W', 'M', 'V', 'd'),
  TYPE_WMVe = MAKE_TYPE ('W', 'M', 'V', 'e'),
  TYPE_WMVf = MAKE_TYPE ('W', 'M', 'V', 'f'),
  TYPE_WMVg = MAKE_TYPE ('W', 'M', 'V', 'g'),
  TYPE_WMVh = MAKE_TYPE ('W', 'M', 'V', 'h'),
  TYPE_WMVi = MAKE_TYPE ('W', 'M', 'V', 'i'),
  TYPE_WMVj = MAKE_TYPE ('W', 'M', 'V', 'j')
};

struct RFBFormat
{
  int width;
  int height;
  int stride;
  int bytes_per_pixel;
  int depth;
  int big_endian;

  guint8 descriptor[16];        /* The raw format descriptor block */
};

enum CursorType
{
  CURSOR_COLOUR = 0,
  CURSOR_ALPHA = 1
};

struct Cursor
{
  enum CursorType type;
  int visible;
  int x;
  int y;
  int width;
  int height;
  int hot_x;
  int hot_y;
  guint8 *cursordata;
  guint8 *cursormask;
};

typedef struct
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstCaps *caps;
  gboolean have_format;

  int parsed;
  GstAdapter *adapter;

  int framerate_num;
  int framerate_denom;

  struct Cursor cursor;
  struct RFBFormat format;
  guint8 *imagedata;
} GstVMncDec;

typedef struct
{
  GstElementClass parent_class;
} GstVMncDecClass;

static GstStaticPadTemplate vmnc_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb"));

static GstStaticPadTemplate vmnc_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vmnc, version=(int)1, "
        "framerate=(fraction)[0, max], "
        "width=(int)[0, max], " "height=(int)[0, max]")
    );

GType gst_vmnc_dec_get_type (void);
GST_BOILERPLATE (GstVMncDec, gst_vmnc_dec, GstElement, GST_TYPE_ELEMENT);

static void vmnc_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void vmnc_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstFlowReturn vmnc_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean vmnc_dec_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn vmnc_dec_change_state (GstElement * element,
    GstStateChange transition);
static void vmnc_dec_finalize (GObject * object);

static void
gst_vmnc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vmnc_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vmnc_dec_sink_factory));
  gst_element_class_set_static_metadata (element_class, "VMnc video decoder",
      "Codec/Decoder/Video",
      "Decode VmWare video to raw (RGB) video",
      "Michael Smith <msmith@xiph.org>");
}

static void
gst_vmnc_dec_class_init (GstVMncDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = vmnc_dec_set_property;
  gobject_class->get_property = vmnc_dec_get_property;

  gobject_class->finalize = vmnc_dec_finalize;

  gstelement_class->change_state = vmnc_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (vmnc_debug, "vmncdec", 0, "VMnc decoder");
}

static void
gst_vmnc_dec_init (GstVMncDec * dec, GstVMncDecClass * g_class)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&vmnc_dec_sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad, vmnc_dec_chain);
  gst_pad_set_setcaps_function (dec->sinkpad, vmnc_dec_setcaps);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&vmnc_dec_src_factory, "src");
  gst_pad_use_fixed_caps (dec->srcpad);

  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->adapter = gst_adapter_new ();
}

static void
vmnc_dec_finalize (GObject * object)
{
  GstVMncDec *dec = GST_VMNC_DEC (object);

  g_object_unref (dec->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vmnc_dec_reset (GstVMncDec * dec)
{
  if (dec->caps) {
    gst_caps_unref (dec->caps);
    dec->caps = NULL;
  }
  if (dec->imagedata) {
    g_free (dec->imagedata);
    dec->imagedata = NULL;
  }

  if (dec->cursor.cursordata) {
    g_free (dec->cursor.cursordata);
    dec->cursor.cursordata = NULL;
  }
  if (dec->cursor.cursormask) {
    g_free (dec->cursor.cursormask);
    dec->cursor.cursormask = NULL;
  }
  dec->cursor.visible = 0;

  /* Use these as defaults if the container doesn't provide anything */
  dec->framerate_num = 5;
  dec->framerate_denom = 1;

  dec->have_format = FALSE;

  gst_adapter_clear (dec->adapter);
}

struct RfbRectangle
{
  guint16 x;
  guint16 y;
  guint16 width;
  guint16 height;

  gint32 type;
};

/* Rectangle handling functions.
 * Return number of bytes consumed, or < 0 on error
 */
typedef int (*rectangle_handler) (GstVMncDec * dec, struct RfbRectangle * rect,
    const guint8 * data, int len, gboolean decode);

static int
vmnc_handle_wmvi_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  GstCaps *caps;
  gint bpp, tc;
  guint32 redmask, greenmask, bluemask;
  guint32 endianness, dataendianness;

  /* A WMVi rectangle has a 16byte payload */
  if (len < 16) {
    GST_DEBUG_OBJECT (dec, "Bad WMVi rect: too short");
    return ERROR_INSUFFICIENT_DATA;
  }

  /* We only compare 13 bytes; ignoring the 3 padding bytes at the end */
  if (dec->caps && memcmp (data, dec->format.descriptor, 13) == 0) {
    /* Nothing changed, so just exit */
    return 16;
  }

  /* Store the whole block for simple comparison later */
  memcpy (dec->format.descriptor, data, 16);

  if (rect->x != 0 || rect->y != 0) {
    GST_WARNING_OBJECT (dec, "Bad WMVi rect: wrong coordinates");
    return ERROR_INVALID;
  }

  bpp = data[0];
  dec->format.depth = data[1];
  dec->format.big_endian = data[2];
  dataendianness = data[2] ? G_BIG_ENDIAN : G_LITTLE_ENDIAN;
  tc = data[3];

  if (bpp != 8 && bpp != 16 && bpp != 32) {
    GST_WARNING_OBJECT (dec, "Bad bpp value: %d", bpp);
    return ERROR_INVALID;
  }

  if (!tc) {
    GST_WARNING_OBJECT (dec, "Paletted video not supported");
    return ERROR_INVALID;
  }

  dec->format.bytes_per_pixel = bpp / 8;
  dec->format.width = rect->width;
  dec->format.height = rect->height;

  redmask = (guint32) (RFB_GET_UINT16 (data + 4)) << data[10];
  greenmask = (guint32) (RFB_GET_UINT16 (data + 6)) << data[11];
  bluemask = (guint32) (RFB_GET_UINT16 (data + 8)) << data[12];

  GST_DEBUG_OBJECT (dec, "Red: mask %d, shift %d",
      RFB_GET_UINT16 (data + 4), data[10]);
  GST_DEBUG_OBJECT (dec, "Green: mask %d, shift %d",
      RFB_GET_UINT16 (data + 6), data[11]);
  GST_DEBUG_OBJECT (dec, "Blue: mask %d, shift %d",
      RFB_GET_UINT16 (data + 8), data[12]);
  GST_DEBUG_OBJECT (dec, "BPP: %d. endianness: %s", bpp,
      data[2] ? "big" : "little");

  /* GStreamer's RGB caps are a bit weird. */
  if (bpp == 8) {
    endianness = G_BYTE_ORDER;  /* Doesn't matter */
  } else if (bpp == 16) {
    /* We require host-endian. */
    endianness = G_BYTE_ORDER;
  } else {                      /* bpp == 32 */
    /* We require big endian */
    endianness = G_BIG_ENDIAN;
    if (endianness != dataendianness) {
      redmask = GUINT32_SWAP_LE_BE (redmask);
      greenmask = GUINT32_SWAP_LE_BE (greenmask);
      bluemask = GUINT32_SWAP_LE_BE (bluemask);
    }
  }

  dec->have_format = TRUE;
  if (!decode) {
    GST_DEBUG_OBJECT (dec, "Parsing, not setting caps");
    return 16;
  }

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "framerate", GST_TYPE_FRACTION, dec->framerate_num, dec->framerate_denom,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "width", G_TYPE_INT, rect->width, "height", G_TYPE_INT, rect->height,
      "bpp", G_TYPE_INT, bpp,
      "depth", G_TYPE_INT, dec->format.depth,
      "endianness", G_TYPE_INT, endianness,
      "red_mask", G_TYPE_INT, redmask,
      "green_mask", G_TYPE_INT, greenmask,
      "blue_mask", G_TYPE_INT, bluemask, NULL);
  gst_pad_set_caps (dec->srcpad, caps);

  if (dec->caps)
    gst_caps_unref (dec->caps);
  dec->caps = caps;

  if (dec->imagedata)
    g_free (dec->imagedata);
  dec->imagedata = g_malloc (dec->format.width * dec->format.height *
      dec->format.bytes_per_pixel);
  GST_DEBUG_OBJECT (dec, "Allocated image data at %p", dec->imagedata);

  dec->format.stride = dec->format.width * dec->format.bytes_per_pixel;

  return 16;
}

static void
render_colour_cursor (GstVMncDec * dec, guint8 * data, int x, int y,
    int off_x, int off_y, int width, int height)
{
  int i, j;
  guint8 *dstraw = data + dec->format.stride * y +
      dec->format.bytes_per_pixel * x;
  guint8 *srcraw = dec->cursor.cursordata +
      dec->cursor.width * dec->format.bytes_per_pixel * off_y;
  guint8 *maskraw = dec->cursor.cursormask +
      dec->cursor.width * dec->format.bytes_per_pixel * off_y;

  /* Boundchecking done by caller; this is just the renderer inner loop */
  if (dec->format.bytes_per_pixel == 1) {
    guint8 *dst = dstraw;
    guint8 *src = srcraw;
    guint8 *mask = maskraw;

    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        dst[j] = (dst[j] & src[j]) ^ mask[j];
      }
      dst += dec->format.width;
      src += dec->cursor.width;
      mask += dec->cursor.width;
    }
  } else if (dec->format.bytes_per_pixel == 2) {
    guint16 *dst = (guint16 *) dstraw;
    guint16 *src = (guint16 *) srcraw;
    guint16 *mask = (guint16 *) maskraw;

    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        dst[j] = (dst[j] & src[j]) ^ mask[j];
      }
      dst += dec->format.width;
      src += dec->cursor.width;
      mask += dec->cursor.width;
    }
  } else {
    guint32 *dst = (guint32 *) dstraw;
    guint32 *src = (guint32 *) srcraw;
    guint32 *mask = (guint32 *) maskraw;

    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        dst[j] = (dst[j] & src[j]) ^ mask[j];
      }
      dst += dec->format.width;
      src += dec->cursor.width;
      mask += dec->cursor.width;
    }
  }
}

static void
render_cursor (GstVMncDec * dec, guint8 * data)
{
  /* First, figure out the portion of the cursor that's on-screen */
  /* X,Y of top-left of cursor */
  int x = dec->cursor.x - dec->cursor.hot_x;
  int y = dec->cursor.y - dec->cursor.hot_y;

  /* Width, height of rendered portion of cursor */
  int width = dec->cursor.width;
  int height = dec->cursor.height;

  /* X,Y offset of rendered portion of cursor */
  int off_x = 0;
  int off_y = 0;

  if (x < 0) {
    off_x = -x;
    width += x;
    x = 0;
  }
  if (x + width > dec->format.width)
    width = dec->format.width - x;
  if (y < 0) {
    off_y = -y;
    height += y;
    y = 0;
  }
  if (y + height > dec->format.height)
    height = dec->format.height - y;

  if (dec->cursor.type == CURSOR_COLOUR) {
    render_colour_cursor (dec, data, x, y, off_x, off_y, width, height);
  } else {
    /* Alpha cursor. */
    /* TODO: Implement me! */
    GST_WARNING_OBJECT (dec, "Alpha composited cursors not yet implemented");
  }
}

static GstBuffer *
vmnc_make_buffer (GstVMncDec * dec, GstBuffer * inbuf)
{
  int size = dec->format.stride * dec->format.height;
  GstBuffer *buf = gst_buffer_new_and_alloc (size);
  guint8 *data = GST_BUFFER_DATA (buf);

  memcpy (data, dec->imagedata, size);

  if (dec->cursor.visible) {
    render_cursor (dec, data);
  }

  if (inbuf) {
    gst_buffer_copy_metadata (buf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);
  }

  gst_buffer_set_caps (buf, dec->caps);

  return buf;
}

static int
vmnc_handle_wmvd_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  /* Cursor data. */
  int datalen = 2;
  int type, size;

  if (len < datalen) {
    GST_DEBUG_OBJECT (dec, "Cursor data too short");
    return ERROR_INSUFFICIENT_DATA;
  }

  type = RFB_GET_UINT8 (data);

  if (type == CURSOR_COLOUR) {
    datalen += rect->width * rect->height * dec->format.bytes_per_pixel * 2;
  } else if (type == CURSOR_ALPHA) {
    datalen += rect->width * rect->height * 4;
  } else {
    GST_WARNING_OBJECT (dec, "Unknown cursor type: %d", type);
    return ERROR_INVALID;
  }

  if (len < datalen) {
    GST_DEBUG_OBJECT (dec, "Cursor data too short");
    return ERROR_INSUFFICIENT_DATA;
  } else if (!decode)
    return datalen;

  dec->cursor.type = type;
  dec->cursor.width = rect->width;
  dec->cursor.height = rect->height;
  dec->cursor.type = type;
  dec->cursor.hot_x = rect->x;
  dec->cursor.hot_y = rect->y;

  if (dec->cursor.cursordata)
    g_free (dec->cursor.cursordata);
  if (dec->cursor.cursormask)
    g_free (dec->cursor.cursormask);

  if (type == 0) {
    size = rect->width * rect->height * dec->format.bytes_per_pixel;
    dec->cursor.cursordata = g_malloc (size);
    dec->cursor.cursormask = g_malloc (size);
    memcpy (dec->cursor.cursordata, data + 2, size);
    memcpy (dec->cursor.cursormask, data + 2 + size, size);
  } else {
    dec->cursor.cursordata = g_malloc (rect->width * rect->height * 4);
    memcpy (dec->cursor.cursordata, data + 2, rect->width * rect->height * 4);
  }

  return datalen;
}

static int
vmnc_handle_wmve_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  guint16 flags;

  /* Cursor state. */
  if (len < 2) {
    GST_DEBUG_OBJECT (dec, "Cursor data too short");
    return ERROR_INSUFFICIENT_DATA;
  } else if (!decode)
    return 2;

  flags = RFB_GET_UINT16 (data);
  dec->cursor.visible = flags & 0x01;

  return 2;
}

static int
vmnc_handle_wmvf_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  /* Cursor position. */
  dec->cursor.x = rect->x;
  dec->cursor.y = rect->y;
  return 0;
}

static int
vmnc_handle_wmvg_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  /* Keyboard stuff; not interesting for playback */
  if (len < 10) {
    GST_DEBUG_OBJECT (dec, "Keyboard data too short");
    return ERROR_INSUFFICIENT_DATA;
  }
  return 10;
}

static int
vmnc_handle_wmvh_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  /* More keyboard stuff; not interesting for playback */
  if (len < 4) {
    GST_DEBUG_OBJECT (dec, "Keyboard data too short");
    return ERROR_INSUFFICIENT_DATA;
  }
  return 4;
}

static int
vmnc_handle_wmvj_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  /* VM state info, not interesting for playback */
  if (len < 2) {
    GST_DEBUG_OBJECT (dec, "VM state data too short");
    return ERROR_INSUFFICIENT_DATA;
  }
  return 2;
}

static void
render_raw_tile (GstVMncDec * dec, const guint8 * data, int x, int y,
    int width, int height)
{
  int i;
  guint8 *dst;
  const guint8 *src;
  int line;

  src = data;
  dst = dec->imagedata + dec->format.stride * y +
      dec->format.bytes_per_pixel * x;
  line = width * dec->format.bytes_per_pixel;

  for (i = 0; i < height; i++) {
    /* This is wrong-endian currently */
    memcpy (dst, src, line);

    dst += dec->format.stride;
    src += line;
  }
}

static void
render_subrect (GstVMncDec * dec, int x, int y, int width,
    int height, guint32 colour)
{
  /* Crazy inefficient! */
  int i, j;
  guint8 *dst;

  for (i = 0; i < height; i++) {
    dst = dec->imagedata + dec->format.stride * (y + i) +
        dec->format.bytes_per_pixel * x;
    for (j = 0; j < width; j++) {
      memcpy (dst, &colour, dec->format.bytes_per_pixel);
      dst += dec->format.bytes_per_pixel;
    }
  }
}

static int
vmnc_handle_raw_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  int datalen = rect->width * rect->height * dec->format.bytes_per_pixel;

  if (len < datalen) {
    GST_DEBUG_OBJECT (dec, "Raw data too short");
    return ERROR_INSUFFICIENT_DATA;
  }

  if (decode)
    render_raw_tile (dec, data, rect->x, rect->y, rect->width, rect->height);

  return datalen;
}

static int
vmnc_handle_copy_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  int src_x, src_y;
  guint8 *src, *dst;
  int i;

  if (len < 4) {
    GST_DEBUG_OBJECT (dec, "Copy data too short");
    return ERROR_INSUFFICIENT_DATA;
  } else if (!decode)
    return 4;

  src_x = RFB_GET_UINT16 (data);
  src_y = RFB_GET_UINT16 (data + 2);

  /* Our destination rectangle is guaranteed in-frame; check this for the source
   * rectangle. */
  if (src_x + rect->width > dec->format.width ||
      src_y + rect->height > dec->format.height) {
    GST_WARNING_OBJECT (dec, "Source rectangle out of range");
    return ERROR_INVALID;
  }

  if (src_y > rect->y || src_x > rect->x) {
    /* Moving forward */
    src = dec->imagedata + dec->format.stride * src_y +
        dec->format.bytes_per_pixel * src_x;
    dst = dec->imagedata + dec->format.stride * rect->y +
        dec->format.bytes_per_pixel * rect->x;
    for (i = 0; i < rect->height; i++) {
      memmove (dst, src, rect->width * dec->format.bytes_per_pixel);
      dst += dec->format.stride;
      src += dec->format.stride;
    }
  } else {
    /* Backwards */
    src = dec->imagedata + dec->format.stride * (src_y + rect->height - 1) +
        dec->format.bytes_per_pixel * src_x;
    dst = dec->imagedata + dec->format.stride * (rect->y + rect->height - 1) +
        dec->format.bytes_per_pixel * rect->x;
    for (i = rect->height; i > 0; i--) {
      memmove (dst, src, rect->width * dec->format.bytes_per_pixel);
      dst -= dec->format.stride;
      src -= dec->format.stride;
    }
  }

  return 4;
}

/* FIXME: data+off might not be properly aligned */
#define READ_PIXEL(pixel, data, off, len)         \
  if (dec->format.bytes_per_pixel == 1) {         \
     if (off >= len)                              \
       return ERROR_INSUFFICIENT_DATA;            \
     pixel = data[off++];                         \
  } else if (dec->format.bytes_per_pixel == 2) {  \
     if (off+2 > len)                             \
       return ERROR_INSUFFICIENT_DATA;            \
     pixel = (*(guint16 *)(data + off));          \
     off += 2;                                    \
  } else {                                        \
     if (off+4 > len)                             \
       return ERROR_INSUFFICIENT_DATA;            \
     pixel = (*(guint32 *)(data + off));          \
     off += 4;                                    \
  }

static int
vmnc_handle_hextile_rectangle (GstVMncDec * dec, struct RfbRectangle *rect,
    const guint8 * data, int len, gboolean decode)
{
  int tilesx = GST_ROUND_UP_16 (rect->width) / 16;
  int tilesy = GST_ROUND_UP_16 (rect->height) / 16;
  int x, y, z;
  int off = 0;
  int subrects;
  int coloured;
  int width, height;
  guint32 fg = 0, bg = 0, colour;
  guint8 flags;

  for (y = 0; y < tilesy; y++) {
    if (y == tilesy - 1)
      height = rect->height - (tilesy - 1) * 16;
    else
      height = 16;

    for (x = 0; x < tilesx; x++) {
      if (x == tilesx - 1)
        width = rect->width - (tilesx - 1) * 16;
      else
        width = 16;

      if (off >= len) {
        return ERROR_INSUFFICIENT_DATA;
      }
      flags = data[off++];

      if (flags & 0x1) {
        if (off + width * height * dec->format.bytes_per_pixel > len) {
          return ERROR_INSUFFICIENT_DATA;
        }
        if (decode)
          render_raw_tile (dec, data + off, rect->x + x * 16, rect->y + y * 16,
              width, height);
        off += width * height * dec->format.bytes_per_pixel;
      } else {
        if (flags & 0x2) {
          READ_PIXEL (bg, data, off, len)
        }
        if (flags & 0x4) {
          READ_PIXEL (fg, data, off, len)
        }

        subrects = 0;
        if (flags & 0x8) {
          if (off >= len) {
            return ERROR_INSUFFICIENT_DATA;
          }
          subrects = data[off++];
        }

        /* Paint background colour on entire tile */
        if (decode)
          render_subrect (dec, rect->x + x * 16, rect->y + y * 16,
              width, height, bg);

        coloured = flags & 0x10;
        for (z = 0; z < subrects; z++) {
          if (coloured) {
            READ_PIXEL (colour, data, off, len);
          } else
            colour = fg;
          if (off + 2 > len)
            return ERROR_INSUFFICIENT_DATA;

          {
            int off_x = (data[off] & 0xf0) >> 4;
            int off_y = (data[off] & 0x0f);
            int w = ((data[off + 1] & 0xf0) >> 4) + 1;
            int h = (data[off + 1] & 0x0f) + 1;

            off += 2;

            /* Ensure we don't have out of bounds coordinates */
            if (off_x + w > width || off_y + h > height) {
              GST_WARNING_OBJECT (dec, "Subrect out of bounds: %d-%d x %d-%d "
                  "extends outside %dx%d", off_x, w, off_y, h, width, height);
              return ERROR_INVALID;
            }

            if (decode)
              render_subrect (dec, rect->x + x * 16 + off_x,
                  rect->y + y * 16 + off_y, w, h, colour);
          }
        }
      }
    }
  }

  return off;
}

/* Handle a packet in one of two modes: decode or parse.
 * In parse mode, we don't execute any of the decoding, we just do enough to
 * figure out how many bytes it contains
 *
 * Returns: >= 0, the number of bytes consumed
 *           < 0, packet too short to decode, or error
 */
static int
vmnc_handle_packet (GstVMncDec * dec, const guint8 * data, int len,
    gboolean decode)
{
  int type;
  int offset = 0;

  if (len < 4) {
    GST_DEBUG_OBJECT (dec, "Packet too short");
    return ERROR_INSUFFICIENT_DATA;
  }

  type = data[0];

  switch (type) {
    case 0:
    {
      int numrect = RFB_GET_UINT16 (data + 2);
      int i;
      int read;

      offset = 4;

      for (i = 0; i < numrect; i++) {
        struct RfbRectangle r;
        rectangle_handler handler;

        if (len < offset + 12) {
          GST_DEBUG_OBJECT (dec,
              "Packet too short for rectangle header: %d < %d",
              len, offset + 12);
          return ERROR_INSUFFICIENT_DATA;
        }
        GST_DEBUG_OBJECT (dec, "Reading rectangle %d", i);
        r.x = RFB_GET_UINT16 (data + offset);
        r.y = RFB_GET_UINT16 (data + offset + 2);
        r.width = RFB_GET_UINT16 (data + offset + 4);
        r.height = RFB_GET_UINT16 (data + offset + 6);
        r.type = RFB_GET_UINT32 (data + offset + 8);

        if (r.type != TYPE_WMVi) {
          /* We must have a WMVi packet to initialise things before we can 
           * continue */
          if (!dec->have_format) {
            GST_WARNING_OBJECT (dec, "Received packet without WMVi: %d",
                r.type);
            return ERROR_INVALID;
          }
          if (r.x + r.width > dec->format.width ||
              r.y + r.height > dec->format.height) {
            GST_WARNING_OBJECT (dec, "Rectangle out of range, type %d", r.type);
            return ERROR_INVALID;
          }
        }

        switch (r.type) {
          case TYPE_WMVd:
            handler = vmnc_handle_wmvd_rectangle;
            break;
          case TYPE_WMVe:
            handler = vmnc_handle_wmve_rectangle;
            break;
          case TYPE_WMVf:
            handler = vmnc_handle_wmvf_rectangle;
            break;
          case TYPE_WMVg:
            handler = vmnc_handle_wmvg_rectangle;
            break;
          case TYPE_WMVh:
            handler = vmnc_handle_wmvh_rectangle;
            break;
          case TYPE_WMVi:
            handler = vmnc_handle_wmvi_rectangle;
            break;
          case TYPE_WMVj:
            handler = vmnc_handle_wmvj_rectangle;
            break;
          case TYPE_RAW:
            handler = vmnc_handle_raw_rectangle;
            break;
          case TYPE_COPY:
            handler = vmnc_handle_copy_rectangle;
            break;
          case TYPE_HEXTILE:
            handler = vmnc_handle_hextile_rectangle;
            break;
          default:
            GST_WARNING_OBJECT (dec, "Unknown rectangle type");
            return ERROR_INVALID;
        }

        read = handler (dec, &r, data + offset + 12, len - offset - 12, decode);
        if (read < 0) {
          GST_DEBUG_OBJECT (dec, "Error calling rectangle handler\n");
          return read;
        }
        offset += 12 + read;
      }
      break;
    }
    default:
      GST_WARNING_OBJECT (dec, "Packet type unknown: %d", type);
      return ERROR_INVALID;
  }

  return offset;
}

static gboolean
vmnc_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  /* We require a format descriptor in-stream, so we ignore the info from the
   * container here. We just use the framerate */
  GstVMncDec *dec = GST_VMNC_DEC (gst_pad_get_parent (pad));

  if (gst_caps_get_size (caps) > 0) {
    GstStructure *structure = gst_caps_get_structure (caps, 0);

    /* We gave these a default in reset(), so we don't need to check for failure
     * here */
    gst_structure_get_fraction (structure, "framerate",
        &dec->framerate_num, &dec->framerate_denom);

    dec->parsed = TRUE;
  } else {
    GST_DEBUG_OBJECT (dec, "Unparsed input");
    dec->parsed = FALSE;
  }

  gst_object_unref (dec);

  return TRUE;
}

static GstFlowReturn
vmnc_dec_chain_frame (GstVMncDec * dec, GstBuffer * inbuf,
    const guint8 * data, int len)
{
  int res;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;

  res = vmnc_handle_packet (dec, data, len, TRUE);

  if (res < 0) {
    ret = GST_FLOW_ERROR;
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Couldn't decode packet"));
  } else {
    GST_DEBUG_OBJECT (dec, "read %d bytes of %d", res, len);
    /* inbuf may be NULL; that's ok */
    outbuf = vmnc_make_buffer (dec, inbuf);
    ret = gst_pad_push (dec->srcpad, outbuf);
  }

  return ret;
}

static GstFlowReturn
vmnc_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstVMncDec *dec;
  GstFlowReturn ret = GST_FLOW_OK;

  dec = GST_VMNC_DEC (gst_pad_get_parent (pad));

  if (!dec->parsed) {
    /* Submit our input buffer to adapter, then parse. Push each frame found
     * through the decoder
     */
    int avail;
    const guint8 *data;
    int read = 0;

    gst_adapter_push (dec->adapter, buf);


    avail = gst_adapter_available (dec->adapter);
    data = gst_adapter_peek (dec->adapter, avail);

    GST_DEBUG_OBJECT (dec, "Parsing %d bytes", avail);

    while (TRUE) {
      int len = vmnc_handle_packet (dec, data, avail, FALSE);

      if (len == ERROR_INSUFFICIENT_DATA) {
        GST_DEBUG_OBJECT (dec, "Not enough data yet");
        ret = GST_FLOW_OK;
        break;
      } else if (len < 0) {
        GST_DEBUG_OBJECT (dec, "Fatal error in bitstream");
        ret = GST_FLOW_ERROR;
        break;
      }

      GST_DEBUG_OBJECT (dec, "Parsed packet: %d bytes", len);

      ret = vmnc_dec_chain_frame (dec, NULL, data, len);
      avail -= len;
      data += len;

      read += len;

      if (ret != GST_FLOW_OK)
        break;
    }
    GST_DEBUG_OBJECT (dec, "Flushing %d bytes", read);

    gst_adapter_flush (dec->adapter, read);
  } else {
    ret = vmnc_dec_chain_frame (dec, buf, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);
  }

  gst_object_unref (dec);

  return ret;
}

static GstStateChangeReturn
vmnc_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstVMncDec *dec = GST_VMNC_DEC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_vmnc_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vmnc_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
vmnc_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /*GstVMncDec *dec = GST_VMNC_DEC (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
vmnc_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /*GstVMncDec *dec = GST_VMNC_DEC (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "vmncdec", GST_RANK_PRIMARY,
          gst_vmnc_dec_get_type ()))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vmnc,
    "VmWare Video Codec plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
