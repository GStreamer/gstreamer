/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
 *
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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
 *
 */
/**
 * SECTION:element-pngenc
 * @title: pngenc
 *
 * Encodes png images.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <zlib.h>

#include "gstpngenc.h"

GST_DEBUG_CATEGORY_STATIC (pngenc_debug);
#define GST_CAT_DEFAULT pngenc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SNAPSHOT                FALSE
#define DEFAULT_COMPRESSION_LEVEL       6

enum
{
  ARG_0,
  ARG_SNAPSHOT,
  ARG_COMPRESSION_LEVEL
};

static GstStaticPadTemplate pngenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png, "
        "width = (int) [ 1, 1000000 ], "
        "height = (int) [ 1, 1000000 ], " "framerate = " GST_VIDEO_FPS_RANGE)
    );

static GstStaticPadTemplate pngenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBA, RGB, GRAY8, GRAY16_BE }"))
    );

#define parent_class gst_pngenc_parent_class
G_DEFINE_TYPE (GstPngEnc, gst_pngenc, GST_TYPE_VIDEO_ENCODER);
GST_ELEMENT_REGISTER_DEFINE (pngenc, "pngenc", GST_RANK_PRIMARY,
    GST_TYPE_PNGENC);

static void gst_pngenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pngenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_pngenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_pngenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_pngenc_flush (GstVideoEncoder * encoder);
static gboolean gst_pngenc_start (GstVideoEncoder * encoder);
static gboolean gst_pngenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_pngenc_finalize (GObject * object);

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
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_pngenc_get_property;
  gobject_class->set_property = gst_pngenc_set_property;

  g_object_class_install_property (gobject_class, ARG_SNAPSHOT,
      g_param_spec_boolean ("snapshot", "Snapshot",
          "Send EOS after encoding a frame, useful for snapshots",
          DEFAULT_SNAPSHOT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_COMPRESSION_LEVEL,
      g_param_spec_uint ("compression-level", "compression-level",
          "PNG compression level",
          Z_NO_COMPRESSION, Z_BEST_COMPRESSION,
          DEFAULT_COMPRESSION_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template
      (element_class, &pngenc_sink_template);
  gst_element_class_add_static_pad_template
      (element_class, &pngenc_src_template);
  gst_element_class_set_static_metadata (element_class, "PNG image encoder",
      "Codec/Encoder/Image",
      "Encode a video frame to a .png image",
      "Jeremy SIMON <jsimon13@yahoo.fr>");

  venc_class->set_format = gst_pngenc_set_format;
  venc_class->handle_frame = gst_pngenc_handle_frame;
  venc_class->propose_allocation = gst_pngenc_propose_allocation;
  venc_class->flush = gst_pngenc_flush;
  venc_class->start = gst_pngenc_start;
  gobject_class->finalize = gst_pngenc_finalize;

  GST_DEBUG_CATEGORY_INIT (pngenc_debug, "pngenc", 0, "PNG image encoder");
}


static gboolean
gst_pngenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstPngEnc *pngenc;
  gboolean ret = TRUE;
  GstVideoInfo *info;
  GstVideoCodecState *output_state;

  pngenc = GST_PNGENC (encoder);
  info = &state->info;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGBA:
      pngenc->png_color_type = PNG_COLOR_TYPE_RGBA;
      break;
    case GST_VIDEO_FORMAT_RGB:
      pngenc->png_color_type = PNG_COLOR_TYPE_RGB;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      pngenc->png_color_type = PNG_COLOR_TYPE_GRAY;
      break;
    default:
      ret = FALSE;
      goto done;
  }

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_GRAY16_BE:
      pngenc->depth = 16;
      break;
    default:                   /* GST_VIDEO_FORMAT_RGB and GST_VIDEO_FORMAT_GRAY8 */
      pngenc->depth = 8;
      break;
  }

  if (pngenc->input_state)
    gst_video_codec_state_unref (pngenc->input_state);
  pngenc->input_state = gst_video_codec_state_ref (state);

  output_state =
      gst_video_encoder_set_output_state (encoder,
      gst_caps_new_empty_simple ("image/png"), state);
  gst_video_codec_state_unref (output_state);

done:

  return ret;
}

static void
gst_pngenc_init (GstPngEnc * pngenc)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (pngenc));

  /* init settings */
  pngenc->png_struct_ptr = NULL;
  pngenc->png_info_ptr = NULL;

  pngenc->snapshot = DEFAULT_SNAPSHOT;
  pngenc->compression_level = DEFAULT_COMPRESSION_LEVEL;
}

static void
gst_pngenc_finalize (GObject * object)
{
  GstPngEnc *pngenc = GST_PNGENC (object);

  if (pngenc->input_state)
    gst_video_codec_state_unref (pngenc->input_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
user_flush_data (png_structp png_ptr G_GNUC_UNUSED)
{
}

/* Copied from glib/gutilsprivate.h
 *
 * Returns the smallest power of 2 greater than or equal to n,
 * or 0 if such power does not fit in a gsize
 */
static inline gsize
gst_pngenc_g_nearest_pow (gsize num)
{
  gsize n = num - 1;

  g_assert (num > 0 && num <= G_MAXSIZE / 2);

  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
#if GLIB_SIZEOF_SIZE_T == 8
  n |= n >> 32;
#endif

  return n + 1;
}

static void
ensure_memory_is_enough (GstPngEnc * pngenc, unsigned int extra_length)
{
  GstMemory *new_memory;
  GstMapInfo map;
  gsize old_size, desired_size;
  guint8 *new_data;

  old_size = pngenc->output_map.size;
  desired_size = gst_pngenc_g_nearest_pow (old_size + extra_length);
  g_assert (desired_size != 0);

  /* Our output memory wasn't big enough.
   * Make a new memory that's twice the size, */
  new_memory = gst_allocator_alloc (NULL, desired_size, NULL);
  gst_memory_map (new_memory, &map, GST_MAP_READWRITE);
  new_data = map.data;

  /* copy previous data  */
  memcpy (new_data, pngenc->output_map.data, old_size);
  gst_memory_unmap (pngenc->output_mem, &pngenc->output_map);
  gst_memory_unref (pngenc->output_mem);

  /* drop it into place, */
  pngenc->output_mem = new_memory;
  pngenc->output_map = map;
}

static void
user_write_data (png_structp png_ptr, png_bytep data, png_uint_32 length)
{
  GstPngEnc *pngenc;

  pngenc = (GstPngEnc *) png_get_io_ptr (png_ptr);

  GST_TRACE_OBJECT (pngenc,
      "Memory size: %" G_GSIZE_FORMAT "\nLength to be written: %u",
      pngenc->output_map.size, length);

  if (pngenc->output_map.size > G_MAXSIZE - length) {
    GST_ERROR_OBJECT (pngenc,
        "Memory buffer would overflow after the png write, aborting.");
    png_error (png_ptr, "Buffer would overflow, aborting the write.");

    /* never reached */
    /* libpng will longjmp and we will catch it later on */
    return;
  }

  if ((pngenc->output_mem_pos + length) > pngenc->output_map.size) {
    GST_INFO_OBJECT (pngenc, "Memory not enough, Allocating more.");
    ensure_memory_is_enough (pngenc, length);
  }

  memcpy (&pngenc->output_map.data[pngenc->output_mem_pos], data, length);
  pngenc->output_mem_pos += length;
}

static gboolean
gst_pngenc_flush (GstVideoEncoder * encoder)
{
  GstPngEnc *pngenc = GST_PNGENC (encoder);

  pngenc->frame_count = 0;

  return TRUE;
}

static gboolean
gst_pngenc_start (GstVideoEncoder * encoder)
{
  GstPngEnc *pngenc = GST_PNGENC (encoder);

  pngenc->frame_count = 0;

  return TRUE;
}

static GstFlowReturn
gst_pngenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstPngEnc *pngenc;
  gint row_index;
  png_byte **row_pointers;
  GstFlowReturn ret = GST_FLOW_OK;
  const GstVideoInfo *info;
  GstVideoFrame vframe;
  gsize memory_size;
  GstBuffer *outbuf;

  pngenc = GST_PNGENC (encoder);

  if (pngenc->snapshot && pngenc->frame_count > 0)
    return GST_FLOW_EOS;

  info = &pngenc->input_state->info;

  GST_DEBUG_OBJECT (pngenc, "BEGINNING");

  /* initialize png struct stuff */
  pngenc->png_struct_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);
  if (pngenc->png_struct_ptr == NULL)
    goto struct_init_fail;

  pngenc->png_info_ptr = png_create_info_struct (pngenc->png_struct_ptr);
  if (!pngenc->png_info_ptr)
    goto png_info_fail;

  /* non-0 return is from a longjmp inside of libpng */
  if (setjmp (png_jmpbuf (pngenc->png_struct_ptr)) != 0)
    goto longjmp_fail;

  png_set_filter (pngenc->png_struct_ptr, 0,
      PNG_FILTER_NONE | PNG_FILTER_VALUE_NONE);
  png_set_compression_level (pngenc->png_struct_ptr, pngenc->compression_level);

  png_set_IHDR (pngenc->png_struct_ptr,
      pngenc->png_info_ptr,
      GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info),
      pngenc->depth,
      pngenc->png_color_type,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_write_fn (pngenc->png_struct_ptr, pngenc,
      (png_rw_ptr) user_write_data, user_flush_data);

  if (!gst_video_frame_map (&vframe, &pngenc->input_state->info,
          frame->input_buffer, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (pngenc, STREAM, FORMAT, (NULL),
        ("Failed to map video frame, caps problem?"));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  row_pointers = g_new (png_byte *, GST_VIDEO_INFO_HEIGHT (info));

  for (row_index = 0; row_index < GST_VIDEO_INFO_HEIGHT (info); row_index++) {
    row_pointers[row_index] = GST_VIDEO_FRAME_COMP_DATA (&vframe, 0) +
        (row_index * GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0));
  }

  /* allocate the output buffer */
  pngenc->output_mem_pos = 0;
  pngenc->output_mem =
      gst_allocator_alloc (NULL, MAX (4096, GST_VIDEO_INFO_SIZE (info)), NULL);
  if (!pngenc->output_mem) {
    GST_ERROR_OBJECT (pngenc, "Failed to allocate memory");
    return GST_FLOW_ERROR;
  }

  gst_memory_map (pngenc->output_mem, &pngenc->output_map, GST_MAP_READWRITE);

  png_write_info (pngenc->png_struct_ptr, pngenc->png_info_ptr);
  png_write_image (pngenc->png_struct_ptr, row_pointers);
  png_write_end (pngenc->png_struct_ptr, NULL);

  g_free (row_pointers);
  gst_video_frame_unmap (&vframe);

  png_destroy_info_struct (pngenc->png_struct_ptr, &pngenc->png_info_ptr);
  png_destroy_write_struct (&pngenc->png_struct_ptr, (png_infopp) NULL);

  /* Set final size and store */
  gst_memory_unmap (pngenc->output_mem, &pngenc->output_map);
  /* Trim the buffer size */
  memory_size = pngenc->output_mem_pos;
  gst_memory_resize (pngenc->output_mem, 0, memory_size);
  pngenc->output_mem_pos = 0;

  outbuf = gst_buffer_new ();
  gst_buffer_append_memory (outbuf, pngenc->output_mem);
  pngenc->output_mem = NULL;
  frame->output_buffer = outbuf;

  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

  if ((ret = gst_video_encoder_finish_frame (encoder, frame)) != GST_FLOW_OK)
    goto done;

  ++pngenc->frame_count;

  if (pngenc->snapshot)
    ret = GST_FLOW_EOS;

done:
  GST_DEBUG_OBJECT (pngenc, "END, ret:%d", ret);

  return ret;

  /* ERRORS */
struct_init_fail:
  {
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    return GST_FLOW_ERROR;
  }

png_info_fail:
  {
    png_destroy_write_struct (&(pngenc->png_struct_ptr), (png_infopp) NULL);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, INIT, (NULL),
        ("Failed to initialize the png info structure"));
    return GST_FLOW_ERROR;
  }

longjmp_fail:
  {
    png_destroy_write_struct (&pngenc->png_struct_ptr, &pngenc->png_info_ptr);
    GST_ELEMENT_ERROR (pngenc, LIBRARY, FAILED, (NULL),
        ("returning from longjmp"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_pngenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
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
    case ARG_COMPRESSION_LEVEL:
      pngenc->compression_level = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
