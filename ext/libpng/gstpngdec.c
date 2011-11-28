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

static void gst_pngdec_base_init (gpointer g_class);
static void gst_pngdec_class_init (GstPngDecClass * klass);
static void gst_pngdec_init (GstPngDec * pngdec);

static gboolean gst_pngdec_libpng_init (GstPngDec * pngdec);
static gboolean gst_pngdec_libpng_clear (GstPngDec * pngdec);

static GstStateChangeReturn gst_pngdec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_pngdec_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_pngdec_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_pngdec_sink_activate (GstPad * sinkpad);

static GstFlowReturn gst_pngdec_caps_create_and_set (GstPngDec * pngdec);

static void gst_pngdec_task (GstPad * pad);
static GstFlowReturn gst_pngdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_pngdec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_pngdec_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstElementClass *parent_class = NULL;

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB ";"
        GST_VIDEO_CAPS_ARGB_64)
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
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_pngdec_change_state;

  GST_DEBUG_CATEGORY_INIT (pngdec_debug, "pngdec", 0, "PNG image decoder");
}

static void
gst_pngdec_init (GstPngDec * pngdec)
{
  pngdec->sinkpad =
      gst_pad_new_from_static_template (&gst_pngdec_sink_pad_template, "sink");
  gst_pad_set_activate_function (pngdec->sinkpad, gst_pngdec_sink_activate);
  gst_pad_set_activatepush_function (pngdec->sinkpad,
      gst_pngdec_sink_activate_push);
  gst_pad_set_activatepull_function (pngdec->sinkpad,
      gst_pngdec_sink_activate_pull);
  gst_pad_set_chain_function (pngdec->sinkpad, gst_pngdec_chain);
  gst_pad_set_event_function (pngdec->sinkpad, gst_pngdec_sink_event);
  gst_pad_set_setcaps_function (pngdec->sinkpad, gst_pngdec_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->sinkpad);

  pngdec->srcpad =
      gst_pad_new_from_static_template (&gst_pngdec_src_pad_template, "src");
  gst_pad_use_fixed_caps (pngdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (pngdec), pngdec->srcpad);

  pngdec->buffer_out = NULL;
  pngdec->png = NULL;
  pngdec->info = NULL;
  pngdec->endinfo = NULL;
  pngdec->setup = FALSE;

  pngdec->color_type = -1;
  pngdec->width = -1;
  pngdec->height = -1;
  pngdec->bpp = -1;
  pngdec->fps_n = 0;
  pngdec->fps_d = 1;

  pngdec->in_timestamp = GST_CLOCK_TIME_NONE;
  pngdec->in_duration = GST_CLOCK_TIME_NONE;

  gst_segment_init (&pngdec->segment, GST_FORMAT_UNDEFINED);

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
  GstBuffer *buffer = NULL;

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));

  GST_LOG ("info ready");

  /* Generate the caps and configure */
  ret = gst_pngdec_caps_create_and_set (pngdec);
  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  /* Allocate output buffer */
  pngdec->rowbytes = png_get_rowbytes (pngdec->png, pngdec->info);
  if (pngdec->rowbytes > (G_MAXUINT32 - 3)
      || pngdec->height > G_MAXUINT32 / pngdec->rowbytes) {
    ret = GST_FLOW_ERROR;
    goto beach;
  }
  pngdec->rowbytes = GST_ROUND_UP_4 (pngdec->rowbytes);
  buffer_size = pngdec->height * pngdec->rowbytes;

  ret =
      gst_pad_alloc_buffer_and_set_caps (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      buffer_size, GST_PAD_CAPS (pngdec->srcpad), &buffer);
  if (ret != GST_FLOW_OK) {
    goto beach;
  }

  pngdec->buffer_out = buffer;

beach:
  pngdec->ret = ret;
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

static gboolean
buffer_clip (GstPngDec * dec, GstBuffer * buffer)
{
  gboolean res = TRUE;
  gint64 cstart, cstop;


  if ((!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) ||
      (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) ||
      (dec->segment.format != GST_FORMAT_TIME))
    goto beach;

  cstart = GST_BUFFER_TIMESTAMP (buffer);
  cstop = GST_BUFFER_DURATION (buffer);

  if ((res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
              cstart, cstart + cstop, &cstart, &cstop))) {
    GST_BUFFER_TIMESTAMP (buffer) = cstart;
    GST_BUFFER_DURATION (buffer) = cstop - cstart;
  }

beach:
  return res;
}

static void
user_end_callback (png_structp png_ptr, png_infop info)
{
  GstPngDec *pngdec = NULL;

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));

  GST_LOG_OBJECT (pngdec, "and we are done reading this image");

  if (!pngdec->buffer_out)
    return;

  if (GST_CLOCK_TIME_IS_VALID (pngdec->in_timestamp))
    GST_BUFFER_TIMESTAMP (pngdec->buffer_out) = pngdec->in_timestamp;
  if (GST_CLOCK_TIME_IS_VALID (pngdec->in_duration))
    GST_BUFFER_DURATION (pngdec->buffer_out) = pngdec->in_duration;

  /* buffer clipping */
  if (buffer_clip (pngdec, pngdec->buffer_out)) {
    /* Push our buffer and then EOS if needed */
    GST_LOG_OBJECT (pngdec, "pushing buffer with ts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (pngdec->buffer_out)));

    pngdec->ret = gst_pad_push (pngdec->srcpad, pngdec->buffer_out);
  } else {
    GST_LOG_OBJECT (pngdec, "dropped decoded buffer");
    gst_buffer_unref (pngdec->buffer_out);
  }
  pngdec->buffer_out = NULL;
  pngdec->image_ready = TRUE;
}

static void
user_read_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
  GstPngDec *pngdec;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guint size;

  pngdec = GST_PNGDEC (png_get_io_ptr (png_ptr));

  GST_LOG ("reading %" G_GSIZE_FORMAT " bytes of data at offset %d", length,
      pngdec->offset);

  ret = gst_pad_pull_range (pngdec->sinkpad, pngdec->offset, length, &buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  size = GST_BUFFER_SIZE (buffer);

  if (size != length)
    goto short_buffer;

  memcpy (data, GST_BUFFER_DATA (buffer), size);

  gst_buffer_unref (buffer);

  pngdec->offset += length;

  return;

  /* ERRORS */
pause:
  {
    GST_INFO_OBJECT (pngdec, "pausing task, reason %s",
        gst_flow_get_name (ret));
    gst_pad_pause_task (pngdec->sinkpad);
    if (ret == GST_FLOW_UNEXPECTED) {
      gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    } else if (ret < GST_FLOW_UNEXPECTED || ret == GST_FLOW_NOT_LINKED) {
      GST_ELEMENT_ERROR (pngdec, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("stream stopped, reason %s", gst_flow_get_name (ret)));
      gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    }
    png_error (png_ptr, "Internal data stream error.");
    return;
  }
short_buffer:
  {
    gst_buffer_unref (buffer);
    GST_ELEMENT_ERROR (pngdec, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("Read %u, needed %" G_GSIZE_FORMAT "bytes", size, length));
    ret = GST_FLOW_ERROR;
    goto pause;
  }
}

static GstFlowReturn
gst_pngdec_caps_create_and_set (GstPngDec * pngdec)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps = NULL, *res = NULL;
  GstPadTemplate *templ = NULL;
  gint bpc = 0, color_type;
  png_uint_32 width, height;

  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), GST_FLOW_ERROR);

  /* Get bits per channel */
  bpc = png_get_bit_depth (pngdec->png, pngdec->info);
  if (bpc > 8) {
    /* Add alpha channel if 16-bit depth */
    png_set_add_alpha (pngdec->png, 0xffff, PNG_FILLER_BEFORE);
    png_set_swap (pngdec->png);
  }

  /* Get Color type */
  color_type = png_get_color_type (pngdec->png, pngdec->info);

#if 0
  /* We used to have this HACK to reverse the outgoing bytes, but the problem
   * that originally required the hack seems to have been in ffmpegcolorspace's
   * RGBA descriptions. It doesn't seem needed now that's fixed, but might
   * still be needed on big-endian systems, I'm not sure. J.S. 6/7/2007 */
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    png_set_bgr (pngdec->png);
#endif

  /* Gray scale converted to RGB and upscaled to 8 bits */
  if ((color_type == PNG_COLOR_TYPE_GRAY_ALPHA) ||
      (color_type == PNG_COLOR_TYPE_GRAY)) {
    GST_LOG_OBJECT (pngdec, "converting grayscale png to RGB");
    png_set_gray_to_rgb (pngdec->png);
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

  pngdec->width = width;
  pngdec->height = height;

  GST_LOG_OBJECT (pngdec, "this is a %dx%d PNG image", pngdec->width,
      pngdec->height);

  switch (pngdec->color_type) {
    case PNG_COLOR_TYPE_RGB:
      GST_LOG_OBJECT (pngdec, "we have no alpha channel, depth is 24 bits");
      pngdec->bpp = 3 * bpc;
      break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
      GST_LOG_OBJECT (pngdec, "we have an alpha channel, depth is 32 bits");
      pngdec->bpp = 4 * bpc;
      break;
    default:
      GST_ELEMENT_ERROR (pngdec, STREAM, NOT_IMPLEMENTED, (NULL),
          ("pngdec does not support this color type"));
      ret = GST_FLOW_NOT_SUPPORTED;
      goto beach;
  }

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", G_TYPE_INT, pngdec->width,
      "height", G_TYPE_INT, pngdec->height,
      "bpp", G_TYPE_INT, pngdec->bpp,
      "framerate", GST_TYPE_FRACTION, pngdec->fps_n, pngdec->fps_d, NULL);

  templ = gst_static_pad_template_get (&gst_pngdec_src_pad_template);

  res = gst_caps_intersect (caps, gst_pad_template_get_caps (templ));

  gst_caps_unref (caps);
  gst_object_unref (templ);

  if (!gst_pad_set_caps (pngdec->srcpad, res))
    ret = GST_FLOW_NOT_NEGOTIATED;

  GST_DEBUG_OBJECT (pngdec, "our caps %" GST_PTR_FORMAT, res);

  gst_caps_unref (res);

  /* Push a newsegment event */
  if (pngdec->need_newsegment) {
    gst_pad_push_event (pngdec->srcpad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    pngdec->need_newsegment = FALSE;
  }

beach:
  return ret;
}

static void
gst_pngdec_task (GstPad * pad)
{
  GstPngDec *pngdec;
  GstBuffer *buffer = NULL;
  size_t buffer_size = 0;
  gint i = 0;
  png_bytep *rows, inp;
  png_uint_32 rowbytes;
  GstFlowReturn ret = GST_FLOW_OK;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT (pngdec, "read frame");

  /* Let libpng come back here on error */
  if (setjmp (png_jmpbuf (pngdec->png))) {
    ret = GST_FLOW_ERROR;
    goto pause;
  }

  /* Set reading callback */
  png_set_read_fn (pngdec->png, pngdec, user_read_data);

  /* Read info */
  png_read_info (pngdec->png, pngdec->info);

  /* Generate the caps and configure */
  ret = gst_pngdec_caps_create_and_set (pngdec);
  if (ret != GST_FLOW_OK) {
    goto pause;
  }

  /* Allocate output buffer */
  rowbytes = png_get_rowbytes (pngdec->png, pngdec->info);
  if (rowbytes > (G_MAXUINT32 - 3) || pngdec->height > G_MAXUINT32 / rowbytes) {
    ret = GST_FLOW_ERROR;
    goto pause;
  }
  rowbytes = GST_ROUND_UP_4 (rowbytes);
  buffer_size = pngdec->height * rowbytes;
  ret =
      gst_pad_alloc_buffer_and_set_caps (pngdec->srcpad, GST_BUFFER_OFFSET_NONE,
      buffer_size, GST_PAD_CAPS (pngdec->srcpad), &buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  rows = (png_bytep *) g_malloc (sizeof (png_bytep) * pngdec->height);

  inp = GST_BUFFER_DATA (buffer);

  for (i = 0; i < pngdec->height; i++) {
    rows[i] = inp;
    inp += rowbytes;
  }

  /* Read the actual picture */
  png_read_image (pngdec->png, rows);
  g_free (rows);

  /* Push the raw RGB frame */
  ret = gst_pad_push (pngdec->srcpad, buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  /* And we are done */
  gst_pad_pause_task (pngdec->sinkpad);
  gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
  return;

pause:
  {
    GST_INFO_OBJECT (pngdec, "pausing task, reason %s",
        gst_flow_get_name (ret));
    gst_pad_pause_task (pngdec->sinkpad);
    if (ret == GST_FLOW_UNEXPECTED) {
      gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (pngdec, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("stream stopped, reason %s", gst_flow_get_name (ret)));
      gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    }
  }
}

static GstFlowReturn
gst_pngdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPngDec *pngdec;
  GstFlowReturn ret = GST_FLOW_OK;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pngdec, "Got buffer, size=%u", GST_BUFFER_SIZE (buffer));

  if (G_UNLIKELY (!pngdec->setup))
    goto not_configured;

  /* Something is going wrong in our callbacks */
  ret = pngdec->ret;
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (pngdec, "we have a pending return code of %d", ret);
    goto beach;
  }

  /* Let libpng come back here on error */
  if (setjmp (png_jmpbuf (pngdec->png))) {
    GST_WARNING_OBJECT (pngdec, "error during decoding");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  pngdec->in_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  pngdec->in_duration = GST_BUFFER_DURATION (buffer);

  /* Progressive loading of the PNG image */
  png_process_data (pngdec->png, pngdec->info, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));

  if (pngdec->image_ready) {
    if (pngdec->framed) {
      /* Reset ourselves for the next frame */
      gst_pngdec_libpng_clear (pngdec);
      gst_pngdec_libpng_init (pngdec);
      GST_LOG_OBJECT (pngdec, "setting up callbacks for next frame");
      png_set_progressive_read_fn (pngdec->png, pngdec,
          user_info_callback, user_endrow_callback, user_end_callback);
    } else {
      GST_LOG_OBJECT (pngdec, "sending EOS");
      pngdec->ret = gst_pad_push_event (pngdec->srcpad, gst_event_new_eos ());
    }
    pngdec->image_ready = FALSE;
  }

  /* grab new return code */
  ret = pngdec->ret;

  /* And release the buffer */
  gst_buffer_unref (buffer);

beach:
  gst_object_unref (pngdec);

  return ret;

  /* ERRORS */
not_configured:
  {
    GST_LOG_OBJECT (pngdec, "we are not configured yet");
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }
}

static gboolean
gst_pngdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *s;
  GstPngDec *pngdec;
  gint num, denom;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_fraction (s, "framerate", &num, &denom)) {
    GST_DEBUG_OBJECT (pngdec, "framed input");
    pngdec->framed = TRUE;
    pngdec->fps_n = num;
    pngdec->fps_d = denom;
  } else {
    GST_DEBUG_OBJECT (pngdec, "single picture input");
    pngdec->framed = FALSE;
    pngdec->fps_n = 0;
    pngdec->fps_d = 1;
  }

  gst_object_unref (pngdec);
  return TRUE;
}

static gboolean
gst_pngdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstPngDec *pngdec;
  gboolean res;

  pngdec = GST_PNGDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      gdouble rate, arate;
      gboolean update;
      gint64 start, stop, position;
      GstFormat fmt;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
          &start, &stop, &position);

      gst_segment_set_newsegment_full (&pngdec->segment, update, rate, arate,
          fmt, start, stop, position);

      GST_LOG_OBJECT (pngdec, "NEWSEGMENT (%s)", gst_format_get_name (fmt));

      if (fmt == GST_FORMAT_TIME) {
        pngdec->need_newsegment = FALSE;
        res = gst_pad_push_event (pngdec->srcpad, event);
      } else {
        gst_event_unref (event);
        res = TRUE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      gst_pngdec_libpng_clear (pngdec);
      gst_pngdec_libpng_init (pngdec);
      png_set_progressive_read_fn (pngdec->png, pngdec,
          user_info_callback, user_endrow_callback, user_end_callback);
      pngdec->ret = GST_FLOW_OK;
      gst_segment_init (&pngdec->segment, GST_FORMAT_UNDEFINED);
      res = gst_pad_push_event (pngdec->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
    {
      GST_LOG_OBJECT (pngdec, "EOS");
      gst_pngdec_libpng_clear (pngdec);
      pngdec->ret = GST_FLOW_UNEXPECTED;
      res = gst_pad_push_event (pngdec->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (pngdec->srcpad, event);
      break;
  }

  gst_object_unref (pngdec);
  return res;
}


/* Clean up the libpng structures */
static gboolean
gst_pngdec_libpng_clear (GstPngDec * pngdec)
{
  png_infopp info = NULL, endinfo = NULL;

  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), FALSE);

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

  pngdec->bpp = pngdec->color_type = pngdec->height = pngdec->width = -1;
  pngdec->offset = 0;
  pngdec->rowbytes = 0;
  pngdec->buffer_out = NULL;

  pngdec->setup = FALSE;

  pngdec->in_timestamp = GST_CLOCK_TIME_NONE;
  pngdec->in_duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_pngdec_libpng_init (GstPngDec * pngdec)
{
  g_return_val_if_fail (GST_IS_PNGDEC (pngdec), FALSE);

  if (pngdec->setup)
    return TRUE;

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

  pngdec->setup = TRUE;

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
    gst_pngdec_libpng_clear (pngdec);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize info structure"));
    return FALSE;
  }
endinfo_failed:
  {
    gst_pngdec_libpng_clear (pngdec);
    GST_ELEMENT_ERROR (pngdec, LIBRARY, INIT, (NULL),
        ("Failed to initialize endinfo structure"));
    return FALSE;
  }
}

static GstStateChangeReturn
gst_pngdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPngDec *pngdec;

  pngdec = GST_PNGDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_pngdec_libpng_init (pngdec);
      pngdec->need_newsegment = TRUE;
      pngdec->framed = FALSE;
      pngdec->ret = GST_FLOW_OK;
      gst_segment_init (&pngdec->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_pngdec_libpng_clear (pngdec);
      break;
    default:
      break;
  }

  return ret;
}

/* this function gets called when we activate ourselves in push mode. */
static gboolean
gst_pngdec_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstPngDec *pngdec;

  pngdec = GST_PNGDEC (GST_OBJECT_PARENT (sinkpad));

  pngdec->ret = GST_FLOW_OK;

  if (active) {
    /* Let libpng come back here on error */
    if (setjmp (png_jmpbuf (pngdec->png)))
      goto setup_failed;

    GST_LOG ("setting up progressive loading callbacks");
    png_set_progressive_read_fn (pngdec->png, pngdec,
        user_info_callback, user_endrow_callback, user_end_callback);
  }
  return TRUE;

setup_failed:
  {
    GST_LOG ("failed setting up libpng jmpbuf");
    gst_pngdec_libpng_clear (pngdec);
    return FALSE;
  }
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_pngdec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_pngdec_task,
        sinkpad);
  } else {
    return gst_pad_stop_task (sinkpad);
  }
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_pngdec_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}
