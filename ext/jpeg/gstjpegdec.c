/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2009> Tim-Philipp Müller <tim centricular net>
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

/**
 * SECTION:element-jpegdec
 *
 * Decodes jpeg images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v v4l2src ! jpegdec ! ffmpegcolorspace ! xvimagesink
 * ]| The above pipeline reads a motion JPEG stream from a v4l2 camera
 * and renders it to the screen.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstjpegdec.h"
#include "gstjpeg.h"
#include <gst/video/video.h>
#include "gst/gst-i18n-plugin.h"
#include <jerror.h>

#define MIN_WIDTH  1
#define MAX_WIDTH  65535
#define MIN_HEIGHT 1
#define MAX_HEIGHT 65535

#define CINFO_GET_JPEGDEC(cinfo_ptr) \
        (((struct GstJpegDecSourceMgr*)((cinfo_ptr)->src))->dec)

#define JPEG_DEFAULT_IDCT_METHOD	JDCT_FASTEST
#define JPEG_DEFAULT_MAX_ERRORS 	0

enum
{
  PROP_0,
  PROP_IDCT_METHOD,
  PROP_MAX_ERRORS
};

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_jpeg_dec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420") "; "
        GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; "
        GST_VIDEO_CAPS_RGBx "; " GST_VIDEO_CAPS_xRGB "; "
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_xBGR "; "
        GST_VIDEO_CAPS_GRAY8)
    );
/* *INDENT-ON* */

/* FIXME: sof-marker is for IJG libjpeg 8, should be different for 6.2 */
static GstStaticPadTemplate gst_jpeg_dec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ " G_STRINGIFY (MIN_WIDTH) ", " G_STRINGIFY (MAX_WIDTH)
        " ], " "height = (int) [ " G_STRINGIFY (MIN_HEIGHT) ", "
        G_STRINGIFY (MAX_HEIGHT) " ], framerate = (fraction) [ 0/1, MAX ], "
        "sof-marker = (int) { 0, 1, 2, 5, 6, 7, 9, 10, 13, 14 }")
    );

GST_DEBUG_CATEGORY_STATIC (jpeg_dec_debug);
#define GST_CAT_DEFAULT jpeg_dec_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/* These macros are adapted from videotestsrc.c 
 *  and/or gst-plugins/gst/games/gstvideoimage.c */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static GstElementClass *parent_class;   /* NULL */

static void gst_jpeg_dec_base_init (gpointer g_class);
static void gst_jpeg_dec_class_init (GstJpegDecClass * klass);
static void gst_jpeg_dec_init (GstJpegDec * jpegdec);

static void gst_jpeg_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jpeg_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_jpeg_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_jpeg_dec_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_jpeg_dec_getcaps (GstPad * pad);
static gboolean gst_jpeg_dec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_jpeg_dec_src_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_jpeg_dec_change_state (GstElement * element,
    GstStateChange transition);
static void gst_jpeg_dec_update_qos (GstJpegDec * dec, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime ts);
static void gst_jpeg_dec_reset_qos (GstJpegDec * dec);
static void gst_jpeg_dec_read_qos (GstJpegDec * dec, gdouble * proportion,
    GstClockTime * time);

GType
gst_jpeg_dec_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo jpeg_dec_info = {
      sizeof (GstJpegDecClass),
      (GBaseInitFunc) gst_jpeg_dec_base_init,
      NULL,
      (GClassInitFunc) gst_jpeg_dec_class_init,
      NULL,
      NULL,
      sizeof (GstJpegDec),
      0,
      (GInstanceInitFunc) gst_jpeg_dec_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstJpegDec",
        &jpeg_dec_info, 0);
  }
  return type;
}

static void
gst_jpeg_dec_finalize (GObject * object)
{
  GstJpegDec *dec = GST_JPEG_DEC (object);

  jpeg_destroy_decompress (&dec->cinfo);

  g_object_unref (dec->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_jpeg_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_jpeg_dec_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_jpeg_dec_sink_pad_template);
  gst_element_class_set_details_simple (element_class, "JPEG image decoder",
      "Codec/Decoder/Image",
      "Decode images from JPEG format", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_jpeg_dec_class_init (GstJpegDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_jpeg_dec_finalize;
  gobject_class->set_property = gst_jpeg_dec_set_property;
  gobject_class->get_property = gst_jpeg_dec_get_property;

  g_object_class_install_property (gobject_class, PROP_IDCT_METHOD,
      g_param_spec_enum ("idct-method", "IDCT Method",
          "The IDCT algorithm to use", GST_TYPE_IDCT_METHOD,
          JPEG_DEFAULT_IDCT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstJpegDec:max-errors
   *
   * Error out after receiving N consecutive decoding errors
   * (-1 = never error out, 0 = automatic, 1 = fail on first error, etc.)
   *
   * Since: 0.10.27
   **/
  g_object_class_install_property (gobject_class, PROP_MAX_ERRORS,
      g_param_spec_int ("max-errors", "Maximum Consecutive Decoding Errors",
          "Error out after receiving N consecutive decoding errors "
          "(-1 = never fail, 0 = automatic, 1 = fail on first error)",
          -1, G_MAXINT, JPEG_DEFAULT_MAX_ERRORS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_change_state);

  GST_DEBUG_CATEGORY_INIT (jpeg_dec_debug, "jpegdec", 0, "JPEG decoder");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gst_jpeg_dec_clear_error (GstJpegDec * dec)
{
  g_free (dec->error_msg);
  dec->error_msg = NULL;
  dec->error_line = 0;
  dec->error_func = NULL;
}

static void
gst_jpeg_dec_set_error_va (GstJpegDec * dec, const gchar * func, gint line,
    const gchar * debug_msg_format, va_list args)
{
#ifndef GST_DISABLE_GST_DEBUG
  gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_WARNING, __FILE__, func,
      line, (GObject *) dec, debug_msg_format, args);
#endif

  g_free (dec->error_msg);
  if (debug_msg_format)
    dec->error_msg = g_strdup_vprintf (debug_msg_format, args);
  else
    dec->error_msg = NULL;

  dec->error_line = line;
  dec->error_func = func;
}

static void
gst_jpeg_dec_set_error (GstJpegDec * dec, const gchar * func, gint line,
    const gchar * debug_msg_format, ...)
{
  va_list va;

  va_start (va, debug_msg_format);
  gst_jpeg_dec_set_error_va (dec, func, line, debug_msg_format, va);
  va_end (va);
}

static GstFlowReturn
gst_jpeg_dec_post_error_or_warning (GstJpegDec * dec)
{
  GstFlowReturn ret;
  int max_errors;

  ++dec->error_count;
  max_errors = g_atomic_int_get (&dec->max_errors);

  if (max_errors < 0) {
    ret = GST_FLOW_OK;
  } else if (max_errors == 0) {
    /* FIXME: do something more clever in "automatic mode" */
    if (dec->packetized) {
      ret = (dec->error_count < 3) ? GST_FLOW_OK : GST_FLOW_ERROR;
    } else {
      ret = GST_FLOW_ERROR;
    }
  } else {
    ret = (dec->error_count < max_errors) ? GST_FLOW_OK : GST_FLOW_ERROR;
  }

  GST_INFO_OBJECT (dec, "decoding error %d/%d (%s)", dec->error_count,
      max_errors, (ret == GST_FLOW_OK) ? "ignoring error" : "erroring out");

  gst_element_message_full (GST_ELEMENT (dec),
      (ret == GST_FLOW_OK) ? GST_MESSAGE_WARNING : GST_MESSAGE_ERROR,
      GST_STREAM_ERROR, GST_STREAM_ERROR_DECODE,
      g_strdup (_("Failed to decode JPEG image")), dec->error_msg,
      __FILE__, dec->error_func, dec->error_line);

  dec->error_msg = NULL;
  gst_jpeg_dec_clear_error (dec);
  return ret;
}

static boolean
gst_jpeg_dec_fill_input_buffer (j_decompress_ptr cinfo)
{
  GstJpegDec *dec;
  guint av;

  dec = CINFO_GET_JPEGDEC (cinfo);
  g_return_val_if_fail (dec != NULL, FALSE);

  av = gst_adapter_available_fast (dec->adapter);
  GST_DEBUG_OBJECT (dec, "fill_input_buffer: fast av=%u, remaining=%u", av,
      dec->rem_img_len);

  if (av == 0) {
    GST_DEBUG_OBJECT (dec, "Out of data");
    return FALSE;
  }

  if (dec->rem_img_len < av)
    av = dec->rem_img_len;
  dec->rem_img_len -= av;

  g_free (dec->cur_buf);
  dec->cur_buf = gst_adapter_take (dec->adapter, av);

  cinfo->src->next_input_byte = dec->cur_buf;
  cinfo->src->bytes_in_buffer = av;

  return TRUE;
}

static void
gst_jpeg_dec_init_source (j_decompress_ptr cinfo)
{
  GST_LOG_OBJECT (CINFO_GET_JPEGDEC (cinfo), "init_source");
}


static void
gst_jpeg_dec_skip_input_data (j_decompress_ptr cinfo, glong num_bytes)
{
  GstJpegDec *dec = CINFO_GET_JPEGDEC (cinfo);

  GST_DEBUG_OBJECT (dec, "skip %ld bytes", num_bytes);

  if (num_bytes > 0 && cinfo->src->bytes_in_buffer >= num_bytes) {
    cinfo->src->next_input_byte += (size_t) num_bytes;
    cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
  } else if (num_bytes > 0) {
    gint available;

    num_bytes -= cinfo->src->bytes_in_buffer;
    cinfo->src->next_input_byte += (size_t) cinfo->src->bytes_in_buffer;
    cinfo->src->bytes_in_buffer = 0;

    available = gst_adapter_available (dec->adapter);
    if (available < num_bytes || available < dec->rem_img_len) {
      GST_WARNING_OBJECT (dec, "Less bytes to skip than available in the "
          "adapter or the remaining image length %ld < %d or %u",
          num_bytes, available, dec->rem_img_len);
    }
    num_bytes = MIN (MIN (num_bytes, available), dec->rem_img_len);
    gst_adapter_flush (dec->adapter, num_bytes);
    dec->rem_img_len -= num_bytes;
  }
}

static boolean
gst_jpeg_dec_resync_to_restart (j_decompress_ptr cinfo, gint desired)
{
  GST_LOG_OBJECT (CINFO_GET_JPEGDEC (cinfo), "resync_to_start");
  return TRUE;
}

static void
gst_jpeg_dec_term_source (j_decompress_ptr cinfo)
{
  GST_LOG_OBJECT (CINFO_GET_JPEGDEC (cinfo), "term_source");
  return;
}

METHODDEF (void)
    gst_jpeg_dec_my_output_message (j_common_ptr cinfo)
{
  return;                       /* do nothing */
}

METHODDEF (void)
    gst_jpeg_dec_my_emit_message (j_common_ptr cinfo, int msg_level)
{
  /* GST_LOG_OBJECT (CINFO_GET_JPEGDEC (&cinfo), "msg_level=%d", msg_level); */
  return;
}

METHODDEF (void)
    gst_jpeg_dec_my_error_exit (j_common_ptr cinfo)
{
  struct GstJpegDecErrorMgr *err_mgr = (struct GstJpegDecErrorMgr *) cinfo->err;

  (*cinfo->err->output_message) (cinfo);
  longjmp (err_mgr->setjmp_buffer, 1);
}

static void
gst_jpeg_dec_init (GstJpegDec * dec)
{
  GST_DEBUG ("initializing");

  /* create the sink and src pads */
  dec->sinkpad =
      gst_pad_new_from_static_template (&gst_jpeg_dec_sink_pad_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_setcaps));
  gst_pad_set_getcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_getcaps));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_sink_event));

  dec->srcpad =
      gst_pad_new_from_static_template (&gst_jpeg_dec_src_pad_template, "src");
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_dec_src_event));
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  /* setup jpeglib */
  memset (&dec->cinfo, 0, sizeof (dec->cinfo));
  memset (&dec->jerr, 0, sizeof (dec->jerr));
  dec->cinfo.err = jpeg_std_error (&dec->jerr.pub);
  dec->jerr.pub.output_message = gst_jpeg_dec_my_output_message;
  dec->jerr.pub.emit_message = gst_jpeg_dec_my_emit_message;
  dec->jerr.pub.error_exit = gst_jpeg_dec_my_error_exit;

  jpeg_create_decompress (&dec->cinfo);

  dec->cinfo.src = (struct jpeg_source_mgr *) &dec->jsrc;
  dec->cinfo.src->init_source = gst_jpeg_dec_init_source;
  dec->cinfo.src->fill_input_buffer = gst_jpeg_dec_fill_input_buffer;
  dec->cinfo.src->skip_input_data = gst_jpeg_dec_skip_input_data;
  dec->cinfo.src->resync_to_restart = gst_jpeg_dec_resync_to_restart;
  dec->cinfo.src->term_source = gst_jpeg_dec_term_source;
  dec->jsrc.dec = dec;

  /* init properties */
  dec->idct_method = JPEG_DEFAULT_IDCT_METHOD;
  dec->max_errors = JPEG_DEFAULT_MAX_ERRORS;

  dec->adapter = gst_adapter_new ();
}

static gboolean
gst_jpeg_dec_ensure_header (GstJpegDec * dec)
{
  gint av;
  gint offset;

  av = gst_adapter_available (dec->adapter);
  /* we expect at least 4 bytes, first of which start marker */
  offset = gst_adapter_masked_scan_uint32 (dec->adapter, 0xffffff00, 0xffd8ff00,
      0, av);
  if (G_UNLIKELY (offset < 0)) {
    GST_DEBUG_OBJECT (dec, "No JPEG header in current buffer");
    /* not found */
    if (av > 4)
      gst_adapter_flush (dec->adapter, av - 4);
    return FALSE;
  }

  if (offset > 0) {
    GST_LOG_OBJECT (dec, "Skipping %u bytes.", offset);
    gst_adapter_flush (dec->adapter, offset);
  }
  GST_DEBUG_OBJECT (dec, "Found JPEG header");

  return TRUE;
}

static inline gboolean
gst_jpeg_dec_parse_tag_has_entropy_segment (guint8 tag)
{
  if (tag == 0xda || (tag >= 0xd0 && tag <= 0xd7))
    return TRUE;
  return FALSE;
}

/* returns image length in bytes if parsed successfully,
 * otherwise 0 if more data needed,
 * if < 0 the absolute value needs to be flushed */
static gint
gst_jpeg_dec_parse_image_data (GstJpegDec * dec)
{
  guint size;
  gboolean resync;
  GstAdapter *adapter = dec->adapter;
  gint offset, noffset;

  size = gst_adapter_available (adapter);

  /* we expect at least 4 bytes, first of which start marker */
  if (gst_adapter_masked_scan_uint32 (adapter, 0xffff0000, 0xffd80000, 0, 4))
    return 0;

  GST_DEBUG ("Parsing jpeg image data (%u bytes)", size);

  GST_DEBUG ("Parse state: offset=%d, resync=%d, entropy len=%d",
      dec->parse_offset, dec->parse_resync, dec->parse_entropy_len);

  /* offset is 2 less than actual offset;
   * - adapter needs at least 4 bytes for scanning,
   * - start and end marker ensure at least that much
   */
  /* resume from state offset */
  offset = dec->parse_offset;

  while (1) {
    guint frame_len;
    guint32 value;

    noffset =
        gst_adapter_masked_scan_uint32_peek (adapter, 0x0000ff00, 0x0000ff00,
        offset, size - offset, &value);
    /* lost sync if 0xff marker not where expected */
    if ((resync = (noffset != offset))) {
      GST_DEBUG ("Lost sync at 0x%08x, resyncing", offset + 2);
    }
    /* may have marker, but could have been resyncng */
    resync = resync || dec->parse_resync;
    /* Skip over extra 0xff */
    while ((noffset >= 0) && ((value & 0xff) == 0xff)) {
      noffset++;
      noffset =
          gst_adapter_masked_scan_uint32_peek (adapter, 0x0000ff00, 0x0000ff00,
          noffset, size - noffset, &value);
    }
    /* enough bytes left for marker? (we need 0xNN after the 0xff) */
    if (noffset < 0) {
      GST_DEBUG ("at end of input and no EOI marker found, need more data");
      goto need_more_data;
    }

    /* now lock on the marker we found */
    offset = noffset;
    value = value & 0xff;
    if (value == 0xd9) {
      GST_DEBUG ("0x%08x: EOI marker", offset + 2);
      /* clear parse state */
      dec->parse_resync = FALSE;
      dec->parse_offset = 0;
      return (offset + 4);
    } else if (value == 0xd8) {
      /* Skip this frame if we found another SOI marker */
      GST_DEBUG ("0x%08x: SOI marker before EOI, skipping", offset + 2);
      dec->parse_resync = FALSE;
      dec->parse_offset = 0;
      return -(offset + 2);
    }


    if (value >= 0xd0 && value <= 0xd7)
      frame_len = 0;
    else {
      /* peek tag and subsequent length */
      if (offset + 2 + 4 > size)
        goto need_more_data;
      else
        gst_adapter_masked_scan_uint32_peek (adapter, 0x0, 0x0, offset + 2, 4,
            &frame_len);
      frame_len = frame_len & 0xffff;
    }
    GST_DEBUG ("0x%08x: tag %02x, frame_len=%u", offset + 2, value, frame_len);
    /* the frame length includes the 2 bytes for the length; here we want at
     * least 2 more bytes at the end for an end marker */
    if (offset + 2 + 2 + frame_len + 2 > size) {
      goto need_more_data;
    }

    if (gst_jpeg_dec_parse_tag_has_entropy_segment (value)) {
      guint eseglen = dec->parse_entropy_len;

      GST_DEBUG ("0x%08x: finding entropy segment length", offset + 2);
      noffset = offset + 2 + frame_len + dec->parse_entropy_len;
      while (1) {
        noffset = gst_adapter_masked_scan_uint32_peek (adapter, 0x0000ff00,
            0x0000ff00, noffset, size - noffset, &value);
        if (noffset < 0) {
          /* need more data */
          dec->parse_entropy_len = size - offset - 4 - frame_len - 2;
          goto need_more_data;
        }
        if ((value & 0xff) != 0x00) {
          eseglen = noffset - offset - frame_len - 2;
          break;
        }
        noffset++;
      }
      dec->parse_entropy_len = 0;
      frame_len += eseglen;
      GST_DEBUG ("entropy segment length=%u => frame_len=%u", eseglen,
          frame_len);
    }
    if (resync) {
      /* check if we will still be in sync if we interpret
       * this as a sync point and skip this frame */
      noffset = offset + frame_len + 2;
      noffset = gst_adapter_masked_scan_uint32 (adapter, 0x0000ff00, 0x0000ff00,
          noffset, 4);
      if (noffset < 0) {
        /* ignore and continue resyncing until we hit the end
         * of our data or find a sync point that looks okay */
        offset++;
        continue;
      }
      GST_DEBUG ("found sync at 0x%x", offset + 2);
    }

    offset += frame_len + 2;
  }

  /* EXITS */
need_more_data:
  {
    dec->parse_offset = offset;
    dec->parse_resync = resync;
    return 0;
  }
}

/* shamelessly ripped from jpegutils.c in mjpegtools */
static void
add_huff_table (j_decompress_ptr dinfo,
    JHUFF_TBL ** htblptr, const UINT8 * bits, const UINT8 * val)
/* Define a Huffman table */
{
  int nsymbols, len;

  if (*htblptr == NULL)
    *htblptr = jpeg_alloc_huff_table ((j_common_ptr) dinfo);

  g_assert (*htblptr);

  /* Copy the number-of-symbols-of-each-code-length counts */
  memcpy ((*htblptr)->bits, bits, sizeof ((*htblptr)->bits));

  /* Validate the counts.  We do this here mainly so we can copy the right
   * number of symbols from the val[] array, without risking marching off
   * the end of memory.  jchuff.c will do a more thorough test later.
   */
  nsymbols = 0;
  for (len = 1; len <= 16; len++)
    nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256)
    g_error ("jpegutils.c:  add_huff_table failed badly. ");

  memcpy ((*htblptr)->huffval, val, nsymbols * sizeof (UINT8));
}



static void
std_huff_tables (j_decompress_ptr dinfo)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
  static const UINT8 bits_dc_luminance[17] =
      { /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_luminance[] =
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  static const UINT8 bits_dc_chrominance[17] =
      { /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_chrominance[] =
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  static const UINT8 bits_ac_luminance[17] =
      { /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
  static const UINT8 val_ac_luminance[] =
      { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };

  static const UINT8 bits_ac_chrominance[17] =
      { /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
  static const UINT8 val_ac_chrominance[] =
      { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
    0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };

  add_huff_table (dinfo, &dinfo->dc_huff_tbl_ptrs[0],
      bits_dc_luminance, val_dc_luminance);
  add_huff_table (dinfo, &dinfo->ac_huff_tbl_ptrs[0],
      bits_ac_luminance, val_ac_luminance);
  add_huff_table (dinfo, &dinfo->dc_huff_tbl_ptrs[1],
      bits_dc_chrominance, val_dc_chrominance);
  add_huff_table (dinfo, &dinfo->ac_huff_tbl_ptrs[1],
      bits_ac_chrominance, val_ac_chrominance);
}



static void
guarantee_huff_tables (j_decompress_ptr dinfo)
{
  if ((dinfo->dc_huff_tbl_ptrs[0] == NULL) &&
      (dinfo->dc_huff_tbl_ptrs[1] == NULL) &&
      (dinfo->ac_huff_tbl_ptrs[0] == NULL) &&
      (dinfo->ac_huff_tbl_ptrs[1] == NULL)) {
    GST_DEBUG ("Generating standard Huffman tables for this frame.");
    std_huff_tables (dinfo);
  }
}

static gboolean
gst_jpeg_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *s;
  GstJpegDec *dec;
  const GValue *framerate;

  dec = GST_JPEG_DEC (GST_OBJECT_PARENT (pad));
  s = gst_caps_get_structure (caps, 0);

  if ((framerate = gst_structure_get_value (s, "framerate")) != NULL) {
    dec->framerate_numerator = gst_value_get_fraction_numerator (framerate);
    dec->framerate_denominator = gst_value_get_fraction_denominator (framerate);
    dec->packetized = TRUE;
    GST_DEBUG ("got framerate of %d/%d fps => packetized mode",
        dec->framerate_numerator, dec->framerate_denominator);
  }

  /* do not extract width/height here. we do that in the chain
   * function on a per-frame basis (including the line[] array
   * setup) */

  /* But we can take the framerate values and set them on the src pad */

  return TRUE;
}

static GstCaps *
gst_jpeg_dec_getcaps (GstPad * pad)
{
  GstJpegDec *dec;
  GstCaps *caps;
  GstPad *peer;

  dec = GST_JPEG_DEC (GST_OBJECT_PARENT (pad));

  if (GST_PAD_CAPS (pad))
    return gst_caps_ref (GST_PAD_CAPS (pad));

  peer = gst_pad_get_peer (dec->srcpad);

  if (peer) {
    GstCaps *peer_caps;
    const GstCaps *templ_caps;
    GstStructure *s;
    guint i, n;

    peer_caps = gst_pad_get_caps (peer);

    /* Translate peercaps to image/jpeg */
    peer_caps = gst_caps_make_writable (peer_caps);
    n = gst_caps_get_size (peer_caps);
    for (i = 0; i < n; i++) {
      s = gst_caps_get_structure (peer_caps, i);

      gst_structure_set_name (s, "image/jpeg");
    }

    templ_caps = gst_pad_get_pad_template_caps (pad);
    caps = gst_caps_intersect_full (peer_caps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peer_caps);
    gst_object_unref (peer);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  return caps;
}


/* yuk */
static void
hresamplecpy1 (guint8 * dest, const guint8 * src, guint len)
{
  gint i;

  for (i = 0; i < len; ++i) {
    /* equivalent to: dest[i] = src[i << 1] */
    *dest = *src;
    ++dest;
    ++src;
    ++src;
  }
}

static void
gst_jpeg_dec_free_buffers (GstJpegDec * dec)
{
  gint i;

  for (i = 0; i < 16; i++) {
    g_free (dec->idr_y[i]);
    g_free (dec->idr_u[i]);
    g_free (dec->idr_v[i]);
    dec->idr_y[i] = NULL;
    dec->idr_u[i] = NULL;
    dec->idr_v[i] = NULL;
  }

  dec->idr_width_allocated = 0;
}

static inline gboolean
gst_jpeg_dec_ensure_buffers (GstJpegDec * dec, guint maxrowbytes)
{
  gint i;

  if (G_LIKELY (dec->idr_width_allocated == maxrowbytes))
    return TRUE;

  /* FIXME: maybe just alloc one or three blocks altogether? */
  for (i = 0; i < 16; i++) {
    dec->idr_y[i] = g_try_realloc (dec->idr_y[i], maxrowbytes);
    dec->idr_u[i] = g_try_realloc (dec->idr_u[i], maxrowbytes);
    dec->idr_v[i] = g_try_realloc (dec->idr_v[i], maxrowbytes);

    if (G_UNLIKELY (!dec->idr_y[i] || !dec->idr_u[i] || !dec->idr_v[i])) {
      GST_WARNING_OBJECT (dec, "out of memory, i=%d, bytes=%u", i, maxrowbytes);
      return FALSE;
    }
  }

  dec->idr_width_allocated = maxrowbytes;
  GST_LOG_OBJECT (dec, "allocated temp memory, %u bytes/row", maxrowbytes);
  return TRUE;
}

static void
gst_jpeg_dec_decode_grayscale (GstJpegDec * dec, guchar * base[1],
    guint width, guint height, guint pstride, guint rstride)
{
  guchar *rows[16];
  guchar **scanarray[1] = { rows };
  gint i, j, k;
  gint lines;

  GST_DEBUG_OBJECT (dec, "indirect decoding of grayscale");

  if (G_UNLIKELY (!gst_jpeg_dec_ensure_buffers (dec, GST_ROUND_UP_32 (width))))
    return;

  memcpy (rows, dec->idr_y, 16 * sizeof (gpointer));

  i = 0;
  while (i < height) {
    lines = jpeg_read_raw_data (&dec->cinfo, scanarray, DCTSIZE);
    if (G_LIKELY (lines > 0)) {
      for (j = 0; (j < DCTSIZE) && (i < height); j++, i++) {
        gint p;

        p = 0;
        for (k = 0; k < width; k++) {
          base[0][p] = rows[j][k];
          p += pstride;
        }
        base[0] += rstride;
      }
    } else {
      GST_INFO_OBJECT (dec, "jpeg_read_raw_data() returned 0");
    }
  }
}

static void
gst_jpeg_dec_decode_rgb (GstJpegDec * dec, guchar * base[3],
    guint width, guint height, guint pstride, guint rstride)
{
  guchar *r_rows[16], *g_rows[16], *b_rows[16];
  guchar **scanarray[3] = { r_rows, g_rows, b_rows };
  gint i, j, k;
  gint lines;

  GST_DEBUG_OBJECT (dec, "indirect decoding of RGB");

  if (G_UNLIKELY (!gst_jpeg_dec_ensure_buffers (dec, GST_ROUND_UP_32 (width))))
    return;

  memcpy (r_rows, dec->idr_y, 16 * sizeof (gpointer));
  memcpy (g_rows, dec->idr_u, 16 * sizeof (gpointer));
  memcpy (b_rows, dec->idr_v, 16 * sizeof (gpointer));

  i = 0;
  while (i < height) {
    lines = jpeg_read_raw_data (&dec->cinfo, scanarray, DCTSIZE);
    if (G_LIKELY (lines > 0)) {
      for (j = 0; (j < DCTSIZE) && (i < height); j++, i++) {
        gint p;

        p = 0;
        for (k = 0; k < width; k++) {
          base[0][p] = r_rows[j][k];
          base[1][p] = g_rows[j][k];
          base[2][p] = b_rows[j][k];
          p += pstride;
        }
        base[0] += rstride;
        base[1] += rstride;
        base[2] += rstride;
      }
    } else {
      GST_INFO_OBJECT (dec, "jpeg_read_raw_data() returned 0");
    }
  }
}

static void
gst_jpeg_dec_decode_indirect (GstJpegDec * dec, guchar * base[3],
    guchar * last[3], guint width, guint height, gint r_v, gint r_h, gint comp)
{
  guchar *y_rows[16], *u_rows[16], *v_rows[16];
  guchar **scanarray[3] = { y_rows, u_rows, v_rows };
  gint i, j, k;
  gint lines;

  GST_DEBUG_OBJECT (dec,
      "unadvantageous width or r_h, taking slow route involving memcpy");

  if (G_UNLIKELY (!gst_jpeg_dec_ensure_buffers (dec, GST_ROUND_UP_32 (width))))
    return;

  memcpy (y_rows, dec->idr_y, 16 * sizeof (gpointer));
  memcpy (u_rows, dec->idr_u, 16 * sizeof (gpointer));
  memcpy (v_rows, dec->idr_v, 16 * sizeof (gpointer));

  /* fill chroma components for grayscale */
  if (comp == 1) {
    GST_DEBUG_OBJECT (dec, "grayscale, filling chroma");
    for (i = 0; i < 16; i++) {
      memset (u_rows[i], GST_ROUND_UP_32 (width), 0x80);
      memset (v_rows[i], GST_ROUND_UP_32 (width), 0x80);
    }
  }

  for (i = 0; i < height; i += r_v * DCTSIZE) {
    lines = jpeg_read_raw_data (&dec->cinfo, scanarray, r_v * DCTSIZE);
    if (G_LIKELY (lines > 0)) {
      for (j = 0, k = 0; j < (r_v * DCTSIZE); j += r_v, k++) {
        if (G_LIKELY (base[0] <= last[0])) {
          memcpy (base[0], y_rows[j], I420_Y_ROWSTRIDE (width));
          base[0] += I420_Y_ROWSTRIDE (width);
        }
        if (r_v == 2) {
          if (G_LIKELY (base[0] <= last[0])) {
            memcpy (base[0], y_rows[j + 1], I420_Y_ROWSTRIDE (width));
            base[0] += I420_Y_ROWSTRIDE (width);
          }
        }
        if (G_LIKELY (base[1] <= last[1] && base[2] <= last[2])) {
          if (r_h == 2) {
            memcpy (base[1], u_rows[k], I420_U_ROWSTRIDE (width));
            memcpy (base[2], v_rows[k], I420_V_ROWSTRIDE (width));
          } else if (r_h == 1) {
            hresamplecpy1 (base[1], u_rows[k], I420_U_ROWSTRIDE (width));
            hresamplecpy1 (base[2], v_rows[k], I420_V_ROWSTRIDE (width));
          } else {
            /* FIXME: implement (at least we avoid crashing by doing nothing) */
          }
        }

        if (r_v == 2 || (k & 1) != 0) {
          base[1] += I420_U_ROWSTRIDE (width);
          base[2] += I420_V_ROWSTRIDE (width);
        }
      }
    } else {
      GST_INFO_OBJECT (dec, "jpeg_read_raw_data() returned 0");
    }
  }
}

#ifndef GST_DISABLE_GST_DEBUG
static inline void
dump_lines (guchar * base[3], guchar ** line[3], int v_samp0, int width)
{
  int j;

  for (j = 0; j < (v_samp0 * DCTSIZE); ++j) {
    GST_LOG ("[%02d]  %5d  %5d  %5d", j,
        (line[0][j] >= base[0]) ?
        (int) (line[0][j] - base[0]) / I420_Y_ROWSTRIDE (width) : -1,
        (line[1][j] >= base[1]) ?
        (int) (line[1][j] - base[1]) / I420_U_ROWSTRIDE (width) : -1,
        (line[2][j] >= base[2]) ?
        (int) (line[2][j] - base[2]) / I420_V_ROWSTRIDE (width) : -1);
  }
}
#endif

static GstFlowReturn
gst_jpeg_dec_decode_direct (GstJpegDec * dec, guchar * base[3],
    guchar * last[3], guint width, guint height)
{
  guchar **line[3];             /* the jpeg line buffer         */
  guchar *y[4 * DCTSIZE] = { NULL, };   /* alloc enough for the lines   */
  guchar *u[4 * DCTSIZE] = { NULL, };   /* r_v will be <4               */
  guchar *v[4 * DCTSIZE] = { NULL, };
  gint i, j;
  gint lines, v_samp[3];

  line[0] = y;
  line[1] = u;
  line[2] = v;

  v_samp[0] = dec->cinfo.comp_info[0].v_samp_factor;
  v_samp[1] = dec->cinfo.comp_info[1].v_samp_factor;
  v_samp[2] = dec->cinfo.comp_info[2].v_samp_factor;

  if (G_UNLIKELY (v_samp[0] > 2 || v_samp[1] > 2 || v_samp[2] > 2))
    goto format_not_supported;

  /* let jpeglib decode directly into our final buffer */
  GST_DEBUG_OBJECT (dec, "decoding directly into output buffer");

  for (i = 0; i < height; i += v_samp[0] * DCTSIZE) {
    for (j = 0; j < (v_samp[0] * DCTSIZE); ++j) {
      /* Y */
      line[0][j] = base[0] + (i + j) * I420_Y_ROWSTRIDE (width);
      if (G_UNLIKELY (line[0][j] > last[0]))
        line[0][j] = last[0];
      /* U */
      if (v_samp[1] == v_samp[0]) {
        line[1][j] = base[1] + ((i + j) / 2) * I420_U_ROWSTRIDE (width);
      } else if (j < (v_samp[1] * DCTSIZE)) {
        line[1][j] = base[1] + ((i / 2) + j) * I420_U_ROWSTRIDE (width);
      }
      if (G_UNLIKELY (line[1][j] > last[1]))
        line[1][j] = last[1];
      /* V */
      if (v_samp[2] == v_samp[0]) {
        line[2][j] = base[2] + ((i + j) / 2) * I420_V_ROWSTRIDE (width);
      } else if (j < (v_samp[2] * DCTSIZE)) {
        line[2][j] = base[2] + ((i / 2) + j) * I420_V_ROWSTRIDE (width);
      }
      if (G_UNLIKELY (line[2][j] > last[2]))
        line[2][j] = last[2];
    }

    /* dump_lines (base, line, v_samp[0], width); */

    lines = jpeg_read_raw_data (&dec->cinfo, line, v_samp[0] * DCTSIZE);
    if (G_UNLIKELY (!lines)) {
      GST_INFO_OBJECT (dec, "jpeg_read_raw_data() returned 0");
    }
  }
  return GST_FLOW_OK;

format_not_supported:
  {
    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "Unsupported subsampling schema: v_samp factors: %u %u %u",
        v_samp[0], v_samp[1], v_samp[2]);
    return GST_FLOW_ERROR;
  }
}

static void
gst_jpeg_dec_update_qos (GstJpegDec * dec, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime ts)
{
  GST_OBJECT_LOCK (dec);
  dec->proportion = proportion;
  if (G_LIKELY (ts != GST_CLOCK_TIME_NONE)) {
    if (G_UNLIKELY (diff > 0))
      dec->earliest_time = ts + 2 * diff + dec->qos_duration;
    else
      dec->earliest_time = ts + diff;
  } else {
    dec->earliest_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (dec);
}

static void
gst_jpeg_dec_reset_qos (GstJpegDec * dec)
{
  gst_jpeg_dec_update_qos (dec, 0.5, 0, GST_CLOCK_TIME_NONE);
}

static void
gst_jpeg_dec_read_qos (GstJpegDec * dec, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (dec);
  *proportion = dec->proportion;
  *time = dec->earliest_time;
  GST_OBJECT_UNLOCK (dec);
}

/* Perform qos calculations before decoding the next frame. Returns TRUE if the
 * frame should be decoded, FALSE if the frame can be dropped entirely */
static gboolean
gst_jpeg_dec_do_qos (GstJpegDec * dec, GstClockTime timestamp)
{
  GstClockTime qostime, earliest_time;
  gdouble proportion;

  /* no timestamp, can't do QoS => decode frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (dec, "invalid timestamp, can't do QoS, decode frame");
    return TRUE;
  }

  /* get latest QoS observation values */
  gst_jpeg_dec_read_qos (dec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => decode frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (dec, "no observation yet, decode frame");
    return TRUE;
  }

  /* qos is done on running time */
  qostime = gst_segment_to_running_time (&dec->segment, GST_FORMAT_TIME,
      timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (dec, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  if (qostime != GST_CLOCK_TIME_NONE && qostime <= earliest_time) {
    GST_DEBUG_OBJECT (dec, "we are late, drop frame");
    return FALSE;
  }

  GST_LOG_OBJECT (dec, "decode frame");
  return TRUE;
}

static void
gst_jpeg_dec_negotiate (GstJpegDec * dec, gint width, gint height, gint clrspc)
{
  GstCaps *caps;
  GstVideoFormat format;

  if (G_UNLIKELY (width == dec->caps_width && height == dec->caps_height &&
          dec->framerate_numerator == dec->caps_framerate_numerator &&
          dec->framerate_denominator == dec->caps_framerate_denominator &&
          clrspc == dec->clrspc))
    return;

  /* framerate == 0/1 is a still frame */
  if (dec->framerate_denominator == 0) {
    dec->framerate_numerator = 0;
    dec->framerate_denominator = 1;
  }

  /* calculate or assume an average frame duration for QoS purposes */
  GST_OBJECT_LOCK (dec);
  if (dec->framerate_numerator != 0) {
    dec->qos_duration = gst_util_uint64_scale (GST_SECOND,
        dec->framerate_denominator, dec->framerate_numerator);
  } else {
    /* if not set just use 25fps */
    dec->qos_duration = gst_util_uint64_scale (GST_SECOND, 1, 25);
  }
  GST_OBJECT_UNLOCK (dec);

  if (dec->cinfo.jpeg_color_space == JCS_RGB) {
    gint i;
    GstCaps *allowed_caps;

    GST_DEBUG_OBJECT (dec, "selecting RGB format");
    /* retrieve allowed caps, and find the first one that reasonably maps
     * to the parameters of the colourspace */
    caps = gst_pad_get_allowed_caps (dec->srcpad);
    if (!caps) {
      GST_DEBUG_OBJECT (dec, "... but no peer, using template caps");
      /* need to copy because get_allowed_caps returns a ref,
       * and get_pad_template_caps doesn't */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (dec->srcpad));
    }
    /* avoid lists of fourcc, etc */
    allowed_caps = gst_caps_normalize (caps);
    gst_caps_unref (caps);
    caps = NULL;
    GST_LOG_OBJECT (dec, "allowed source caps %" GST_PTR_FORMAT, allowed_caps);

    for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
      if (caps)
        gst_caps_unref (caps);
      caps = gst_caps_copy_nth (allowed_caps, i);
      /* sigh, ds and _parse_caps need fixed caps for parsing, fixate */
      gst_pad_fixate_caps (dec->srcpad, caps);
      GST_LOG_OBJECT (dec, "checking caps %" GST_PTR_FORMAT, caps);
      if (!gst_video_format_parse_caps (caps, &format, NULL, NULL))
        continue;
      /* we'll settle for the first (preferred) downstream rgb format */
      if (gst_video_format_is_rgb (format))
        break;
      /* default fall-back */
      format = GST_VIDEO_FORMAT_RGB;
    }
    if (caps)
      gst_caps_unref (caps);
    gst_caps_unref (allowed_caps);
    caps = gst_video_format_new_caps (format, width, height,
        dec->framerate_numerator, dec->framerate_denominator, 1, 1);
    dec->outsize = gst_video_format_get_size (format, width, height);
    /* some format info */
    dec->offset[0] =
        gst_video_format_get_component_offset (format, 0, width, height);
    dec->offset[1] =
        gst_video_format_get_component_offset (format, 1, width, height);
    dec->offset[2] =
        gst_video_format_get_component_offset (format, 2, width, height);
    /* equal for all components */
    dec->stride = gst_video_format_get_row_stride (format, 0, width);
    dec->inc = gst_video_format_get_pixel_stride (format, 0);
  } else if (dec->cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    /* TODO is anything else then 8bit supported in jpeg? */
    format = GST_VIDEO_FORMAT_GRAY8;
    caps = gst_video_format_new_caps (format, width, height,
        dec->framerate_numerator, dec->framerate_denominator, 1, 1);
    dec->outsize = gst_video_format_get_size (format, width, height);
    dec->offset[0] =
        gst_video_format_get_component_offset (format, 0, width, height);
    dec->stride = gst_video_format_get_row_stride (format, 0, width);
    dec->inc = gst_video_format_get_pixel_stride (format, 0);
  } else {
    /* go for plain and simple I420 */
    /* TODO other YUV cases ? */
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, dec->framerate_numerator,
        dec->framerate_denominator, NULL);
    dec->outsize = I420_SIZE (width, height);
  }

  GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (dec, "max_v_samp_factor=%d", dec->cinfo.max_v_samp_factor);
  GST_DEBUG_OBJECT (dec, "max_h_samp_factor=%d", dec->cinfo.max_h_samp_factor);

  gst_pad_set_caps (dec->srcpad, caps);
  gst_caps_unref (caps);

  dec->caps_width = width;
  dec->caps_height = height;
  dec->caps_framerate_numerator = dec->framerate_numerator;
  dec->caps_framerate_denominator = dec->framerate_denominator;
}

static GstFlowReturn
gst_jpeg_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstJpegDec *dec;
  GstBuffer *outbuf = NULL;
#ifndef GST_DISABLE_GST_DEBUG
  guchar *data;
#endif
  guchar *outdata;
  guchar *base[3], *last[3];
  gint img_len;
  guint outsize;
  gint width, height;
  gint r_h, r_v;
  guint code, hdr_ok;
  GstClockTime timestamp, duration;

  dec = GST_JPEG_DEC (GST_PAD_PARENT (pad));

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    dec->next_ts = timestamp;

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (dec, "buffer has DISCONT flag set");
    dec->discont = TRUE;
    if (!dec->packetized && gst_adapter_available (dec->adapter)) {
      GST_WARNING_OBJECT (dec, "DISCONT buffer in non-packetized mode, bad");
      gst_adapter_clear (dec->adapter);
    }
  }

  gst_adapter_push (dec->adapter, buf);
  buf = NULL;

  /* If we are non-packetized and know the total incoming size in bytes,
   * just wait until we have enough before doing any processing. */

  if (!dec->packetized && (dec->segment.format == GST_FORMAT_BYTES) &&
      (dec->segment.stop != -1) &&
      (gst_adapter_available (dec->adapter) < dec->segment.stop)) {
    /* We assume that non-packetized input in bytes is *one* single jpeg image */
    GST_DEBUG ("Non-packetized mode. Got %d bytes, need %" G_GINT64_FORMAT,
        gst_adapter_available (dec->adapter), dec->segment.stop);
    goto need_more_data;
  }

again:
  if (!gst_jpeg_dec_ensure_header (dec))
    goto need_more_data;

  /* If we know that each input buffer contains data
   * for a whole jpeg image (e.g. MJPEG streams), just 
   * do some sanity checking instead of parsing all of 
   * the jpeg data */
  if (dec->packetized) {
    img_len = gst_adapter_available (dec->adapter);
  } else {
    /* Parse jpeg image to handle jpeg input that
     * is not aligned to buffer boundaries */
    img_len = gst_jpeg_dec_parse_image_data (dec);

    if (img_len == 0) {
      goto need_more_data;
    } else if (img_len < 0) {
      gst_adapter_flush (dec->adapter, -img_len);
      goto again;
    }
  }

  dec->rem_img_len = img_len;

  GST_LOG_OBJECT (dec, "image size = %u", img_len);

  /* QoS: if we're too late anyway, skip decoding */
  if (dec->packetized && !gst_jpeg_dec_do_qos (dec, timestamp))
    goto skip_decoding;

#ifndef GST_DISABLE_GST_DEBUG
  data = (guint8 *) gst_adapter_peek (dec->adapter, 4);
  GST_LOG_OBJECT (dec, "reading header %02x %02x %02x %02x", data[0], data[1],
      data[2], data[3]);
#endif

  gst_jpeg_dec_fill_input_buffer (&dec->cinfo);

  if (setjmp (dec->jerr.setjmp_buffer)) {
    code = dec->jerr.pub.msg_code;

    if (code == JERR_INPUT_EOF) {
      GST_DEBUG ("jpeg input EOF error, we probably need more data");
      goto need_more_data;
    }
    goto decode_error;
  }

  /* read header */
  hdr_ok = jpeg_read_header (&dec->cinfo, TRUE);
  if (G_UNLIKELY (hdr_ok != JPEG_HEADER_OK)) {
    GST_WARNING_OBJECT (dec, "reading the header failed, %d", hdr_ok);
  }

  GST_LOG_OBJECT (dec, "num_components=%d", dec->cinfo.num_components);
  GST_LOG_OBJECT (dec, "jpeg_color_space=%d", dec->cinfo.jpeg_color_space);

  if (!dec->cinfo.num_components || !dec->cinfo.comp_info)
    goto components_not_supported;

  r_h = dec->cinfo.comp_info[0].h_samp_factor;
  r_v = dec->cinfo.comp_info[0].v_samp_factor;

  GST_LOG_OBJECT (dec, "r_h = %d, r_v = %d", r_h, r_v);

  if (dec->cinfo.num_components > 3)
    goto components_not_supported;

  /* verify color space expectation to avoid going *boom* or bogus output */
  if (dec->cinfo.jpeg_color_space != JCS_YCbCr &&
      dec->cinfo.jpeg_color_space != JCS_GRAYSCALE &&
      dec->cinfo.jpeg_color_space != JCS_RGB)
    goto unsupported_colorspace;

#ifndef GST_DISABLE_GST_DEBUG
  {
    gint i;

    for (i = 0; i < dec->cinfo.num_components; ++i) {
      GST_LOG_OBJECT (dec, "[%d] h_samp_factor=%d, v_samp_factor=%d, cid=%d",
          i, dec->cinfo.comp_info[i].h_samp_factor,
          dec->cinfo.comp_info[i].v_samp_factor,
          dec->cinfo.comp_info[i].component_id);
    }
  }
#endif

  /* prepare for raw output */
  dec->cinfo.do_fancy_upsampling = FALSE;
  dec->cinfo.do_block_smoothing = FALSE;
  dec->cinfo.out_color_space = dec->cinfo.jpeg_color_space;
  dec->cinfo.dct_method = dec->idct_method;
  dec->cinfo.raw_data_out = TRUE;

  GST_LOG_OBJECT (dec, "starting decompress");
  guarantee_huff_tables (&dec->cinfo);
  if (!jpeg_start_decompress (&dec->cinfo)) {
    GST_WARNING_OBJECT (dec, "failed to start decompression cycle");
  }

  /* sanity checks to get safe and reasonable output */
  switch (dec->cinfo.jpeg_color_space) {
    case JCS_GRAYSCALE:
      if (dec->cinfo.num_components != 1)
        goto invalid_yuvrgbgrayscale;
      break;
    case JCS_RGB:
      if (dec->cinfo.num_components != 3 || dec->cinfo.max_v_samp_factor > 1 ||
          dec->cinfo.max_h_samp_factor > 1)
        goto invalid_yuvrgbgrayscale;
      break;
    case JCS_YCbCr:
      if (dec->cinfo.num_components != 3 ||
          r_v > 2 || r_v < dec->cinfo.comp_info[0].v_samp_factor ||
          r_v < dec->cinfo.comp_info[1].v_samp_factor ||
          r_h < dec->cinfo.comp_info[0].h_samp_factor ||
          r_h < dec->cinfo.comp_info[1].h_samp_factor)
        goto invalid_yuvrgbgrayscale;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  width = dec->cinfo.output_width;
  height = dec->cinfo.output_height;

  if (G_UNLIKELY (width < MIN_WIDTH || width > MAX_WIDTH ||
          height < MIN_HEIGHT || height > MAX_HEIGHT))
    goto wrong_size;

  gst_jpeg_dec_negotiate (dec, width, height, dec->cinfo.jpeg_color_space);

  ret = gst_pad_alloc_buffer_and_set_caps (dec->srcpad, GST_BUFFER_OFFSET_NONE,
      dec->outsize, GST_PAD_CAPS (dec->srcpad), &outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto alloc_failed;

  outdata = GST_BUFFER_DATA (outbuf);
  outsize = GST_BUFFER_SIZE (outbuf);

  GST_LOG_OBJECT (dec, "width %d, height %d, buffer size %d, required size %d",
      width, height, outsize, dec->outsize);

  GST_BUFFER_TIMESTAMP (outbuf) = dec->next_ts;

  if (dec->packetized && GST_CLOCK_TIME_IS_VALID (dec->next_ts)) {
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      /* use duration from incoming buffer for outgoing buffer */
      dec->next_ts += duration;
    } else if (dec->framerate_numerator != 0) {
      duration = gst_util_uint64_scale (GST_SECOND,
          dec->framerate_denominator, dec->framerate_numerator);
      dec->next_ts += duration;
    } else {
      duration = GST_CLOCK_TIME_NONE;
      dec->next_ts = GST_CLOCK_TIME_NONE;
    }
  } else {
    duration = GST_CLOCK_TIME_NONE;
    dec->next_ts = GST_CLOCK_TIME_NONE;
  }
  GST_BUFFER_DURATION (outbuf) = duration;

  if (dec->cinfo.jpeg_color_space == JCS_RGB) {
    base[0] = outdata + dec->offset[0];
    base[1] = outdata + dec->offset[1];
    base[2] = outdata + dec->offset[2];
    gst_jpeg_dec_decode_rgb (dec, base, width, height, dec->inc, dec->stride);
  } else if (dec->cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    base[0] = outdata + dec->offset[0];
    gst_jpeg_dec_decode_grayscale (dec, base, width, height, dec->inc,
        dec->stride);
  } else {
    /* mind the swap, jpeglib outputs blue chroma first
     * ensonic: I see no swap?
     */
    base[0] = outdata + I420_Y_OFFSET (width, height);
    base[1] = outdata + I420_U_OFFSET (width, height);
    base[2] = outdata + I420_V_OFFSET (width, height);

    /* make sure we don't make jpeglib write beyond our buffer,
     * which might happen if (height % (r_v*DCTSIZE)) != 0 */
    last[0] = base[0] + (I420_Y_ROWSTRIDE (width) * (height - 1));
    last[1] =
        base[1] + (I420_U_ROWSTRIDE (width) * ((GST_ROUND_UP_2 (height) / 2) -
            1));
    last[2] =
        base[2] + (I420_V_ROWSTRIDE (width) * ((GST_ROUND_UP_2 (height) / 2) -
            1));

    GST_LOG_OBJECT (dec, "decompressing (reqired scanline buffer height = %u)",
        dec->cinfo.rec_outbuf_height);

    /* For some widths jpeglib requires more horizontal padding than I420 
     * provides. In those cases we need to decode into separate buffers and then
     * copy over the data into our final picture buffer, otherwise jpeglib might
     * write over the end of a line into the beginning of the next line,
     * resulting in blocky artifacts on the left side of the picture. */
    if (G_UNLIKELY (width % (dec->cinfo.max_h_samp_factor * DCTSIZE) != 0
            || dec->cinfo.comp_info[0].h_samp_factor != 2
            || dec->cinfo.comp_info[1].h_samp_factor != 1
            || dec->cinfo.comp_info[2].h_samp_factor != 1)) {
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, dec,
          "indirect decoding using extra buffer copy");
      gst_jpeg_dec_decode_indirect (dec, base, last, width, height, r_v, r_h,
          dec->cinfo.num_components);
    } else {
      ret = gst_jpeg_dec_decode_direct (dec, base, last, width, height);

      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto decode_direct_failed;
    }
  }

  GST_LOG_OBJECT (dec, "decompressing finished");
  jpeg_finish_decompress (&dec->cinfo);

  /* Clipping */
  if (dec->segment.format == GST_FORMAT_TIME) {
    gint64 start, stop, clip_start, clip_stop;

    GST_LOG_OBJECT (dec, "Attempting clipping");

    start = GST_BUFFER_TIMESTAMP (outbuf);
    if (GST_BUFFER_DURATION (outbuf) == GST_CLOCK_TIME_NONE)
      stop = start;
    else
      stop = start + GST_BUFFER_DURATION (outbuf);

    if (gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
            start, stop, &clip_start, &clip_stop)) {
      GST_LOG_OBJECT (dec, "Clipping start to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (clip_start));
      GST_BUFFER_TIMESTAMP (outbuf) = clip_start;
      if (GST_BUFFER_DURATION (outbuf) != GST_CLOCK_TIME_NONE) {
        GST_LOG_OBJECT (dec, "Clipping duration to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (clip_stop - clip_start));
        GST_BUFFER_DURATION (outbuf) = clip_stop - clip_start;
      }
    } else
      goto drop_buffer;
  }

  /* reset error count on successful decode */
  dec->error_count = 0;

  ++dec->good_count;

  GST_LOG_OBJECT (dec, "pushing buffer (ts=%" GST_TIME_FORMAT ", dur=%"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

  ret = gst_pad_push (dec->srcpad, outbuf);

skip_decoding:
done:
  gst_adapter_flush (dec->adapter, dec->rem_img_len);

exit:

  if (G_UNLIKELY (ret == GST_FLOW_ERROR)) {
    jpeg_abort_decompress (&dec->cinfo);
    ret = gst_jpeg_dec_post_error_or_warning (dec);
  }

  return ret;

  /* special cases */
need_more_data:
  {
    GST_LOG_OBJECT (dec, "we need more data");
    if (outbuf) {
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }
    ret = GST_FLOW_OK;
    goto exit;
  }
  /* ERRORS */
wrong_size:
  {
    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "Picture is too small or too big (%ux%u)", width, height);
    ret = GST_FLOW_ERROR;
    goto done;
  }
decode_error:
  {
    gchar err_msg[JMSG_LENGTH_MAX];

    dec->jerr.pub.format_message ((j_common_ptr) (&dec->cinfo), err_msg);

    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "Decode error #%u: %s", code, err_msg);

    if (outbuf) {
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }
    ret = GST_FLOW_ERROR;
    goto done;
  }
decode_direct_failed:
  {
    /* already posted an error message */
    jpeg_abort_decompress (&dec->cinfo);
    gst_buffer_replace (&outbuf, NULL);
    goto done;
  }
alloc_failed:
  {
    const gchar *reason;

    reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (dec, "failed to alloc buffer, reason %s", reason);
    /* Reset for next time */
    jpeg_abort_decompress (&dec->cinfo);
    if (ret != GST_FLOW_UNEXPECTED && ret != GST_FLOW_WRONG_STATE &&
        ret != GST_FLOW_NOT_LINKED) {
      gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
          "Buffer allocation failed, reason: %s", reason);
    }
    goto exit;
  }
drop_buffer:
  {
    GST_WARNING_OBJECT (dec, "Outgoing buffer is outside configured segment");
    gst_buffer_unref (outbuf);
    ret = GST_FLOW_OK;
    goto exit;
  }
components_not_supported:
  {
    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "number of components not supported: %d (max 3)",
        dec->cinfo.num_components);
    ret = GST_FLOW_ERROR;
    goto done;
  }
unsupported_colorspace:
  {
    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "Picture has unknown or unsupported colourspace");
    ret = GST_FLOW_ERROR;
    goto done;
  }
invalid_yuvrgbgrayscale:
  {
    gst_jpeg_dec_set_error (dec, GST_FUNCTION, __LINE__,
        "Picture is corrupt or unhandled YUV/RGB/grayscale layout");
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
gst_jpeg_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstJpegDec *dec;
  gboolean res;

  dec = GST_JPEG_DEC (gst_pad_get_parent (pad));
  if (G_UNLIKELY (dec == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);
      gst_jpeg_dec_update_qos (dec, proportion, diff, timestamp);
      break;
    }
    default:
      break;
  }

  res = gst_pad_push_event (dec->sinkpad, event);

  gst_object_unref (dec);
  return res;
}

static gboolean
gst_jpeg_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstJpegDec *dec = GST_JPEG_DEC (GST_OBJECT_PARENT (pad));

  GST_DEBUG_OBJECT (dec, "event : %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (dec, "Aborting decompress");
      jpeg_abort_decompress (&dec->cinfo);
      gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
      gst_adapter_clear (dec->adapter);
      g_free (dec->cur_buf);
      dec->cur_buf = NULL;
      dec->parse_offset = 0;
      dec->parse_entropy_len = 0;
      dec->parse_resync = FALSE;
      gst_jpeg_dec_reset_qos (dec);
      break;
    case GST_EVENT_NEWSEGMENT:{
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      GST_DEBUG_OBJECT (dec, "Got NEWSEGMENT [%" GST_TIME_FORMAT
          " - %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "]",
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (position));

      gst_segment_set_newsegment_full (&dec->segment, update, rate,
          applied_rate, format, start, stop, position);

      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (dec->srcpad, event);

  return ret;
}

static void
gst_jpeg_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJpegDec *dec;

  dec = GST_JPEG_DEC (object);

  switch (prop_id) {
    case PROP_IDCT_METHOD:
      dec->idct_method = g_value_get_enum (value);
      break;
    case PROP_MAX_ERRORS:
      g_atomic_int_set (&dec->max_errors, g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_jpeg_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstJpegDec *dec;

  dec = GST_JPEG_DEC (object);

  switch (prop_id) {
    case PROP_IDCT_METHOD:
      g_value_set_enum (value, dec->idct_method);
      break;
    case PROP_MAX_ERRORS:
      g_value_set_int (value, g_atomic_int_get (&dec->max_errors));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_jpeg_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstJpegDec *dec;

  dec = GST_JPEG_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->error_count = 0;
      dec->good_count = 0;
      dec->framerate_numerator = 0;
      dec->framerate_denominator = 1;
      dec->caps_framerate_numerator = dec->caps_framerate_denominator = 0;
      dec->caps_width = -1;
      dec->caps_height = -1;
      dec->clrspc = -1;
      dec->packetized = FALSE;
      dec->next_ts = 0;
      dec->discont = TRUE;
      dec->parse_offset = 0;
      dec->parse_entropy_len = 0;
      dec->parse_resync = FALSE;
      dec->cur_buf = NULL;
      gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
      gst_jpeg_dec_reset_qos (dec);
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (dec->adapter);
      g_free (dec->cur_buf);
      dec->cur_buf = NULL;
      gst_jpeg_dec_free_buffers (dec);
      break;
    default:
      break;
  }

  return ret;
}
