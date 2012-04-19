/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <inttypes.h>

#include "gstmpeg2dec.h"

/* 16byte-aligns a buffer for libmpeg2 */
#define ALIGN_16(p) ((void *)(((uintptr_t)(p) + 15) & ~((uintptr_t)15)))

/* mpeg2dec changed a struct name after 0.3.1, here's a workaround */
/* mpeg2dec also only defined MPEG2_RELEASE after 0.3.1
   #if MPEG2_RELEASE < MPEG2_VERSION(0,3,2)
*/
#ifndef MPEG2_RELEASE
#define MPEG2_VERSION(a,b,c) ((((a)&0xff)<<16)|(((b)&0xff)<<8)|((c)&0xff))
#define MPEG2_RELEASE MPEG2_VERSION(0,3,1)
typedef picture_t mpeg2_picture_t;
typedef gint mpeg2_state_t;

#define STATE_BUFFER 0
#endif

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);
GST_DEBUG_CATEGORY_STATIC (mpeg2dec_debug);
#define GST_CAT_DEFAULT (mpeg2dec_debug)

/* Send a warning message about decoding errors after receiving this many
 * STATE_INVALID return values from mpeg2_parse. -1 means never.
 */
#define WARN_THRESHOLD (5)

//#define enable_user_data
#ifdef enable_user_data
static GstStaticPadTemplate user_data_template_factory =
GST_STATIC_PAD_TEMPLATE ("user_data",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
#endif

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { I420, Y42B, Y444 }, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]")
    );

static void gst_mpeg2dec_finalize (GObject * object);
static void gst_mpeg2dec_reset (GstMpeg2dec * mpeg2dec);

static gboolean gst_mpeg2dec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn gst_mpeg2dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_mpeg2dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mpeg2dec_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_mpeg2dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static void clear_buffers (GstMpeg2dec * mpeg2dec);

//static gboolean gst_mpeg2dec_sink_query (GstPad * pad, GstObject * parent, GstQuery * query);

#if 0
static const GstFormat *gst_mpeg2dec_get_formats (GstPad * pad);
#endif

#if 0
static const GstEventMask *gst_mpeg2dec_get_event_masks (GstPad * pad);
#endif

static gboolean gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstBuffer ** buf,
    GstVideoFrame * frame);

/*static guint gst_mpeg2dec_signals[LAST_SIGNAL] = { 0 };*/

#define gst_mpeg2dec_parent_class parent_class
G_DEFINE_TYPE (GstMpeg2dec, gst_mpeg2dec, GST_TYPE_ELEMENT);

static void
gst_mpeg2dec_class_init (GstMpeg2decClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_mpeg2dec_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template_factory));
#ifdef enable_user_data
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&user_data_template_factory));
#endif
  gst_element_class_set_static_metadata (gstelement_class,
      "mpeg1 and mpeg2 video decoder", "Codec/Decoder/Video",
      "Uses libmpeg2 to decode MPEG video streams",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_class->change_state = gst_mpeg2dec_change_state;

  GST_DEBUG_CATEGORY_INIT (mpeg2dec_debug, "mpeg2dec", 0,
      "MPEG2 decoder element");
}

static void
gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec)
{
  /* create the sink and src pads */
  mpeg2dec->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_pad_set_chain_function (mpeg2dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_chain));
#if 0
  gst_pad_set_query_function (mpeg2dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_sink_query));
#endif
  gst_pad_set_event_function (mpeg2dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->sinkpad);

  mpeg2dec->srcpad =
      gst_pad_new_from_static_template (&src_template_factory, "src");
  gst_pad_set_event_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_event));
#if 0
  gst_pad_set_query_function (mpeg2dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_query));
#endif
  gst_pad_use_fixed_caps (mpeg2dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->srcpad);

#ifdef enable_user_data
  mpeg2dec->userdatapad =
      gst_pad_new_from_static_template (&user_data_template_factory,
      "user_data");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->userdatapad);
#endif

  mpeg2dec->error_count = 0;
  mpeg2dec->can_allocate_aligned = TRUE;

  /* initialize the mpeg2dec acceleration */
}

static void
gst_mpeg2dec_finalize (GObject * object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

  if (mpeg2dec->decoder) {
    GST_DEBUG_OBJECT (mpeg2dec, "closing decoder");
    mpeg2_close (mpeg2dec->decoder);
    mpeg2dec->decoder = NULL;
  }
  clear_buffers (mpeg2dec);
  g_free (mpeg2dec->dummybuf[3]);
  mpeg2dec->dummybuf[3] = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mpeg2dec_reset (GstMpeg2dec * mpeg2dec)
{
  /* reset the initial video state */
  gst_video_info_init (&mpeg2dec->vinfo);
  gst_segment_init (&mpeg2dec->segment, GST_FORMAT_UNDEFINED);
  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
  mpeg2dec->frame_period = 0;
  mpeg2dec->need_sequence = TRUE;
  mpeg2dec->next_time = -1;
  mpeg2dec->offset = 0;
  mpeg2dec->error_count = 0;
  mpeg2dec->can_allocate_aligned = TRUE;
  mpeg2_reset (mpeg2dec->decoder, 1);
}

static void
gst_mpeg2dec_qos_reset (GstMpeg2dec * mpeg2dec)
{
  GST_OBJECT_LOCK (mpeg2dec);
  mpeg2dec->proportion = 1.0;
  mpeg2dec->earliest_time = -1;
  mpeg2dec->dropped = 0;
  mpeg2dec->processed = 0;
  GST_OBJECT_UNLOCK (mpeg2dec);
}

static GstFlowReturn
gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstBuffer ** buf,
    GstVideoFrame * frame)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstVideoFrame outframe;
  guint i, n_planes;

  ret = gst_buffer_pool_acquire_buffer (dec->pool, &outbuf, NULL);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    return ret;;

  GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, dec,
      "cropping input buffer to output buffer");

  gst_video_frame_map (&outframe, &dec->cinfo, outbuf, GST_MAP_WRITE);

  n_planes = GST_VIDEO_FRAME_N_PLANES (&outframe);

  for (i = 0; i < n_planes; i++) {
    guint w, h, j;
    guint8 *sp, *dp;
    gint ss, ds;

    sp = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    dp = GST_VIDEO_FRAME_PLANE_DATA (&outframe, i);

    ss = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);
    ds = GST_VIDEO_FRAME_PLANE_STRIDE (&outframe, i);

    w = MIN (ABS (ss), ABS (ds));
    h = GST_VIDEO_FRAME_COMP_HEIGHT (&outframe, i);

    GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy plane %u, w:%u h:%u ", i, w, h);

    for (j = 0; j < h; j++) {
      memcpy (dp, sp, w);
      dp += ds;
      sp += ss;
    }
  }
  gst_video_frame_unmap (&outframe);

  gst_buffer_copy_into (outbuf, *buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS, 0, -1);

  gst_buffer_unref (*buf);
  *buf = outbuf;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mpeg2dec_negotiate_pool (GstMpeg2dec * dec, GstCaps * caps,
    GstVideoInfo * vinfo, GstVideoInfo * cinfo)
{
  GstQuery *query;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  GstCaps *pcaps;

  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (caps, TRUE);

  if (gst_pad_peer_query (dec->srcpad, query)) {
    /* check if downstream supports cropping */
    dec->has_cropping =
        gst_query_has_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE);
  } else {
    /* use the query default then */
    GST_DEBUG_OBJECT (dec, "didn't get downstream ALLOCATION hints");
    dec->has_cropping = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  } else {
    pool = NULL;
    size = 0;
    min = max = 0;
  }

  GST_DEBUG_OBJECT (dec,
      "size:%d, min:%d, max:%d,pool:%p", size, min, max, pool);
  GST_DEBUG_OBJECT (dec, "downstream cropping %d", dec->has_cropping);

  if (pool == NULL) {
    /* we did not get a pool, make one ourselves then */
    pool = gst_video_buffer_pool_new ();
  }

  if (dec->pool) {
    gst_buffer_pool_set_active (dec->pool, FALSE);
    gst_object_unref (dec->pool);
  }
  dec->pool = pool;

  if (dec->need_cropping && dec->has_cropping) {
    /* we can crop, configure the pool with buffers of caps and size of the
     * decoded picture size and then crop them with metadata */
    pcaps = gst_video_info_to_caps (vinfo);
    size = MAX (size, GST_VIDEO_INFO_SIZE (vinfo));
  } else {
    /* no cropping, use cropped videoinfo */
    pcaps = gst_caps_ref (caps);
    size = MAX (size, GST_VIDEO_INFO_SIZE (cinfo));
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, pcaps, size, min, max);
  gst_caps_unref (pcaps);

  if (gst_query_has_allocation_meta (query, GST_VIDEO_META_API_TYPE)) {
    /* just set the option, if the pool can support it we will transparently use
     * it through the video info API. We could also see if the pool support this
     * option and only activate it then. */
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);
  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

  gst_query_unref (query);

  return GST_FLOW_OK;
}

static void
init_dummybuf (GstMpeg2dec * mpeg2dec)
{
  g_free (mpeg2dec->dummybuf[3]);

  /* libmpeg2 needs 16 byte aligned buffers... care for this here */
  mpeg2dec->dummybuf[3] = g_malloc0 (mpeg2dec->size + 15);
  mpeg2dec->dummybuf[0] = ALIGN_16 (mpeg2dec->dummybuf[3]);
  mpeg2dec->dummybuf[1] = mpeg2dec->dummybuf[0] + mpeg2dec->u_offs;
  mpeg2dec->dummybuf[2] = mpeg2dec->dummybuf[0] + mpeg2dec->v_offs;
}

static GstFlowReturn
handle_sequence (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const mpeg2_sequence_t *sequence;
  gint par_n, par_d;
  GstVideoInfo vinfo, cinfo;
  GstVideoFormat format;
  GstCaps *caps;
  gint y_size, uv_size;

  sequence = info->sequence;

  if (sequence->frame_period == 0)
    goto invalid_frame_period;

  /* mpeg2 video can only be from 16x16 to 4096x4096. Everything
   * else is a corrupted file */
  if (sequence->width > 4096 || sequence->width < 16 ||
      sequence->height > 4096 || sequence->height < 16)
    goto invalid_size;

  GST_DEBUG_OBJECT (mpeg2dec,
      "widthxheight: %dx%d , decoded_widthxheight: %dx%d",
      sequence->picture_width, sequence->picture_height, sequence->width,
      sequence->height);

  if (sequence->picture_width != sequence->width ||
      sequence->picture_height != sequence->height) {
    GST_DEBUG_OBJECT (mpeg2dec, "we need to crop");
    mpeg2dec->need_cropping = TRUE;
  } else {
    GST_DEBUG_OBJECT (mpeg2dec, "no cropping needed");
    mpeg2dec->need_cropping = FALSE;
  }

  y_size = sequence->width * sequence->height;
  /* get subsampling */
  if (sequence->chroma_width < sequence->width) {
    /* horizontally subsampled */
    if (sequence->chroma_height < sequence->height) {
      /* and vertically subsamples */
      format = GST_VIDEO_FORMAT_I420;
      uv_size = y_size >> 2;
    } else {
      format = GST_VIDEO_FORMAT_Y42B;
      uv_size = y_size >> 1;
    }
  } else {
    /* not subsampled */
    format = GST_VIDEO_FORMAT_Y444;
    uv_size = y_size;
  }

  /* calculate size and offsets of the decoded frames */
  mpeg2dec->size = y_size + 2 * (uv_size);
  mpeg2dec->u_offs = y_size;
  mpeg2dec->v_offs = y_size + uv_size;

  /* we store the codec size before cropping */
  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, format, sequence->width, sequence->height);

  /* sink caps par overrides sequence PAR */
  if (mpeg2dec->have_par) {
    par_n = mpeg2dec->in_par_n;
    par_d = mpeg2dec->in_par_d;
    GST_DEBUG_OBJECT (mpeg2dec, "using sink par %d:%d", par_n, par_d);
  } else {
    par_n = sequence->pixel_width;
    par_d = sequence->pixel_height;
    GST_DEBUG_OBJECT (mpeg2dec, "using encoded par %d:%d", par_n, par_d);
  }

  if (par_n == 0 || par_d == 0) {
    if (!gst_util_fraction_multiply (4, 3, sequence->picture_height,
            sequence->picture_width, &par_n, &par_d))
      par_n = par_d = 1;

    GST_WARNING_OBJECT (mpeg2dec, "Unknown par, assuming %d:%d", par_n, par_d);
  }
  vinfo.par_n = par_n;
  vinfo.par_d = par_d;

  /* set framerate */
  vinfo.fps_n = 27000000;
  vinfo.fps_d = sequence->frame_period;
  mpeg2dec->frame_period = sequence->frame_period * GST_USECOND / 27;

  if (!(sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE))
    vinfo.interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
  else
    vinfo.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  vinfo.chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;
  vinfo.colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;

  if (sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION) {
    /* do color description */
    switch (sequence->colour_primaries) {
      case 1:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        break;
      case 4:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
        break;
      case 5:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
        break;
      case 6:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        break;
      case 7:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 8-255 reseved */
      default:
        vinfo.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;
        break;
    }
    /* matrix coefficients */
    switch (sequence->matrix_coefficients) {
      case 1:
        vinfo.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
      case 4:
        vinfo.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_FCC;
        break;
      case 5:
      case 6:
        vinfo.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
      case 7:
        vinfo.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 8-255 reseved */
      default:
        vinfo.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
        break;
    }
    /* transfer characteristics */
    switch (sequence->transfer_characteristics) {
      case 1:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case 4:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA22;
        break;
      case 5:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA28;
        break;
      case 6:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case 7:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE240M;
        break;
      case 8:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 9-255 reseved */
      default:
        vinfo.colorimetry.transfer = GST_VIDEO_TRANSFER_UNKNOWN;
        break;
    }
  }

  GST_DEBUG_OBJECT (mpeg2dec,
      "sequence flags: %d, frame period: %d (%g), frame rate: %d/%d",
      sequence->flags, sequence->frame_period,
      (double) (mpeg2dec->frame_period) / GST_SECOND, vinfo.fps_n, vinfo.fps_d);
  GST_DEBUG_OBJECT (mpeg2dec, "profile: %02x, colour_primaries: %d",
      sequence->profile_level_id, sequence->colour_primaries);
  GST_DEBUG_OBJECT (mpeg2dec, "transfer chars: %d, matrix coef: %d",
      sequence->transfer_characteristics, sequence->matrix_coefficients);
  GST_DEBUG_OBJECT (mpeg2dec,
      "FLAGS: CONSTRAINED_PARAMETERS:%d, PROGRESSIVE_SEQUENCE:%d",
      sequence->flags & SEQ_FLAG_CONSTRAINED_PARAMETERS,
      sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);
  GST_DEBUG_OBJECT (mpeg2dec, "FLAGS: LOW_DELAY:%d, COLOUR_DESCRIPTION:%d",
      sequence->flags & SEQ_FLAG_LOW_DELAY,
      sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION);

  /* for the output caps we always take the cropped dimensions */
  cinfo = vinfo;
  gst_video_info_set_format (&cinfo, GST_VIDEO_INFO_FORMAT (&vinfo),
      sequence->picture_width, sequence->picture_height);
  caps = gst_video_info_to_caps (&cinfo);
  gst_pad_set_caps (mpeg2dec->srcpad, caps);

  gst_mpeg2dec_negotiate_pool (mpeg2dec, caps, &vinfo, &cinfo);
  gst_caps_unref (caps);

  mpeg2dec->vinfo = vinfo;
  mpeg2dec->cinfo = cinfo;

  mpeg2_custom_fbuf (mpeg2dec->decoder, 1);
  init_dummybuf (mpeg2dec);

  /* Pump in some null buffers, because otherwise libmpeg2 doesn't
   * initialise the discard_fbuf->id */
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);

  mpeg2dec->need_sequence = FALSE;

done:
  return ret;

invalid_frame_period:
  {
    GST_WARNING_OBJECT (mpeg2dec, "Frame period is 0!");
    ret = GST_FLOW_ERROR;
    goto done;
  }
invalid_size:
  {
    GST_ERROR_OBJECT (mpeg2dec, "Invalid frame dimensions: %d x %d",
        sequence->width, sequence->height);
    return GST_FLOW_ERROR;
  }
}

static void
clear_buffers (GstMpeg2dec * mpeg2dec)
{
  gint i;
  GstVideoFrame *frame;

  for (i = 0; i < 4; i++) {
    frame = &mpeg2dec->ip_frame[i];
    if (frame->buffer) {
      gst_video_frame_unmap (frame);
      gst_buffer_unref (frame->buffer);
      frame->buffer = NULL;
    }
  }
  frame = &mpeg2dec->b_frame;
  if (frame->buffer) {
    gst_video_frame_unmap (frame);
    gst_buffer_unref (frame->buffer);
    frame->buffer = NULL;
  }
}

static void
clear_queued (GstMpeg2dec * mpeg2dec)
{
  g_list_foreach (mpeg2dec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mpeg2dec->queued);
  mpeg2dec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstMpeg2dec * mpeg2dec)
{
  GstFlowReturn res = GST_FLOW_OK;

  while (mpeg2dec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (mpeg2dec->queued->data);

    GST_LOG_OBJECT (mpeg2dec, "pushing buffer %p, timestamp %"
        GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* iterate ouput queue an push downstream */
    res = gst_pad_push (mpeg2dec->srcpad, buf);

    mpeg2dec->queued = g_list_delete_link (mpeg2dec->queued, mpeg2dec->queued);
  }
  return res;
}

static GstFlowReturn
handle_picture (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  gboolean key_frame = FALSE;
  GstBuffer *outbuf;
  GstVideoFrame *frame;
  GstFlowReturn ret;
  gint type;
  guint8 *buf[3];

  if (mpeg2dec->need_cropping && !mpeg2dec->has_cropping) {
    GstAllocationParams params = { 0, };

    /* we need to crop manually */
    params.align = 15;
    outbuf =
        gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&mpeg2dec->vinfo),
        &params);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_buffer_pool_acquire_buffer (mpeg2dec->pool, &outbuf, NULL);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto no_buffer;
  }

  /* we store the original byteoffset of this picture in the stream here
   * because we need it for indexing */
  GST_BUFFER_OFFSET (outbuf) = mpeg2dec->offset;

  if (info->current_picture) {
    type = info->current_picture->flags & PIC_MASK_CODING_TYPE;
  } else {
    type = 0;
  }

  GST_DEBUG_OBJECT (mpeg2dec, "handle picture type %d", type);

  key_frame = type == PIC_FLAG_CODING_TYPE_I;

  switch (type) {
    case PIC_FLAG_CODING_TYPE_I:
      mpeg2_skip (mpeg2dec->decoder, 0);
      if (mpeg2dec->segment.rate < 0.0) {
        /* negative rate, flush the queued pictures in reverse */
        GST_DEBUG_OBJECT (mpeg2dec, "flushing queued buffers");
        flush_queued (mpeg2dec);
      }
      /* fallthrough */
    case PIC_FLAG_CODING_TYPE_P:
      frame = &mpeg2dec->ip_frame[mpeg2dec->ip_framepos];
      GST_DEBUG_OBJECT (mpeg2dec, "I/P unref %p, ref %p", frame, outbuf);
      mpeg2dec->ip_framepos = (mpeg2dec->ip_framepos + 1) & 3;
      break;
    case PIC_FLAG_CODING_TYPE_B:
      frame = &mpeg2dec->b_frame;
      GST_DEBUG_OBJECT (mpeg2dec, "B unref %p, ref %p", frame, outbuf);
      break;
    default:
      goto unknown_frame;
  }

  if (frame->buffer) {
    gst_video_frame_unmap (frame);
    gst_buffer_unref (frame->buffer);
    frame->buffer = NULL;
  }

  if (mpeg2dec->need_cropping && mpeg2dec->has_cropping) {
    GstVideoCropMeta *crop;

    crop = gst_buffer_add_video_crop_meta (outbuf);
    /* we can do things slightly more efficient when we know that
     * downstream understands clipping */
    crop->x = 0;
    crop->y = 0;
    crop->width = info->sequence->picture_width;
    crop->height = info->sequence->picture_height;
  }

  gst_video_frame_map (frame, &mpeg2dec->vinfo, outbuf, GST_MAP_WRITE);

  buf[0] = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  buf[1] = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
  buf[2] = GST_VIDEO_FRAME_PLANE_DATA (frame, 2);

  GST_DEBUG_OBJECT (mpeg2dec, "set_buf: %p %p %p, outbuf %p",
      buf[0], buf[1], buf[2], outbuf);

  mpeg2_set_buf (mpeg2dec->decoder, buf, frame);

  GST_DEBUG_OBJECT (mpeg2dec, "picture %s, outbuf %p, offset %"
      G_GINT64_FORMAT,
      key_frame ? ", kf," : "    ", outbuf, GST_BUFFER_OFFSET (outbuf)
      );

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame)
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;

  return ret;

no_buffer:
  {
    return ret;
  }
unknown_frame:
  {
    return ret;
  }
}

/* try to clip the buffer to the segment boundaries */
static gboolean
clip_buffer (GstMpeg2dec * dec, GstBuffer * buf)
{
  gboolean res = TRUE;
  GstClockTime in_ts, in_dur, stop;
  guint64 cstart, cstop;

  in_ts = GST_BUFFER_TIMESTAMP (buf);
  in_dur = GST_BUFFER_DURATION (buf);

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT " , duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur));

  /* can't clip without TIME segment */
  if (dec->segment.format != GST_FORMAT_TIME)
    goto beach;

  /* we need a start time */
  if (!GST_CLOCK_TIME_IS_VALID (in_ts))
    goto beach;

  /* generate valid stop, if duration unknown, we have unknown stop */
  stop =
      GST_CLOCK_TIME_IS_VALID (in_dur) ? (in_ts + in_dur) : GST_CLOCK_TIME_NONE;

  /* now clip */
  if (!(res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
              in_ts, stop, &cstart, &cstop)))
    goto beach;

  /* update timestamp and possibly duration if the clipped stop time is
   * valid */
  GST_BUFFER_TIMESTAMP (buf) = cstart;
  if (GST_CLOCK_TIME_IS_VALID (cstop))
    GST_BUFFER_DURATION (buf) = cstop - cstart;

beach:
  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;
}

static GstFlowReturn
handle_slice (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  const mpeg2_picture_t *picture;
  gboolean key_frame = FALSE;
  GstClockTime time;
  GstVideoFrame *frame;

  GST_DEBUG_OBJECT (mpeg2dec, "picture slice/end %p %p %p %p",
      info->display_fbuf,
      info->display_picture, info->current_picture,
      (info->display_fbuf ? info->display_fbuf->id : NULL));

  if (!info->display_fbuf || !info->display_fbuf->id)
    goto no_display;

  frame = (GstVideoFrame *) (info->display_fbuf->id);
  outbuf = frame->buffer;

  picture = info->display_picture;

  key_frame = (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;

  GST_DEBUG_OBJECT (mpeg2dec, "picture flags: %d, type: %d, keyframe: %d",
      picture->flags, picture->flags & PIC_MASK_CODING_TYPE, key_frame);

  if (key_frame) {
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    mpeg2_skip (mpeg2dec->decoder, 0);
  } else {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_KEYFRAME && key_frame)
    mpeg2dec->discont_state = MPEG2DEC_DISC_NONE;

  time = GST_CLOCK_TIME_NONE;

#if MPEG2_RELEASE < MPEG2_VERSION(0,4,0)
  if (picture->flags & PIC_FLAG_PTS) {
    time = MPEG_TIME_TO_GST_TIME (picture->pts);
    GST_DEBUG_OBJECT (mpeg2dec, "picture pts %" G_GUINT64_FORMAT
        ", time %" GST_TIME_FORMAT, picture->pts, GST_TIME_ARGS (time));
  }
#else
  if (picture->flags & PIC_FLAG_TAGS) {
    guint64 pts = (((guint64) picture->tag2) << 32) | picture->tag;

    time = MPEG_TIME_TO_GST_TIME (pts);
    GST_DEBUG_OBJECT (mpeg2dec, "picture tags %" G_GUINT64_FORMAT
        ", time %" GST_TIME_FORMAT, pts, GST_TIME_ARGS (time));
  }
#endif

  if (time == GST_CLOCK_TIME_NONE) {
    time = mpeg2dec->next_time;
    GST_DEBUG_OBJECT (mpeg2dec, "picture didn't have pts");
  } else {
    GST_DEBUG_OBJECT (mpeg2dec,
        "picture had pts %" GST_TIME_FORMAT ", we had %"
        GST_TIME_FORMAT, GST_TIME_ARGS (time),
        GST_TIME_ARGS (mpeg2dec->next_time));
    mpeg2dec->next_time = time;
  }
  GST_BUFFER_TIMESTAMP (outbuf) = time;

  /* TODO set correct offset here based on frame number */
  if (info->display_picture_2nd) {
    GST_BUFFER_DURATION (outbuf) = (picture->nb_fields +
        info->display_picture_2nd->nb_fields) * mpeg2dec->frame_period / 2;
  } else {
    GST_BUFFER_DURATION (outbuf) =
        picture->nb_fields * mpeg2dec->frame_period / 2;
  }
  mpeg2dec->next_time += GST_BUFFER_DURATION (outbuf);

  if (picture->flags & PIC_FLAG_TOP_FIELD_FIRST)
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);

#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
  /* repeat field introduced in 0.5.0 */
  if (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD)
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_RFF);
#endif

  GST_DEBUG_OBJECT (mpeg2dec,
      "picture: %s %s %s %s %s fields:%d off:%" G_GINT64_FORMAT " ts:%"
      GST_TIME_FORMAT,
      (picture->flags & PIC_FLAG_PROGRESSIVE_FRAME ? "prog" : "    "),
      (picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? "tff" : "   "),
#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
      (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD ? "rff" : "   "),
#else
      "unknown rff",
#endif
      (picture->flags & PIC_FLAG_SKIP ? "skip" : "    "),
      (picture->flags & PIC_FLAG_COMPOSITE_DISPLAY ? "composite" : "         "),
      picture->nb_fields, GST_BUFFER_OFFSET (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

  if (picture->flags & PIC_FLAG_SKIP)
    goto skip;

  if (mpeg2dec->discont_state != MPEG2DEC_DISC_NONE)
    goto drop;

  /* check for clipping */
  if (!clip_buffer (mpeg2dec, outbuf))
    goto clipped;

  if (GST_CLOCK_TIME_IS_VALID (time)) {
    gboolean need_skip;
    GstClockTime qostime;

    /* qos needs to be done on running time */
    qostime = gst_segment_to_running_time (&mpeg2dec->segment, GST_FORMAT_TIME,
        time);

    GST_OBJECT_LOCK (mpeg2dec);
    /* check for QoS, don't perform the last steps of getting and
     * pushing the buffers that are known to be late. */
    /* FIXME, we can also entirely skip decoding if the next valid buffer is
     * known to be after a keyframe (using the granule_shift) */
    need_skip = mpeg2dec->earliest_time != -1
        && qostime <= mpeg2dec->earliest_time;
    GST_OBJECT_UNLOCK (mpeg2dec);

    if (need_skip) {
      GstMessage *qos_msg;
      guint64 stream_time;
      gint64 jitter;

      mpeg2dec->dropped++;

      stream_time =
          gst_segment_to_stream_time (&mpeg2dec->segment, GST_FORMAT_TIME,
          time);
      jitter = GST_CLOCK_DIFF (qostime, mpeg2dec->earliest_time);

      qos_msg =
          gst_message_new_qos (GST_OBJECT_CAST (mpeg2dec), FALSE, qostime,
          stream_time, time, GST_BUFFER_DURATION (outbuf));
      gst_message_set_qos_values (qos_msg, jitter, mpeg2dec->proportion,
          1000000);
      gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
          mpeg2dec->processed, mpeg2dec->dropped);
      gst_element_post_message (GST_ELEMENT_CAST (mpeg2dec), qos_msg);

      goto dropping_qos;
    }
  }

  mpeg2dec->processed++;

  /* ref before pushing it out, so we still have the ref in our
   * array of buffers */
  gst_buffer_ref (outbuf);

  /* do cropping if the target region is smaller than the input one */
  if (mpeg2dec->need_cropping && !mpeg2dec->has_cropping) {
    GST_DEBUG_OBJECT (mpeg2dec, "cropping buffer");
    ret = gst_mpeg2dec_crop_buffer (mpeg2dec, &outbuf, frame);
    if (ret != GST_FLOW_OK)
      goto done;
  }

  if (mpeg2dec->segment.rate >= 0.0) {
    /* forward: push right away */
    GST_LOG_OBJECT (mpeg2dec, "pushing buffer %p, timestamp %"
        GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
        outbuf,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
    GST_LOG_OBJECT (mpeg2dec, "... with flags %x", GST_BUFFER_FLAGS (outbuf));

    ret = gst_pad_push (mpeg2dec->srcpad, outbuf);
    GST_DEBUG_OBJECT (mpeg2dec, "pushed with result %s",
        gst_flow_get_name (ret));
  } else {
    /* reverse: queue, we'll push in reverse when we receive the next (previous)
     * keyframe. */
    GST_DEBUG_OBJECT (mpeg2dec, "queued frame");
    mpeg2dec->queued = g_list_prepend (mpeg2dec->queued, outbuf);
    ret = GST_FLOW_OK;
  }
done:
  return ret;

  /* special cases */
no_display:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "no picture to display");
    return GST_FLOW_OK;
  }
skip:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer because of skip flag");
    return GST_FLOW_OK;
  }
drop:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, discont state %d",
        mpeg2dec->discont_state);
    return GST_FLOW_OK;
  }
clipped:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, clipped");
    return GST_FLOW_OK;
  }
dropping_qos:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer because of QoS");
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_mpeg2dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstMpeg2dec *mpeg2dec;
  GstMapInfo map;
  GstClockTime pts;
  const mpeg2_info_t *info;
  mpeg2_state_t state;
  gboolean done = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  mpeg2dec = GST_MPEG2DEC (parent);

  gst_buffer_map (buf, &map, GST_MAP_READ);
  pts = GST_BUFFER_TIMESTAMP (buf);

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (mpeg2dec, "DISCONT, reset decoder");
    /* when we receive a discont, reset our state as to not create too much
     * distortion in the picture due to missing packets */
    mpeg2_reset (mpeg2dec->decoder, 0);
    mpeg2_skip (mpeg2dec->decoder, 1);
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
  }

  GST_LOG_OBJECT (mpeg2dec, "received buffer, timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  info = mpeg2dec->info;

  mpeg2dec->offset = GST_BUFFER_OFFSET (buf);

  if (pts != GST_CLOCK_TIME_NONE) {
    gint64 mpeg_pts = GST_TIME_TO_MPEG_TIME (pts);

    GST_DEBUG_OBJECT (mpeg2dec,
        "have pts: %" G_GINT64_FORMAT " (%" GST_TIME_FORMAT ")",
        mpeg_pts, GST_TIME_ARGS (MPEG_TIME_TO_GST_TIME (mpeg_pts)));

#if MPEG2_RELEASE >= MPEG2_VERSION(0,4,0)
    mpeg2_tag_picture (mpeg2dec->decoder, mpeg_pts & 0xffffffff,
        mpeg_pts >> 32);
#else
    mpeg2_pts (mpeg2dec->decoder, mpeg_pts);
#endif
  } else {
    GST_LOG ("no pts");
  }

  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer");
  mpeg2_buffer (mpeg2dec->decoder, map.data, map.data + map.size);
  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer done");

  while (!done) {
    GST_LOG_OBJECT (mpeg2dec, "calling parse");
    state = mpeg2_parse (mpeg2dec->decoder);
    GST_DEBUG_OBJECT (mpeg2dec, "parse state %d", state);

    switch (state) {
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 5, 0)
      case STATE_SEQUENCE_MODIFIED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence modified");
        /* fall through */
#endif
      case STATE_SEQUENCE:
        ret = handle_sequence (mpeg2dec, info);
        /* if there is an error handling the sequence
         * reset the decoder, maybe something more elegant
         * could be done.
         */
        if (ret == GST_FLOW_ERROR) {
          mpeg2dec->error_count++;
          GST_WARNING_OBJECT (mpeg2dec, "Decoding error #%d",
              mpeg2dec->error_count);
          if (mpeg2dec->error_count >= WARN_THRESHOLD && WARN_THRESHOLD > 0) {
            GST_ELEMENT_WARNING (mpeg2dec, STREAM, DECODE,
                ("%d consecutive decoding errors", mpeg2dec->error_count),
                (NULL));
          }
          mpeg2_reset (mpeg2dec->decoder, 0);
          mpeg2_skip (mpeg2dec->decoder, 1);
          mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;

          goto exit;
        }
        break;
      case STATE_SEQUENCE_REPEATED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence repeated");
        break;
      case STATE_GOP:
        GST_DEBUG_OBJECT (mpeg2dec, "gop");
        break;
      case STATE_PICTURE:
        ret = handle_picture (mpeg2dec, info);
        break;
      case STATE_SLICE_1ST:
        GST_LOG_OBJECT (mpeg2dec, "1st slice of frame encountered");
        break;
      case STATE_PICTURE_2ND:
        GST_LOG_OBJECT (mpeg2dec,
            "Second picture header encountered. Decoding 2nd field");
        break;
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 4, 0)
      case STATE_INVALID_END:
        GST_DEBUG_OBJECT (mpeg2dec, "invalid end");
#endif
      case STATE_END:
        GST_DEBUG_OBJECT (mpeg2dec, "end");
        mpeg2dec->need_sequence = TRUE;
      case STATE_SLICE:
        ret = handle_slice (mpeg2dec, info);
        break;
      case STATE_BUFFER:
        done = TRUE;
        break;
        /* error */
      case STATE_INVALID:
        /* FIXME: at some point we should probably send newsegment events to
         * let downstream know that parts of the stream are missing */
        mpeg2dec->error_count++;
        GST_WARNING_OBJECT (mpeg2dec, "Decoding error #%d",
            mpeg2dec->error_count);
        if (mpeg2dec->error_count >= WARN_THRESHOLD && WARN_THRESHOLD > 0) {
          GST_ELEMENT_WARNING (mpeg2dec, STREAM, DECODE,
              ("%d consecutive decoding errors", mpeg2dec->error_count),
              (NULL));
        }
        continue;
      default:
        GST_ERROR_OBJECT (mpeg2dec, "Unknown libmpeg2 state %d, FIXME", state);
        goto exit;
    }

    mpeg2dec->error_count = 0;

    /*
     * FIXME: should pass more information such as state the user data is from
     */
#ifdef enable_user_data
    if (info->user_data_len > 0) {
      GstBuffer *udbuf =
          gst_buffer_new_allocate (NULL, info->user_data_len, NULL);

      gst_buffer_fill (udbuf, 0, info->user_data, info->user_data_len);

      gst_pad_push (mpeg2dec->userdatapad, udbuf);
    }
#endif

    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (mpeg2dec, "exit loop, reason %s",
          gst_flow_get_name (ret));
      break;
    }
  }
done:
  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);
  return ret;

  /* errors */
exit:
  {
    ret = GST_FLOW_OK;
    goto done;
  }
}

static gboolean
gst_mpeg2dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMpeg2dec *mpeg2dec;
  gboolean ret = TRUE;

  mpeg2dec = GST_MPEG2DEC (parent);

  GST_DEBUG_OBJECT (mpeg2dec, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_mpeg2dec_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      /* we need TIME */
      if (seg.format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      /* now configure the values */
      mpeg2dec->segment = seg;

      GST_DEBUG_OBJECT (mpeg2dec, "Pushing seg %" GST_SEGMENT_FORMAT, &seg);

      ret = gst_pad_push_event (mpeg2dec->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (mpeg2dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
      mpeg2dec->next_time = -1;;
      gst_mpeg2dec_qos_reset (mpeg2dec);
      mpeg2_reset (mpeg2dec->decoder, 0);
      mpeg2_skip (mpeg2dec->decoder, 1);
      clear_queued (mpeg2dec);
      ret = gst_pad_push_event (mpeg2dec->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
      ret = gst_pad_push_event (mpeg2dec->srcpad, event);
      break;
    default:
      ret = gst_pad_push_event (mpeg2dec->srcpad, event);
      break;
  }

done:

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "received non TIME newsegment");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_mpeg2dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMpeg2dec *mpeg2dec;
  GstStructure *s;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  s = gst_caps_get_structure (caps, 0);

  /* parse the par, this overrides the encoded par */
  mpeg2dec->have_par = gst_structure_get_fraction (s, "pixel-aspect-ratio",
      &mpeg2dec->in_par_n, &mpeg2dec->in_par_d);

  gst_object_unref (mpeg2dec);

  return TRUE;
}

static gboolean
gst_mpeg2dec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (parent);

  if (mpeg2dec->decoder == NULL)
    goto no_decoder;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      GstQOSType type;
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (mpeg2dec);
      mpeg2dec->proportion = proportion;
      mpeg2dec->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (mpeg2dec);

      GST_DEBUG_OBJECT (mpeg2dec,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT,
          GST_TIME_ARGS (timestamp), diff);

      res = gst_pad_push_event (mpeg2dec->sinkpad, event);
      break;
    }
    case GST_EVENT_SEEK:
    case GST_EVENT_NAVIGATION:
      /* Forward unchanged */
    default:
      res = gst_pad_push_event (mpeg2dec->sinkpad, event);
      break;
  }
  return res;

no_decoder:
  {
    GST_DEBUG_OBJECT (mpeg2dec, "no decoder, cannot handle event");
    gst_event_unref (event);
    return FALSE;
  }
}

static GstStateChangeReturn
gst_mpeg2dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      mpeg2_accel (MPEG2_ACCEL_DETECT);
      if ((mpeg2dec->decoder = mpeg2_init ()) == NULL)
        goto init_failed;
      mpeg2dec->info = mpeg2_info (mpeg2dec->decoder);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_mpeg2dec_reset (mpeg2dec);
      gst_mpeg2dec_qos_reset (mpeg2dec);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mpeg2dec_qos_reset (mpeg2dec);
      clear_queued (mpeg2dec);
      if (mpeg2dec->pool) {
        gst_buffer_pool_set_active (mpeg2dec->pool, FALSE);
        gst_object_unref (mpeg2dec->pool);
        mpeg2dec->pool = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (mpeg2dec->decoder) {
        mpeg2_close (mpeg2dec->decoder);
        mpeg2dec->decoder = NULL;
        mpeg2dec->info = NULL;
      }
      clear_buffers (mpeg2dec);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (mpeg2dec, LIBRARY, INIT,
        (NULL), ("Failed to initialize libmpeg2 library"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "mpeg2dec", GST_RANK_PRIMARY,
          GST_TYPE_MPEG2DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpeg2dec,
    "LibMpeg2 decoder", plugin_init, VERSION, "GPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
