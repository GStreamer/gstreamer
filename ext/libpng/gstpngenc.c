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
/**
 * SECTION:element-pngenc
 *
 * Encodes png images.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstpngenc.h"
#include <gst/video/video.h>
#include <zlib.h>

#define MAX_HEIGHT              4096


static const GstElementDetails gst_pngenc_details =
GST_ELEMENT_DETAILS ("PNG image encoder",
    "Codec/Encoder/Image",
    "Encode a video frame to a .png image",
    "Jeremy SIMON <jsimon13@yahoo.fr>");

GST_DEBUG_CATEGORY_STATIC (pngenc_debug);
#define GST_CAT_DEFAULT pngenc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SNAPSHOT                TRUE
/* #define DEFAULT_NEWMEDIA             FALSE */
#define DEFAULT_COMPRESSION_LEVEL       6

enum
{
  ARG_0,
  ARG_SNAPSHOT,
/*   ARG_NEWMEDIA, */
  ARG_COMPRESSION_LEVEL
};

static GstStaticPadTemplate pngenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0, MAX ]")
    );

static GstStaticPadTemplate pngenc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

/* static GstElementClass *parent_class = NULL; */

GST_BOILERPLATE (GstPngEnc, gst_pngenc, GstElement, GST_TYPE_ELEMENT);

static void gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_pngenc_chain (GstPad * pad, GstBuffer * data);

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

static void
gst_pngenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_sink_template));
  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_src_template));
  gst_element_class_set_details (element_class, &gst_pngenc_details);
}

static void
gst_pngenc_class_init (GstPngEncClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_pngenc_get_property;
  gobject_class->set_property = gst_pngenc_set_property;

  g_object_class_install_property (gobject_class, ARG_SNAPSHOT,
      g_param_spec_boolean ("snapshot", "Snapshot",
          "Send EOS after encoding a frame, useful for snapshots",
          DEFAULT_SNAPSHOT, (GParamFlags) G_PARAM_READWRITE));

/*   g_object_class_install_property (gobject_class, ARG_NEWMEDIA, */
/*       g_param_spec_boolean ("newmedia", "newmedia", */
/*           "Send new media discontinuity after encoding each frame", */
/*           DEFAULT_NEWMEDIA, (GParamFlags) G_PARAM_READWRITE)); */

  g_object_class_install_property
      (gobject_class, ARG_COMPRESSION_LEVEL,
      g_param_spec_uint ("compression-level", "compression-level",
          "PNG compression level",
          Z_NO_COMPRESSION, Z_BEST_COMPRESSION,
          DEFAULT_COMPRESSION_LEVEL, (GParamFlags) G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (pngenc_debug, "pngenc", 0, "PNG image encoder");
}


static gboolean
gst_pngenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPngEnc *pngenc;
  const GValue *fps;
  GstStructure *structure;
  GstCaps *pcaps;
  gboolean ret = TRUE;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &pngenc->width);
  gst_structure_get_int (structure, "height", &pngenc->height);
  fps = gst_structure_get_value (structure, "framerate");
  gst_structure_get_int (structure, "bpp", &pngenc->bpp);

  if (pngenc->bpp == 32)
    pngenc->stride = pngenc->width * 4;
  else
    pngenc->stride = GST_ROUND_UP_4 (pngenc->width * 3);

  pcaps = gst_caps_new_simple ("image/png",
      "width", G_TYPE_INT, pngenc->width,
      "height", G_TYPE_INT, pngenc->height, NULL);
  structure = gst_caps_get_structure (pcaps, 0);
  gst_structure_set_value (structure, "framerate", fps);

  ret = gst_pad_set_caps (pngenc->srcpad, pcaps);

  gst_caps_unref (pcaps);
  gst_object_unref (pngenc);

  return ret;
}

static void
gst_pngenc_init (GstPngEnc * pngenc, GstPngEncClass * g_class)
{
  /* sinkpad */
  pngenc->sinkpad = gst_pad_new_from_static_template
      (&pngenc_sink_template, "sink");
  gst_pad_set_chain_function (pngenc->sinkpad, gst_pngenc_chain);
  /*   gst_pad_set_link_function (pngenc->sinkpad, gst_pngenc_sinklink); */
  /*   gst_pad_set_getcaps_function (pngenc->sinkpad, gst_pngenc_sink_getcaps); */
  gst_pad_set_setcaps_function (pngenc->sinkpad, gst_pngenc_setcaps);
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->sinkpad);

  /* srcpad */
  pngenc->srcpad = gst_pad_new_from_static_template
      (&pngenc_src_template, "src");
  /*   pngenc->srcpad = gst_pad_new ("src", GST_PAD_SRC); */
  /*   gst_pad_set_getcaps_function (pngenc->srcpad, gst_pngenc_src_getcaps); */
  /*   gst_pad_set_setcaps_function (pngenc->srcpad, gst_pngenc_setcaps); */
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->srcpad);

  /* init settings */
  pngenc->png_struct_ptr = NULL;
  pngenc->png_info_ptr = NULL;

  pngenc->snapshot = DEFAULT_SNAPSHOT;
/*   pngenc->newmedia = FALSE; */
  pngenc->compression_level = DEFAULT_COMPRESSION_LEVEL;
}

static void
user_flush_data (png_structp png_ptr G_GNUC_UNUSED)
{
}

static void
user_write_data (png_structp png_ptr, png_bytep data, png_uint_32 length)
{
  GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  if (pngenc->written + length >= GST_BUFFER_SIZE (pngenc->buffer_out)) {
    GST_ERROR_OBJECT (pngenc, "output buffer bigger than the input buffer!?");
    png_error (png_ptr, "output buffer bigger than the input buffer!?");

    /* never reached */
    return;
  }

  memcpy (GST_BUFFER_DATA (pngenc->buffer_out) + pngenc->written, data, length);
  pngenc->written += length;
}

static GstFlowReturn
gst_pngenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstPngEnc *pngenc;
  gint row_index;
  png_byte *row_pointers[MAX_HEIGHT];
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *encoded_buf = NULL;

  pngenc = GST_PNGENC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pngenc, "BEGINNING");

  /* initialize png struct stuff */
  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);
  if (pngenc->png_struct_ptr == NULL) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);
  if (!pngenc->png_info_ptr) {
    gst_buffer_unref (buf);
    png_destroy_write_struct (&(pngenc->png_struct_ptr), (png_infopp) NULL);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize the png info structure"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (png_jmpbuf (pngenc->png_struct_ptr)) != 0) {
    gst_buffer_unref (buf);
    png_destroy_write_struct (&pngenc->png_struct_ptr, &pngenc->png_info_ptr);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, FAILED, (NULL),
        ("returning from longjmp"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  png_set_filter (pngenc->png_struct_ptr, 0,
      PNG_FILTER_NONE | PNG_FILTER_VALUE_NONE);
  png_set_compression_level (pngenc->png_struct_ptr, pngenc->compression_level);

  png_set_IHDR (pngenc->png_struct_ptr,
      pngenc->png_info_ptr,
      pngenc->width,
      pngenc->height,
      8,
      (pngenc->bpp == 32) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_write_fn (pngenc->png_struct_ptr, pngenc,
      (png_rw_ptr) user_write_data, user_flush_data);

  for (row_index = 0; row_index < pngenc->height; row_index++) {
    row_pointers[row_index] = GST_BUFFER_DATA (buf) +
        (row_index * pngenc->stride);
  }

  /* allocate the output buffer */
  pngenc->buffer_out =
      gst_buffer_new_and_alloc (pngenc->height * pngenc->stride);
  pngenc->written = 0;

  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  encoded_buf = gst_buffer_create_sub (pngenc->buffer_out, 0, pngenc->written);

  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp) NULL);
  gst_buffer_copy_metadata (encoded_buf, buf, GST_BUFFER_COPY_TIMESTAMPS);
  gst_buffer_unref (buf);
  gst_buffer_set_caps (encoded_buf, GST_PAD_CAPS (pngenc->srcpad));

  if ((ret = gst_pad_push (pngenc->srcpad, encoded_buf)) != GST_FLOW_OK)
    goto done;

  if (pngenc->snapshot) {
    GstEvent *event;

    GST_DEBUG_OBJECT (pngenc, "snapshot mode, sending EOS");
    /* send EOS event, since a frame has been pushed out */
    event = gst_event_new_eos ();

    gst_pad_push_event (pngenc->srcpad, event);
    ret = GST_FLOW_UNEXPECTED;
  }

done:
  GST_DEBUG_OBJECT (pngenc, "END, ret:%d", ret);

  if (pngenc->buffer_out != NULL) {
    gst_buffer_unref (pngenc->buffer_out);
    pngenc->buffer_out = NULL;
  }

  gst_object_unref (pngenc);
  return ret;
}


static void
gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    case ARG_SNAPSHOT:
      g_value_set_boolean (value, pngenc->snapshot);
      break;
/*     case ARG_NEWMEDIA: */
/*       g_value_set_boolean (value, pngenc->newmedia); */
/*       break; */
    case ARG_COMPRESSION_LEVEL:
      g_value_set_uint (value, pngenc->compression_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPngEnc *pngenc;

  pngenc = GST_PNGENC (object);

  switch (prop_id) {
    case ARG_SNAPSHOT:
      pngenc->snapshot = g_value_get_boolean (value);
      break;
/*     case ARG_NEWMEDIA: */
/*       pngenc->newmedia = g_value_get_boolean (value); */
/*       break; */
    case ARG_COMPRESSION_LEVEL:
      pngenc->compression_level = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
