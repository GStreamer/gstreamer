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
#include <string.h>
#include <gst/gst.h>
#include "gstpngdec.h"
#include <gst/video/video.h>

static GstElementDetails gst_pngdec_details = {
  "PNG decoder",
  "Codec/Decoder/Image",
  "Decode a png video frame to a raw image",
  "Wim Taymans <wim@fluendo.com>",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_pngdec_base_init (gpointer g_class);
static void gst_pngdec_class_init (GstPngDecClass * klass);
static void gst_pngdec_init (GstPngDec * pngdec);

static void gst_pngdec_chain (GstPad * pad, GstData * _data);

static GstCaps *gst_pngdec_src_getcaps (GstPad * pad);

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
    GST_STATIC_CAPS ("video/x-png, "
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
}


static GstPadLinkReturn
gst_pngdec_sinklink (GstPad * pad, const GstCaps * caps)
{
  GstPngDec *pngdec;
  GstStructure *structure;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &pngdec->fps);

  return TRUE;
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
  gst_pad_set_link_function (pngdec->sinkpad, gst_pngdec_sinklink);

  gst_pad_set_getcaps_function (pngdec->srcpad, gst_pngdec_src_getcaps);

  pngdec->png = NULL;
  pngdec->info = NULL;

  pngdec->color_type = -1;
  pngdec->width = -1;
  pngdec->height = -1;
  pngdec->fps = -1;
}

static GstCaps *
gst_pngdec_src_getcaps (GstPad * pad)
{
  GstPngDec *pngdec;
  GstCaps *caps;
  gint i;
  GstPadTemplate *templ;
  GstCaps *inter;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));
  templ = gst_static_pad_template_get (&gst_pngdec_src_pad_template);
  caps = gst_caps_copy (gst_pad_template_get_caps (templ));

  if (pngdec->color_type != -1) {
    GstCaps *to_inter = NULL;

    switch (pngdec->color_type) {
      case PNG_COLOR_TYPE_RGB:
        to_inter = gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, 24, NULL);
        break;
      case PNG_COLOR_TYPE_RGB_ALPHA:
        to_inter = gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, 32, NULL);
        break;
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_PALETTE:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
      default:
        g_warning ("unsupported colortype");
        break;
    }
    inter = gst_caps_intersect (caps, to_inter);
    gst_caps_free (caps);
    gst_caps_free (to_inter);
    caps = inter;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    if (pngdec->width != -1) {
      gst_structure_set (structure, "width", G_TYPE_INT, pngdec->width, NULL);
    }

    if (pngdec->height != -1) {
      gst_structure_set (structure, "height", G_TYPE_INT, pngdec->height, NULL);
    }

    if (pngdec->fps != -1) {
      gst_structure_set (structure,
          "framerate", G_TYPE_DOUBLE, pngdec->fps, NULL);
    }
  }

  return caps;
}

static void
user_read_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
  GstPngDec *dec;

  dec = GST_PNGDEC (png_ptr->io_ptr);

  if (GST_BUFFER_SIZE (dec->buffer_in) < dec->offset + length) {
    g_warning ("reading past end of buffer");
  }

  memcpy (data, GST_BUFFER_DATA (dec->buffer_in) + dec->offset, length);

  dec->offset += length;
}

static void
gst_pngdec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstPngDec *pngdec;
  png_uint_32 width, height;
  gint depth, color;
  png_bytep *rows, inp;
  GstBuffer *out;
  gint i;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  if (!GST_PAD_IS_USABLE (pngdec->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }

  pngdec->buffer_in = buf;
  pngdec->offset = 0;

  /* initialize png struct stuff */
  pngdec->png = png_create_read_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);

  /* FIXME: better error handling */
  if (pngdec->png == NULL) {
    g_warning ("Failed to initialize png structure");
    return;
  }

  pngdec->info = png_create_info_struct (pngdec->png);
  if (pngdec->info == NULL) {
    g_warning ("Failed to initialize info structure");
    png_destroy_read_struct (&(pngdec->png), (png_infopp) NULL,
        (png_infopp) NULL);
    return;
  }

  pngdec->endinfo = png_create_info_struct (pngdec->png);
  if (pngdec->endinfo == NULL) {
    g_warning ("Failed to initialize endinfo structure");
    return;
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (pngdec->png->jmpbuf) != 0) {
    GST_DEBUG ("returning from longjmp");
    png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);
    return;
  }

  png_set_read_fn (pngdec->png, pngdec, user_read_data);

  png_read_info (pngdec->png, pngdec->info);
  png_get_IHDR (pngdec->png, pngdec->info, &width, &height,
      &depth, &color, NULL, NULL, NULL);

  GST_LOG ("color type: %d\n", pngdec->info->color_type);

  if (pngdec->width != width ||
      pngdec->height != height ||
      pngdec->color_type != pngdec->info->color_type) {
    pngdec->width = width;
    pngdec->height = height;
    pngdec->color_type = pngdec->info->color_type;

    if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (pngdec->srcpad))) {
      GST_ELEMENT_ERROR (pngdec, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }
  }

  rows = (png_bytep *) g_malloc (sizeof (png_bytep) * height);

  out =
      gst_pad_alloc_buffer (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      width * height * 4);

  inp = GST_BUFFER_DATA (out);
  for (i = 0; i < height; i++) {
    rows[i] = inp;
    inp += width * 4;
  }

  png_read_image (pngdec->png, rows);
  free (rows);

  png_destroy_info_struct (pngdec->png, &pngdec->info);
  png_destroy_read_struct (&pngdec->png, &pngdec->info, &pngdec->endinfo);

  pngdec->buffer_in = NULL;
  gst_buffer_unref (buf);

  gst_pad_push (pngdec->srcpad, GST_DATA (out));
}
