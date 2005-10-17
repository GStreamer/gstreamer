/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstpngdec.h"

#include <string.h>
#include <gst/video/video.h>

static GstElementDetails gst_pngdec_details = {
  "PNG decoder",
  "Codec/Decoder/Image",
  "Decode a png video frame to a raw image",
  "Wim Taymans <wim@fluendo.com>",
};

GST_DEBUG_CATEGORY (pngdec_debug);
#define GST_CAT_DEFAULT pngdec_debug

static void gst_pngdec_base_init (gpointer g_class);
static void gst_pngdec_class_init (GstPngDecClass * klass);
static void gst_pngdec_init (GstPngDec * pngdec);

static GstFlowReturn gst_pngdec_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

static void
user_error_fn (png_structp png_ptr, png_const_charp error_msg)
{
  g_warning ("%s", error_msg);
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning ("%s", warning_msg);
}

GType
gst_pngdec_get_type (void)
{
  static GType pngdec_type = 0;

  if (!pngdec_type) {
    static const GTypeInfo pngdec_info = {
      sizeof (GstPngDecClass),
      gst_pngdec_base_init,
      NULL,
      (GClassInitFunc) gst_pngdec_class_init,
      NULL,
      NULL,
      sizeof (GstPngDec),
      0,
      (GInstanceInitFunc) gst_pngdec_init,
    };

    pngdec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPngDec",
        &pngdec_info, 0);
  }
  return pngdec_type;
}

static GstStaticPadTemplate gst_pngdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate gst_pngdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 0.0, MAX ]")
    );

static void
gst_pngdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pngdec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pngdec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_pngdec_details);
}

static void
gst_pngdec_class_init (GstPngDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (pngdec_debug, "pngdec", 0, "PNG image decoder");
}


static void
gst_pngdec_init (GstPngDec * pngdec)
{
  pngdec->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_pngdec_sink_pad_template), "sink");
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->sinkpad);

  pngdec->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_pngdec_src_pad_template), "src");
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->srcpad);

  gst_pad_set_chain_function (pngdec->sinkpad, gst_pngdec_chain);

  pngdec->png = NULL;
  pngdec->info = NULL;

  pngdec->color_type = -1;
  pngdec->width = -1;
  pngdec->height = -1;
  pngdec->fps = 0.0;
}

static void
user_read_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
  GstPngDec *dec;

  dec = GST_PNGDEC (png_ptr->io_ptr);

  GST_LOG ("reading %d bytes of data at offset %d", length, dec->offset);

  if (GST_BUFFER_SIZE (dec->buffer_in) < dec->offset + length) {
    g_warning ("reading past end of buffer");
  }

  memcpy (data, GST_BUFFER_DATA (dec->buffer_in) + dec->offset, length);

  dec->offset += length;
}

#define ROUND_UP_4(x) (((x) + 3) & ~3)

static GstFlowReturn
gst_pngdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstPngDec *pngdec;
  png_uint_32 width, height;
  gint depth, color;
  png_bytep *rows, inp;
  GstBuffer *out;
  gint i;

  GST_LOG ("chain");

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  if (!GST_PAD_IS_USABLE (pngdec->srcpad)) {
    gst_buffer_unref (buf);
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  pngdec->buffer_in = buf;
  pngdec->offset = 0;

  /* initialize png struct stuff */
  pngdec->png = png_create_read_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);

  if (pngdec->png == NULL) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  pngdec->info = png_create_info_struct (pngdec->png);
  if (pngdec->info == NULL) {
    gst_buffer_unref (buf);
    png_destroy_read_struct (&(pngdec->png), (png_infopp) NULL,
        (png_infopp) NULL);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize info structure"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  pngdec->endinfo = png_create_info_struct (pngdec->png);
  if (pngdec->endinfo == NULL) {
    gst_buffer_unref (buf);
    png_destroy_read_struct (&pngdec->png, &pngdec->info, (png_infopp) NULL);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize endinfo structure"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (pngdec->png->jmpbuf) != 0) {
    gst_buffer_unref (buf);
    png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, FAILED, (NULL),
        ("returning from longjmp"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  png_set_read_fn (pngdec->png, pngdec, user_read_data);

  png_read_info (pngdec->png, pngdec->info);
  png_get_IHDR (pngdec->png, pngdec->info, &width, &height,
      &depth, &color, NULL, NULL, NULL);

  if (pngdec->info->bit_depth != 8) {
    gst_buffer_unref (buf);
    png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);
    GST_ELEMENT_ERROR (pngdec, STREAM, NOT_IMPLEMENTED, (NULL),
        ("pngdec only supports 8 bit images for now"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  if (pngdec->width != width ||
      pngdec->height != height ||
      pngdec->color_type != pngdec->info->color_type) {
    GstCaps *caps, *templ, *res;
    gboolean ret;
    gint bpp;

    GST_LOG ("this is a %dx%d PNG image", width, height);
    pngdec->width = width;
    pngdec->height = height;
    pngdec->color_type = pngdec->info->color_type;

    switch (pngdec->color_type) {
      case PNG_COLOR_TYPE_RGB:
        GST_LOG ("we have no alpha channel, depth is 24 bits");
        bpp = 24;
        break;
      case PNG_COLOR_TYPE_RGB_ALPHA:
        GST_LOG ("we have an alpha channel, depth is 32 bits");
        bpp = 32;
        break;
      default:
        GST_ELEMENT_ERROR (pngdec, STREAM, NOT_IMPLEMENTED, (NULL),
            ("pngdec does not support grayscale or paletted data yet"));
        ret = GST_FLOW_UNEXPECTED;
        goto beach;
    }

    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "bpp", G_TYPE_INT, bpp, "framerate", G_TYPE_DOUBLE, pngdec->fps, NULL);

    templ = gst_caps_copy (gst_pad_template_get_caps
        (gst_static_pad_template_get (&gst_pngdec_src_pad_template)));

    res = gst_caps_intersect (templ, caps);
    gst_caps_unref (caps);
    gst_caps_unref (templ);
    {
      gchar *caps_str = gst_caps_to_string (res);

      GST_LOG ("intersecting caps with template gave %s", caps_str);
      g_free (caps_str);
    }
    ret = gst_pad_set_caps (pngdec->srcpad, res);
    gst_caps_unref (res);

    if (!ret) {
      gst_buffer_unref (buf);
      png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);
      GST_ELEMENT_ERROR (pngdec, CORE, NEGOTIATION, (NULL), (NULL));
      ret = GST_FLOW_UNEXPECTED;
      goto beach;
    }
  }

  rows = (png_bytep *) g_malloc (sizeof (png_bytep) * height);

  ret = gst_pad_alloc_buffer (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      height * ROUND_UP_4 (pngdec->info->rowbytes),
      GST_PAD_CAPS (pngdec->srcpad), &out);

  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  inp = GST_BUFFER_DATA (out);
  for (i = 0; i < height; i++) {
    rows[i] = inp;
    inp += ROUND_UP_4 (pngdec->info->rowbytes);
  }

  png_read_image (pngdec->png, rows);
  free (rows);

  png_destroy_info_struct (pngdec->png, &pngdec->info);
  png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);

  pngdec->buffer_in = NULL;
  gst_buffer_unref (buf);

  /* Pushing */
  GST_LOG ("pushing our raw RGB frame of %d bytes", GST_BUFFER_SIZE (out));
  ret = gst_pad_push (pngdec->srcpad, out);

beach:
  return ret;
}
