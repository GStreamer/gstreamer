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
#include <zlib.h>

GST_DEBUG_CATEGORY_STATIC (pngenc_debug);
#define GST_CAT_DEFAULT pngenc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SNAPSHOT                FALSE
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
        "width = (int) [ 16, 1000000 ], "
        "height = (int) [ 16, 1000000 ], " "framerate = " GST_VIDEO_FPS_RANGE)
    );

static GstStaticPadTemplate pngenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBA, RGB, GRAY8, GRAY16_BE }"))
    );

/* static GstElementClass *parent_class = NULL; */

G_DEFINE_TYPE (GstPngEnc, gst_pngenc, GST_TYPE_ELEMENT);

static void gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_pngenc_chain (GstPad * pad, GstObject * parent,
    GstBuffer * data);
static gboolean gst_pngenc_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

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
gst_pngenc_class_init (GstPngEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_pngenc_get_property;
  gobject_class->set_property = gst_pngenc_set_property;

  g_object_class_install_property (gobject_class, ARG_SNAPSHOT,
      g_param_spec_boolean ("snapshot", "Snapshot",
          "Send EOS after encoding a frame, useful for snapshots",
          DEFAULT_SNAPSHOT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

/*   g_object_class_install_property (gobject_class, ARG_NEWMEDIA, */
/*       g_param_spec_boolean ("newmedia", "newmedia", */
/*           "Send new media discontinuity after encoding each frame", */
/*           DEFAULT_NEWMEDIA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)); */

  g_object_class_install_property (gobject_class, ARG_COMPRESSION_LEVEL,
      g_param_spec_uint ("compression-level", "compression-level",
          "PNG compression level",
          Z_NO_COMPRESSION, Z_BEST_COMPRESSION,
          DEFAULT_COMPRESSION_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_sink_template));
  gst_element_class_add_pad_template
      (element_class, gst_static_pad_template_get (&pngenc_src_template));
  gst_element_class_set_static_metadata (element_class, "PNG image encoder",
      "Codec/Encoder/Image",
      "Encode a video frame to a .png image",
      "Jeremy SIMON <jsimon13@yahoo.fr>");

  GST_DEBUG_CATEGORY_INIT (pngenc_debug, "pngenc", 0, "PNG image encoder");
}


static gboolean
gst_pngenc_setcaps (GstPngEnc * pngenc, GstCaps * caps)
{
  int fps_n, fps_d;
  GstCaps *pcaps;
  gboolean ret;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);

  if (G_UNLIKELY (!ret))
    goto done;

  pngenc->info = info;

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_RGBA:
      pngenc->png_color_type = PNG_COLOR_TYPE_RGBA;
      pngenc->depth = 8;
      break;
    case GST_VIDEO_FORMAT_RGB:
      pngenc->png_color_type = PNG_COLOR_TYPE_RGB;
      pngenc->depth = 8;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      pngenc->png_color_type = PNG_COLOR_TYPE_GRAY;
      pngenc->depth = 8;
      break;
    case GST_VIDEO_FORMAT_GRAY16_BE:
      pngenc->png_color_type = PNG_COLOR_TYPE_GRAY;
      pngenc->depth = 16;
      break;
    default:
      ret = FALSE;
      goto done;
  }

  pngenc->width = GST_VIDEO_INFO_WIDTH (&info);
  pngenc->height = GST_VIDEO_INFO_HEIGHT (&info);
  fps_n = GST_VIDEO_INFO_FPS_N (&info);
  fps_d = GST_VIDEO_INFO_FPS_D (&info);

  if (G_UNLIKELY (pngenc->width < 16 || pngenc->width > 1000000 ||
          pngenc->height < 16 || pngenc->height > 1000000)) {
    ret = FALSE;
    goto done;
  }

  pcaps = gst_caps_new_simple ("image/png",
      "width", G_TYPE_INT, pngenc->width,
      "height", G_TYPE_INT, pngenc->height,
      "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

  ret = gst_pad_set_caps (pngenc->srcpad, pcaps);

  gst_caps_unref (pcaps);

  /* Fall-through. */
done:
  if (G_UNLIKELY (!ret)) {
    pngenc->width = 0;
    pngenc->height = 0;
  }

  return ret;
}

static void
gst_pngenc_init (GstPngEnc * pngenc)
{
  /* sinkpad */
  pngenc->sinkpad = gst_pad_new_from_static_template
      (&pngenc_sink_template, "sink");
  gst_pad_set_chain_function (pngenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pngenc_chain));
  gst_pad_set_event_function (pngenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pngenc_sink_event));
  gst_element_add_pad (GST_ELEMENT (pngenc), pngenc->sinkpad);

  /* srcpad */
  pngenc->srcpad = gst_pad_new_from_static_template
      (&pngenc_src_template, "src");
  gst_pad_use_fixed_caps (pngenc->srcpad);
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
  GstMapInfo map;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  gst_buffer_map (pngenc->buffer_out, &map, GST_MAP_WRITE);
  if (pngenc->written + length >= map.size) {
    gst_buffer_unmap (pngenc->buffer_out, &map);
    GST_ERROR_OBJECT (pngenc, "output buffer bigger than the input buffer!?");
    png_error (png_ptr, "output buffer bigger than the input buffer!?");

    /* never reached */
    return;
  }

  GST_DEBUG_OBJECT (pngenc, "writing %u bytes", (guint) length);

  memcpy (map.data + pngenc->written, data, length);
  gst_buffer_unmap (pngenc->buffer_out, &map);
  pngenc->written += length;
}

static GstFlowReturn
gst_pngenc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstPngEnc *pngenc;
  gint row_index;
  png_byte **row_pointers;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *encoded_buf = NULL;
  GstVideoFrame frame;

  pngenc = GST_PNGENC (parent);

  GST_DEBUG_OBJECT (pngenc, "BEGINNING");

  if (G_UNLIKELY (pngenc->width <= 0 || pngenc->height <= 0)) {
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto exit;
  }

  if (!gst_video_frame_map (&frame, &pngenc->info, buf, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (pngenc, STREAM, FORMAT, (NULL),
        ("Failed to map video frame, caps problem?"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }

  /* initialize png struct stuff */
  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);
  if (pngenc->png_struct_ptr == NULL) {
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);
  if (!pngenc->png_info_ptr) {
    png_destroy_write_struct (&(pngenc->png_struct_ptr), (png_infopp) NULL);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize the png info structure"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (png_jmpbuf (pngenc->png_struct_ptr)) != 0) {
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
      pngenc->depth,
      pngenc->png_color_type,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_write_fn (pngenc->png_struct_ptr, pngenc,
      (png_rw_ptr) user_write_data, user_flush_data);

  row_pointers = g_new (png_byte *, pngenc->height);

  for (row_index = 0; row_index < pngenc->height; row_index++) {
    row_pointers[row_index] = GST_VIDEO_FRAME_COMP_DATA (&frame, 0) +
        (row_index * GST_VIDEO_FRAME_COMP_STRIDE (&frame, 0));
  }

  /* allocate the output buffer */
  pngenc->buffer_out =
      gst_buffer_new_and_alloc (pngenc->height * pngenc->width);
  pngenc->written = 0;

  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  g_free (row_pointers);

  GST_DEBUG_OBJECT (pngenc, "written %d", pngenc->written);

  encoded_buf =
      gst_buffer_copy_region (pngenc->buffer_out, GST_BUFFER_COPY_MEMORY,
      0, pngenc->written);

  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp) NULL);

  GST_BUFFER_TIMESTAMP (encoded_buf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (encoded_buf) = GST_BUFFER_DURATION (buf);

  if ((ret = gst_pad_push (pngenc->srcpad, encoded_buf)) != GST_FLOW_OK)
    goto done;

  if (pngenc->snapshot) {
    GstEvent *event;

    GST_DEBUG_OBJECT (pngenc, "snapshot mode, sending EOS");
    /* send EOS event, since a frame has been pushed out */
    event = gst_event_new_eos ();

    gst_pad_push_event (pngenc->srcpad, event);
    ret = GST_FLOW_EOS;
  }

done:
  gst_video_frame_unmap (&frame);
exit:
  gst_buffer_unref (buf);
  GST_DEBUG_OBJECT (pngenc, "END, ret:%d", ret);

  if (pngenc->buffer_out != NULL) {
    gst_buffer_unref (pngenc->buffer_out);
    pngenc->buffer_out = NULL;
  }

  return ret;
}

static gboolean
gst_pngenc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstPngEnc *enc;
  gboolean res;

  enc = GST_PNGENC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_pngenc_setcaps (enc, caps);
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_push_event (enc->srcpad, event);
      break;
  }
  return res;
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
