/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft  
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstvirtualdub.h"

#define GST_TYPE_XSHARPEN \
  (gst_xsharpen_get_type())
#define GST_XSHARPEN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XSHARPEN,GstXsharpen))
#define GST_XSHARPEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstXsharpen))
#define GST_IS_XSHARPEN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XSHARPEN))
#define GST_IS_XSHARPEN_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XSHARPEN))

typedef struct _GstXsharpen GstXsharpen;
typedef struct _GstXsharpenClass GstXsharpenClass;

struct _GstXsharpen
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint strength, strengthinv, threshold;
  gint srcpitch, dstpitch;
};

struct _GstXsharpenClass
{
  GstElementClass parent_class;
};

GstElementDetails gst_xsharpen_details = {
  "",
  "Filter/Video/Effect",
  "LGPL",
  "Apply a sharpen effect on video" VERSION,
  "Jeremy SIMON <jsimon13@yahoo.fr>",
  "(C) 2000 Donald Graft",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  ARG_STRENGTH,
  ARG_THRESHOLD,
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static void gst_xsharpen_class_init (GstXsharpenClass * klass);
static void gst_xsharpen_init (GstXsharpen * sharpen);

static void gst_xsharpen_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_xsharpen_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_xsharpen_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

GType
gst_xsharpen_get_type (void)
{
  static GType xsharpen_type = 0;

  if (!xsharpen_type) {
    static const GTypeInfo xsharpen_info = {
      sizeof (GstXsharpenClass), NULL,
      NULL,
      (GClassInitFunc) gst_xsharpen_class_init,
      NULL,
      NULL,
      sizeof (GstXsharpen),
      0,
      (GInstanceInitFunc) gst_xsharpen_init,
    };

    xsharpen_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstXsharpen", &xsharpen_info,
        0);
  }
  return xsharpen_type;
}

static void
gst_xsharpen_class_init (GstXsharpenClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STRENGTH,
      g_param_spec_int ("strength", "strength", "strength",
          0, 255, 255, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_THRESHOLD,
      g_param_spec_int ("threshold", "threshold", "threshold",
          0, 255, 255, (GParamFlags) G_PARAM_READWRITE));

  gobject_class->set_property = gst_xsharpen_set_property;
  gobject_class->get_property = gst_xsharpen_get_property;
}

static GstPadLinkReturn
gst_xsharpen_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstXsharpen *sharpen;

  sharpen = GST_XSHARPEN (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &sharpen->width);
  gst_caps_get_int (caps, "height", &sharpen->height);

  sharpen->strengthinv = 255 - sharpen->strength;

  sharpen->dstpitch = sharpen->srcpitch = sharpen->width * sizeof (Pixel32);

  return gst_pad_try_set_caps (sharpen->srcpad, caps);
}

static void
gst_xsharpen_init (GstXsharpen * sharpen)
{
  sharpen->sinkpad =
      gst_pad_new_from_template (gst_virtualdub_sink_factory (), "sink");
  gst_pad_set_chain_function (sharpen->sinkpad, gst_xsharpen_chain);
  gst_pad_set_link_function (sharpen->sinkpad, gst_xsharpen_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (sharpen), sharpen->sinkpad);

  sharpen->srcpad =
      gst_pad_new_from_template (gst_virtualdub_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (sharpen), sharpen->srcpad);
}

static void
gst_xsharpen_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstXsharpen *xsharpen;
  GstBuffer *outbuf;
  gint x, y;
  gint r, g, b, R, G, B;
  Pixel32 p, min, max;
  gint luma, lumac, lumamax, lumamin, mindiff, maxdiff;
  Pixel32 *src_buf, *dst_buf, *src, *dst;

  xsharpen = GST_XSHARPEN (gst_pad_get_parent (pad));

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) =
      (xsharpen->width * xsharpen->height * sizeof (Pixel32));
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  src_buf = (Pixel32 *) GST_BUFFER_DATA (buf);
  dst_buf = (Pixel32 *) GST_BUFFER_DATA (outbuf);
  min = max = 0;

  /* First copy through the four border lines. */
  src = src_buf;
  dst = dst_buf;
  for (x = 0; x < xsharpen->width; x++) {
    dst[x] = src[x];
  }

  src =
      (Pixel *) ((char *) src_buf + (xsharpen->height -
          1) * xsharpen->srcpitch);
  dst =
      (Pixel *) ((char *) dst_buf + (xsharpen->height -
          1) * xsharpen->dstpitch);

  for (x = 0; x < xsharpen->width; x++) {
    dst[x] = src[x];
  }

  src = src_buf;
  dst = dst_buf;

  for (y = 0; y < xsharpen->height; y++) {
    dst[0] = src[0];
    dst[xsharpen->width - 1] = src[xsharpen->width - 1];
    src = (Pixel *) ((char *) src + xsharpen->srcpitch);
    dst = (Pixel *) ((char *) dst + xsharpen->dstpitch);
  }

  /* Now calculate and store the pixel luminances for the remaining pixels. */
  src = src_buf;
  for (y = 0; y < xsharpen->height; y++) {
    for (x = 0; x < xsharpen->width; x++) {
      r = (src[x] >> 16) & 0xff;
      g = (src[x] >> 8) & 0xff;
      b = src[x] & 0xff;
      luma = (55 * r + 182 * g + 19 * b) >> 8;
      src[x] &= 0x00ffffff;
      src[x] |= (luma << 24);
    }
    src = (Pixel *) ((char *) src + xsharpen->srcpitch);
  }

  /* Finally run the 3x3 rank-order sharpening kernel over the pixels. */
  src = (Pixel *) ((char *) src_buf + xsharpen->srcpitch);
  dst = (Pixel *) ((char *) dst_buf + xsharpen->dstpitch);

  for (y = 1; y < xsharpen->height - 1; y++) {
    for (x = 1; x < xsharpen->width - 1; x++) {
      /* Find the brightest and dimmest pixels in the 3x3 window
         surrounding the current pixel. */

      lumamax = -1;
      lumamin = 1000;

      p = ((Pixel32 *) ((char *) src - xsharpen->srcpitch))[x - 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = ((Pixel32 *) ((char *) src - xsharpen->srcpitch))[x];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = ((Pixel32 *) ((char *) src - xsharpen->srcpitch))[x + 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = src[x - 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = src[x];
      lumac = luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = src[x + 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = ((Pixel32 *) ((char *) src + xsharpen->srcpitch))[x - 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = ((Pixel32 *) ((char *) src + xsharpen->srcpitch))[x];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      p = ((Pixel32 *) ((char *) src + xsharpen->srcpitch))[x + 1];
      luma = p >> 24;
      if (luma > lumamax) {
        lumamax = luma;
        max = p;
      }
      if (luma < lumamin) {
        lumamin = luma;
        min = p;
      }

      /* Determine whether the current pixel is closer to the
         brightest or the dimmest pixel. Then compare the current
         pixel to that closest pixel. If the difference is within
         threshold, map the current pixel to the closest pixel;
         otherwise pass it through. */

      p = -1;
      if (xsharpen->strength != 0) {
        mindiff = lumac - lumamin;
        maxdiff = lumamax - lumac;
        if (mindiff > maxdiff) {
          if (maxdiff < xsharpen->threshold) {
            p = max;
          }
        } else {
          if (mindiff < xsharpen->threshold) {
            p = min;
          }
        }
      }

      if (p == -1) {
        dst[x] = src[x];
      } else {
        R = (src[x] >> 16) & 0xff;
        G = (src[x] >> 8) & 0xff;
        B = src[x] & 0xff;
        r = (p >> 16) & 0xff;
        g = (p >> 8) & 0xff;
        b = p & 0xff;
        r = (xsharpen->strength * r + xsharpen->strengthinv * R) / 255;
        g = (xsharpen->strength * g + xsharpen->strengthinv * G) / 255;
        b = (xsharpen->strength * b + xsharpen->strengthinv * B) / 255;
        dst[x] = (r << 16) | (g << 8) | b;
      }
    }
    src = (Pixel *) ((char *) src + xsharpen->srcpitch);
    dst = (Pixel *) ((char *) dst + xsharpen->dstpitch);
  }

  gst_buffer_unref (buf);

  gst_pad_push (xsharpen->srcpad, GST_DATA (outbuf));
}

static void
gst_xsharpen_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXsharpen *xsharpen;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XSHARPEN (object));

  xsharpen = GST_XSHARPEN (object);

  switch (prop_id) {
    case ARG_STRENGTH:
      xsharpen->strength = g_value_get_int (value);
      xsharpen->strengthinv = 255 - xsharpen->strength;
    case ARG_THRESHOLD:
      xsharpen->threshold = g_value_get_int (value);
    default:
      break;
  }
}

static void
gst_xsharpen_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstXsharpen *xsharpen;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_XSHARPEN (object));

  xsharpen = GST_XSHARPEN (object);

  switch (prop_id) {
    case ARG_STRENGTH:
      g_value_set_int (value, xsharpen->strength);
      break;
    case ARG_THRESHOLD:
      g_value_set_int (value, xsharpen->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
