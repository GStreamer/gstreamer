/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */
/**
 * SECTION:element-pngdec
 *
 * Decodes png images. If there is no framerate set on sink caps, it sends EOS
 * after the first picture.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpngdec.h"

#include <stdlib.h>
#include <string.h>
#include <gst/video/video.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (pngdec_debug);
#define GST_CAT_DEFAULT pngdec_debug

static gboolean gst_pngdec_libpng_init (GstPngDec * pngdec);
static gboolean gst_pngdec_reset (GstVideoDecoder * decoder, gboolean hard);

static GstFlowReturn gst_pngdec_caps_create_and_set (GstPngDec * pngdec);

static gboolean gst_pngdec_start (GstVideoDecoder * decoder);
static gboolean gst_pngdec_stop (GstVideoDecoder * decoder);
static gboolean gst_pngdec_set_format (GstVideoDecoder * Decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_pngdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

GST_BOILERPLATE (GstPngDec, gst_pngdec, GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER);

static GstStaticPadTemplate gst_pngdec_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_ARGB_64 ";"
        GST_VIDEO_CAPS_GRAY8 ";" GST_VIDEO_CAPS_GRAY16 ("BIG_ENDIAN"))
    );

static GstStaticPadTemplate gst_pngdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png")
    );

static void
gst_pngdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_pngdec_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_pngdec_sink_pad_template);
  gst_element_class_set_details_simple (element_class, "PNG image decoder",
      "Codec/Decoder/Image",
      "Decode a png video frame to a raw image",
      "Wim Taymans <wim@fluendo.com>");
}

static void
gst_pngdec_class_init (GstPngDecClass * klass)
{
  GstVideoDecoderClass *vdec_class = (GstVideoDecoderClass *) klass;

  vdec_class->start = gst_pngdec_start;
  vdec_class->reset = gst_pngdec_reset;
  vdec_class->set_format = gst_pngdec_set_format;
  vdec_class->handle_frame = gst_pngdec_handle_frame;

  GST_DEBUG_CATEGORY_INIT (pngdec_debug, "pngdec", 0, "PNG image decoder");
}

static void
gst_pngdec_init (GstPngDec * pngdec, GstPngDecClass * klass)
{
  pngdec->buffer_out = NULL;
  pngdec->png = NULL;
  pngdec->info = NULL;
  pngdec->endinfo = NULL;

  pngdec->color_type = -1;

  pngdec->image_ready = FALSE;
}

static void
user_error_fn (png_structp png_ptr, png_const_charp error_msg)
{
  GST_ERROR ("%s", error_msg);
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  GST_WARNING ("%s", warning_msg);
}

static void
user_info_callback (png_structp png_ptr, png_infop info)
{
  GstPngDec *pngdec = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  size_t buffer_size;
  guint height;

  GST_LOG ("info ready");

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));
  /* Generate the caps and configure */
  ret = gst_pngdec_caps_create_and_set (pngdec);
  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  height = GST_VIDEO_INFO_HEIGHT (&pngdec->output_state->info);

  /* Allocate output buffer */
  pngdec->rowbytes = png_get_rowbytes (pngdec->png, pngdec->info);
  GST_DEBUG ("png told us each row takes %d bytes", pngdec->rowbytes);
  if (pngdec->rowbytes > (G_MAXUINT32 - 3)
      || height > G_MAXUINT32 / pngdec->rowbytes) {
    ret = GST_FLOW_ERROR;
    goto beach;
  }
  pngdec->rowbytes = GST_ROUND_UP_4 (pngdec->rowbytes);
  buffer_size = height * pngdec->rowbytes;

  GST_DEBUG ("Allocating a buffer of %d bytes", buffer_size);

  g_assert (pngdec->buffer_out == NULL);
  pngdec->buffer_out = pngdec->current_frame->output_buffer =
      gst_buffer_new_and_alloc (buffer_size);

beach:
  pngdec->ret = ret;
}

static gboolean
gst_pngdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstPngDec *pngdec = (GstPngDec *) decoder;

  if (pngdec->input_state)
    gst_video_codec_state_unref (pngdec->input_state);
  pngdec->input_state = gst_video_codec_state_ref (state);

  /* We'll set format later on */

  return TRUE;
}

static void
user_endrow_callback (png_structp png_ptr, png_bytep new_row,
    png_uint_32 row_num, int pass)
{
  GstPngDec *pngdec = NULL;

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));

  /* FIXME: implement interlaced pictures */

  /* If buffer_out doesn't exist, it means buffer_alloc failed, which 
   * will already have set the return code */
  if (GST_IS_BUFFER (pngdec->buffer_out)) {
    size_t offset = row_num * pngdec->rowbytes;

    GST_LOG ("got row %u, copying in buffer %p at offset %" G_GSIZE_FORMAT,
        (guint) row_num, pngdec->buffer_out, offset);
    memcpy (GST_BUFFER_DATA (pngdec->buffer_out) + offset, new_row,
        pngdec->rowbytes);
    pngdec->ret = GST_FLOW_OK;
  }
}

static void
user_end_callback (png_structp png_ptr, png_infop info)
{
  GstPngDec *pngdec = NULL;

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));

  GST_LOG_OBJECT (pngdec, "and we are done reading this image");

  if (!pngdec->buffer_out)
    return;

  pngdec->ret =
      gst_video_decoder_finish_frame (GST_VIDEO_DECODER (pngdec),
      pngdec->current_frame);

  pngdec->buffer_out = NULL;
  pngdec->image_ready = TRUE;
}


static GstFlowReturn
gst_pngdec_caps_create_and_set (GstPngDec * pngdec)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint bpc = 0, color_type;
  png_uint_32 width, height;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;

  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), GST_FLOW_ERROR);

  /* Get bits per channel */
  bpc = png_get_bit_depth (pngdec->png, pngdec->info);

  /* Get Color type */
  color_type = png_get_color_type (pngdec->png, pngdec->info);

  /* Add alpha channel if 16-bit depth, but not for GRAY images */
  if ((bpc > 8) && (color_type != PNG_COLOR_TYPE_GRAY)) {
    png_set_add_alpha (pngdec->png, 0xffff, PNG_FILLER_BEFORE);
    png_set_swap (pngdec->png);
  }
#if 0
  /* We used to have this HACK to reverse the outgoing bytes, but the problem
   * that originally required the hack seems to have been in ffmpegcolorspace's
   * RGBA descriptions. It doesn't seem needed now that's fixed, but might
   * still be needed on big-endian systems, I'm not sure. J.S. 6/7/2007 */
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    png_set_bgr (pngdec->png);
#endif

  /* Gray scale with alpha channel converted to RGB */
  if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    GST_LOG_OBJECT (pngdec,
        "converting grayscale png with alpha channel to RGB");
    png_set_gray_to_rgb (pngdec->png);
  }

  /* Gray scale converted to upscaled to 8 bits */
  if ((color_type == PNG_COLOR_TYPE_GRAY_ALPHA) ||
      (color_type == PNG_COLOR_TYPE_GRAY)) {
    if (bpc < 8) {              /* Convert to 8 bits */
      GST_LOG_OBJECT (pngdec, "converting grayscale image to 8 bits");
#if PNG_LIBPNG_VER < 10400
      png_set_gray_1_2_4_to_8 (pngdec->png);
#else
      png_set_expand_gray_1_2_4_to_8 (pngdec->png);
#endif
    }
  }

  /* Palette converted to RGB */
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    GST_LOG_OBJECT (pngdec, "converting palette png to RGB");
    png_set_palette_to_rgb (pngdec->png);
  }

  /* Update the info structure */
  png_read_update_info (pngdec->png, pngdec->info);

  /* Get IHDR header again after transformation settings */
  png_get_IHDR (pngdec->png, pngdec->info, &width, &height,
      &bpc, &pngdec->color_type, NULL, NULL, NULL);

  GST_LOG_OBJECT (pngdec, "this is a %dx%d PNG image", width, height);

  switch (pngdec->color_type) {
    case PNG_COLOR_TYPE_RGB:
      GST_LOG_OBJECT (pngdec, "we have no alpha channel, depth is 24 bits");
      if (bpc == 8)
        format = GST_VIDEO_FORMAT_RGB;
      break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
      GST_LOG_OBJECT (pngdec,
          "we have an alpha channel, depth is 32 or 64 bits");
      if (bpc == 8)
        format = GST_VIDEO_FORMAT_RGBA;
      else if (bpc == 16)
        format = GST_VIDEO_FORMAT_ARGB64;
      break;
    case PNG_COLOR_TYPE_GRAY:
      GST_LOG_OBJECT (pngdec,
          "We have an gray image, depth is 8 or 16 (be) bits");
      if (bpc == 8)
        format = GST_VIDEO_FORMAT_GRAY8;
      else if (bpc == 16)
        format = GST_VIDEO_FORMAT_GRAY16_BE;
      break;
    default:
      break;
  }

  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ELEMENT_ERROR (pngdec, STREAM, NOT_IMPLEMENTED, (NULL),
        ("pngdec does not support this color type"));
    ret = GST_FLOW_NOT_SUPPORTED;
    goto beach;
  }

  /* Check if output state changed */
  if (pngdec->output_state) {
    GstVideoInfo *info = &pngdec->output_state->info;

    if (width == GST_VIDEO_INFO_WIDTH (info) &&
        height == GST_VIDEO_INFO_HEIGHT (info) &&
        GST_VIDEO_INFO_FORMAT (info) == format) {
      goto beach;
    }
    gst_video_codec_state_unref (pngdec->output_state);
  }

  pngdec->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (pngdec), format,
      width, height, pngdec->input_state);
  GST_DEBUG ("Final %d %d", GST_VIDEO_INFO_WIDTH (&pngdec->output_state->info),
      GST_VIDEO_INFO_HEIGHT (&pngdec->output_state->info));

beach:
  return ret;
}

static GstFlowReturn
gst_pngdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstPngDec *pngdec = (GstPngDec *) decoder;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (pngdec, "Got buffer, size=%u",
      GST_BUFFER_SIZE (frame->input_buffer));

  /* Let libpng come back here on error */
  if (setjmp (png_jmpbuf (pngdec->png))) {
    GST_WARNING_OBJECT (pngdec, "error during decoding");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  pngdec->current_frame = frame;

  /* Progressive loading of the PNG image */
  png_process_data (pngdec->png, pngdec->info,
      GST_BUFFER_DATA (frame->input_buffer),
      GST_BUFFER_SIZE (frame->input_buffer));

  if (pngdec->image_ready) {
    if (1) {
      /* Reset ourselves for the next frame */
      gst_pngdec_reset (decoder, TRUE);
      GST_LOG_OBJECT (pngdec, "setting up callbacks for next frame");
      png_set_progressive_read_fn (pngdec->png, pngdec,
          user_info_callback, user_endrow_callback, user_end_callback);
    } else {
      GST_LOG_OBJECT (pngdec, "sending EOS");
      pngdec->ret = GST_FLOW_UNEXPECTED;
    }
    pngdec->image_ready = FALSE;
  }

  ret = pngdec->ret;
beach:

  return ret;
}

/* Clean up the libpng structures */
static gboolean
gst_pngdec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  gst_pngdec_stop (decoder);
  gst_pngdec_start (decoder);

  return TRUE;
}

static gboolean
gst_pngdec_libpng_init (GstPngDec * pngdec)
{
  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), FALSE);

  GST_LOG ("init libpng structures");

  /* initialize png struct stuff */
  pngdec->png = png_create_read_struct (PNG_LIBPNG_VER_STRING,
      (png_voidp) NULL, user_error_fn, user_warning_fn);

  if (pngdec->png == NULL)
    goto init_failed;

  pngdec->info = png_create_info_struct (pngdec->png);
  if (pngdec->info == NULL)
    goto info_failed;

  pngdec->endinfo = png_create_info_struct (pngdec->png);
  if (pngdec->endinfo == NULL)
    goto endinfo_failed;

  png_set_progressive_read_fn (pngdec->png, pngdec,
      user_info_callback, user_endrow_callback, user_end_callback);

  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize png structure"));
    return FALSE;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize info structure"));
    return FALSE;
  }
endinfo_failed:
  {
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize endinfo structure"));
    return FALSE;
  }
}

static gboolean
gst_pngdec_start (GstVideoDecoder * decoder)
{
  GstPngDec *pngdec = (GstPngDec *) decoder;

  gst_pngdec_libpng_init (pngdec);

  return TRUE;
}

static gboolean
gst_pngdec_stop (GstVideoDecoder * decoder)
{
  GstPngDec *pngdec = (GstPngDec *) decoder;
  png_infopp info = NULL, endinfo = NULL;

  GST_LOG ("cleaning up libpng structures");

  if (pngdec->info) {
    info = &pngdec->info;
  }

  if (pngdec->endinfo) {
    endinfo = &pngdec->endinfo;
  }

  if (pngdec->png) {
    png_destroy_read_struct (&(pngdec->png), info, endinfo);
    pngdec->png = NULL;
    pngdec->info = NULL;
    pngdec->endinfo = NULL;
  }

  pngdec->color_type = -1;
  pngdec->rowbytes = 0;
  pngdec->buffer_out = NULL;

  return TRUE;
}
