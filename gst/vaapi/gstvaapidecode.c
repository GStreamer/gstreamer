/*
 *  gstvaapidecode.c - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

/**
 * SECTION:gstvaapidecode
 * @short_description: A VA-API based video decoder
 *
 * vaapidecode decodes from raw bitstreams to surfaces suitable for
 * the vaapisink element.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/video/videocontext.h>

#include "gstvaapidecode.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"

#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidecode);
#define GST_CAT_DEFAULT gst_debug_vaapidecode

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapidecode_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=2, systemstream=(boolean)false")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
    GST_CAPS_CODEC("video/x-wmv")
    GST_CAPS_CODEC("image/jpeg")
    ;

static const char gst_vaapidecode_src_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapidecode_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_sink_caps_str));

static GstStaticPadTemplate gst_vaapidecode_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_src_caps_str));

/* GstImplementsInterface interface */
static gboolean
gst_vaapidecode_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapidecode_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapidecode_implements_interface_supported;
}

/* GstVideoContext interface */
static void
gst_vaapidecode_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
    GstVaapiDecode *decode = GST_VAAPIDECODE (context);
    gst_vaapi_set_display (type, value, &decode->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidecode_set_video_context;
}

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiDecode,
    gst_vaapidecode,
    GST_TYPE_VIDEO_DECODER,
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapidecode_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode,
    GstVideoCodecState *ref_state);

static void
gst_vaapi_decoder_notify_caps(GObject *obj, GParamSpec *pspec, void *user_data)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(user_data);
    GstVideoCodecState *codec_state;

    g_assert(decode->decoder == GST_VAAPI_DECODER(obj));

    codec_state = gst_vaapi_decoder_get_codec_state(decode->decoder);
    gst_vaapidecode_update_src_caps(decode, codec_state);
    gst_video_codec_state_unref(codec_state);
}

static inline gboolean
gst_vaapidecode_update_sink_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    gst_caps_replace(&decode->sinkpad_caps, caps);
    return TRUE;
}

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode,
    GstVideoCodecState *ref_state)
{
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);
    GstVideoCodecState *state;
    GstVideoInfo *vi;

    state = gst_video_decoder_set_output_state(vdec,
        GST_VIDEO_INFO_FORMAT(&ref_state->info),
        ref_state->info.width, ref_state->info.height, ref_state);
    if (!state)
        return FALSE;

    vi = &state->info;
    gst_video_codec_state_unref(state);

    /* XXX: gst_video_info_to_caps() from GStreamer 0.10 does not
       reconstruct suitable caps for "encoded" video formats */
    state->caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS_NAME);
    if (!state->caps)
        return FALSE;

    gst_caps_set_simple(state->caps,
        "type", G_TYPE_STRING, "vaapi",
        "opengl", G_TYPE_BOOLEAN, USE_GLX,
        "width", G_TYPE_INT, vi->width,
        "height", G_TYPE_INT, vi->height,
        "framerate", GST_TYPE_FRACTION, vi->fps_n, vi->fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, vi->par_n, vi->par_d,
        NULL);

    if (GST_VIDEO_INFO_IS_INTERLACED(vi))
        gst_caps_set_simple(state->caps, "interlaced", G_TYPE_BOOLEAN,
            TRUE, NULL);

    gst_caps_replace(&decode->srcpad_caps, state->caps);
    return TRUE;
}

static void
gst_vaapidecode_release(GstVaapiDecode *decode)
{
    g_mutex_lock(&decode->decoder_mutex);
    g_cond_signal(&decode->decoder_ready);
    g_mutex_unlock(&decode->decoder_mutex);
}

static GstFlowReturn
gst_vaapidecode_decode_frame(GstVideoDecoder *vdec, GstVideoCodecFrame *frame)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiDecoderStatus status;
    GstFlowReturn ret;
    gint64 end_time;

    if (!decode->render_time_base)
        decode->render_time_base = g_get_monotonic_time();
    end_time = decode->render_time_base;
    end_time += GST_TIME_AS_USECONDS(decode->last_buffer_time);
    end_time += G_TIME_SPAN_SECOND;

    /* Decode current frame */
    for (;;) {
        status = gst_vaapi_decoder_decode(decode->decoder, frame);
        if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) {
            gboolean was_signalled;
            g_mutex_lock(&decode->decoder_mutex);
            was_signalled = g_cond_wait_until(
                &decode->decoder_ready,
                &decode->decoder_mutex,
                end_time
            );
            g_mutex_unlock(&decode->decoder_mutex);
            if (was_signalled)
                continue;
            goto error_decode_timeout;
        }
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto error_decode;
        break;
    }
    return GST_FLOW_OK;

    /* ERRORS */
error_decode_timeout:
    {
        GST_WARNING("decode timeout. Decoder required a VA surface but none "
                    "got available within one second");
        return GST_FLOW_UNEXPECTED;
    }
error_decode:
    {
        GST_ERROR("decode error %d", status);
        switch (status) {
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
            ret = GST_FLOW_NOT_SUPPORTED;
            break;
        default:
            ret = GST_FLOW_UNEXPECTED;
            break;
        }
        gst_video_decoder_drop_frame(vdec, frame);
        return ret;
    }
}

static GstFlowReturn
gst_vaapidecode_push_decoded_frames(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;
    GstVideoCodecFrame *out_frame;
    GstFlowReturn ret;

    /* Output all decoded frames */
    for (;;) {
        status = gst_vaapi_decoder_get_frame(decode->decoder, &out_frame);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return GST_FLOW_OK;

        if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY(out_frame)) {
            proxy = gst_video_codec_frame_get_user_data(out_frame);

            gst_vaapi_surface_proxy_set_user_data(proxy,
                decode, (GDestroyNotify)gst_vaapidecode_release);

            out_frame->output_buffer =
                gst_vaapi_video_buffer_new_with_surface_proxy(proxy);
            if (!out_frame->output_buffer)
                goto error_create_buffer;
        }

        ret = gst_video_decoder_finish_frame(vdec, out_frame);
        if (ret != GST_FLOW_OK)
            goto error_commit_buffer;

        if (GST_CLOCK_TIME_IS_VALID(out_frame->pts))
            decode->last_buffer_time = out_frame->pts;
        gst_video_codec_frame_unref(out_frame);
    };
    return GST_FLOW_OK;

    /* ERRORS */
error_create_buffer:
    {
        const GstVaapiID surface_id =
            gst_vaapi_surface_get_id(GST_VAAPI_SURFACE_PROXY_SURFACE(proxy));

        GST_ERROR("video sink failed to create video buffer for proxy'ed "
                  "surface %" GST_VAAPI_ID_FORMAT,
                  GST_VAAPI_ID_ARGS(surface_id));
        gst_video_decoder_drop_frame(vdec, out_frame);
        gst_video_codec_frame_unref(out_frame);
        return GST_FLOW_UNEXPECTED;
    }
error_commit_buffer:
    {
        GST_DEBUG("video sink rejected the video buffer (error %d)", ret);
        gst_video_codec_frame_unref(out_frame);
        return GST_FLOW_UNEXPECTED;
    }
}

static GstFlowReturn
gst_vaapidecode_handle_frame(GstVideoDecoder *vdec, GstVideoCodecFrame *frame)
{
    GstFlowReturn ret;

    ret = gst_vaapidecode_decode_frame(vdec, frame);
    if (ret != GST_FLOW_OK)
        return ret;
    return gst_vaapidecode_push_decoded_frames(vdec);
}

static GstFlowReturn
gst_vaapidecode_finish(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiDecoderStatus status;

    status = gst_vaapi_decoder_flush(decode->decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        goto error_flush;
    return gst_vaapidecode_push_decoded_frames(vdec);

    /* ERRORS */
error_flush:
    {
        GST_ERROR("failed to flush decoder (status %d)", status);
        return GST_FLOW_UNEXPECTED;
    }
}

static inline gboolean
gst_vaapidecode_ensure_display(GstVaapiDecode *decode)
{
    return gst_vaapi_ensure_display(decode, GST_VAAPI_DISPLAY_TYPE_ANY,
        &decode->display);
}

static inline guint
gst_vaapi_codec_from_caps(GstCaps *caps)
{
    return gst_vaapi_profile_get_codec(gst_vaapi_profile_from_caps(caps));
}

static gboolean
gst_vaapidecode_create(GstVaapiDecode *decode, GstCaps *caps)
{
    GstVaapiDisplay *dpy;

    if (!gst_vaapidecode_ensure_display(decode))
        return FALSE;
    dpy = decode->display;

    switch (gst_vaapi_codec_from_caps(caps)) {
    case GST_VAAPI_CODEC_MPEG2:
        decode->decoder = gst_vaapi_decoder_mpeg2_new(dpy, caps);
        break;
    case GST_VAAPI_CODEC_MPEG4:
    case GST_VAAPI_CODEC_H263:
        decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);
        break;
    case GST_VAAPI_CODEC_H264:
        decode->decoder = gst_vaapi_decoder_h264_new(dpy, caps);
        break;
    case GST_VAAPI_CODEC_WMV3:
    case GST_VAAPI_CODEC_VC1:
        decode->decoder = gst_vaapi_decoder_vc1_new(dpy, caps);
        break;
#if USE_JPEG_DECODER
    case GST_VAAPI_CODEC_JPEG:
        decode->decoder = gst_vaapi_decoder_jpeg_new(dpy, caps);
        break;
#endif
    default:
        decode->decoder = NULL;
        break;
    }
    if (!decode->decoder)
        return FALSE;

    g_signal_connect(
        G_OBJECT(decode->decoder),
        "notify::caps",
        G_CALLBACK(gst_vaapi_decoder_notify_caps),
        decode
    );

    decode->decoder_caps = gst_caps_ref(caps);
    return TRUE;
}

static void
gst_vaapidecode_destroy(GstVaapiDecode *decode)
{
    g_clear_object(&decode->decoder);
    gst_caps_replace(&decode->decoder_caps, NULL);
    gst_vaapidecode_release(decode);
}

static gboolean
gst_vaapidecode_reset_full(GstVaapiDecode *decode, GstCaps *caps, gboolean hard)
{
    GstVaapiCodec codec;

    /* Reset timers if hard reset was requested (e.g. seek) */
    if (hard) {
        decode->render_time_base = 0;
        decode->last_buffer_time = 0;
    }

    /* Only reset decoder if codec type changed */
    else if (decode->decoder && decode->decoder_caps) {
        if (gst_caps_is_always_compatible(caps, decode->decoder_caps))
            return TRUE;
        codec = gst_vaapi_codec_from_caps(caps);
        if (codec == gst_vaapi_decoder_get_codec(decode->decoder))
            return TRUE;
    }

    gst_vaapidecode_destroy(decode);
    return gst_vaapidecode_create(decode, caps);
}

static void
gst_vaapidecode_finalize(GObject *object)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    gst_vaapidecode_destroy(decode);

    gst_caps_replace(&decode->sinkpad_caps, NULL);
    gst_caps_replace(&decode->srcpad_caps,  NULL);
    gst_caps_replace(&decode->allowed_caps, NULL);

    g_clear_object(&decode->display);

    g_cond_clear(&decode->decoder_ready);
    g_mutex_clear(&decode->decoder_mutex);

    G_OBJECT_CLASS(gst_vaapidecode_parent_class)->finalize(object);
}

static gboolean
gst_vaapidecode_open(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    decode->is_ready = TRUE;
    return TRUE;
}

static gboolean
gst_vaapidecode_close(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    gst_vaapidecode_destroy(decode);
    g_clear_object(&decode->display);
    decode->is_ready = FALSE;
    return TRUE;
}

static gboolean
gst_vaapidecode_reset(GstVideoDecoder *vdec, gboolean hard)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    return gst_vaapidecode_reset_full(decode, decode->sinkpad_caps, hard);
}

static gboolean
gst_vaapidecode_set_format(GstVideoDecoder *vdec, GstVideoCodecState *state)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    if (!gst_vaapidecode_update_sink_caps(decode, state->caps))
        return FALSE;
    if (!gst_vaapidecode_update_src_caps(decode, state))
        return FALSE;
    if (!gst_vaapidecode_reset_full(decode, decode->sinkpad_caps, FALSE))
        return FALSE;
    return TRUE;
}

static GstFlowReturn
gst_vaapidecode_parse(GstVideoDecoder *vdec,
    GstVideoCodecFrame *frame, GstAdapter *adapter, gboolean at_eos)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiDecoderStatus status;
    GstFlowReturn ret;
    guint got_unit_size;
    gboolean got_frame;

    status = gst_vaapi_decoder_parse(decode->decoder, frame,
        adapter, at_eos, &got_unit_size, &got_frame);

    switch (status) {
    case GST_VAAPI_DECODER_STATUS_SUCCESS:
        if (got_unit_size > 0)
            gst_video_decoder_add_to_frame(vdec, got_unit_size);
        if (got_frame)
            ret = gst_video_decoder_have_frame(vdec);
        else
            ret = GST_FLOW_OK;
        break;
    case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        ret = GST_VIDEO_DECODER_FLOW_NEED_DATA;
        break;
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
        GST_WARNING("parse error %d", status);
        ret = GST_FLOW_NOT_SUPPORTED;
        break;
    default:
        GST_ERROR("parse error %d", status);
        ret = GST_FLOW_UNEXPECTED;
        break;
    }
    return ret;
}

static void
gst_vaapidecode_class_init(GstVaapiDecodeClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstVideoDecoderClass * const vdec_class = GST_VIDEO_DECODER_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidecode,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize   = gst_vaapidecode_finalize;

    vdec_class->open         = GST_DEBUG_FUNCPTR(gst_vaapidecode_open);
    vdec_class->close        = GST_DEBUG_FUNCPTR(gst_vaapidecode_close);
    vdec_class->set_format   = GST_DEBUG_FUNCPTR(gst_vaapidecode_set_format);
    vdec_class->reset        = GST_DEBUG_FUNCPTR(gst_vaapidecode_reset);
    vdec_class->parse        = GST_DEBUG_FUNCPTR(gst_vaapidecode_parse);
    vdec_class->handle_frame = GST_DEBUG_FUNCPTR(gst_vaapidecode_handle_frame);
    vdec_class->finish       = GST_DEBUG_FUNCPTR(gst_vaapidecode_finish);

    gst_element_class_set_static_metadata(element_class,
        "VA-API decoder",
        "Codec/Decoder/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);
}

static gboolean
gst_vaapidecode_ensure_allowed_caps(GstVaapiDecode *decode)
{
    GstCaps *decode_caps;
    guint i, n_decode_caps;

    if (decode->allowed_caps)
        return TRUE;

    if (!gst_vaapidecode_ensure_display(decode))
        goto error_no_display;

    decode_caps = gst_vaapi_display_get_decode_caps(decode->display);
    if (!decode_caps)
        goto error_no_decode_caps;
    n_decode_caps = gst_caps_get_size(decode_caps);

    decode->allowed_caps = gst_caps_new_empty();
    if (!decode->allowed_caps)
        goto error_no_memory;

    for (i = 0; i < n_decode_caps; i++) {
        GstStructure *structure;
        structure = gst_caps_get_structure(decode_caps, i);
        if (!structure)
            continue;
        structure = gst_structure_copy(structure);
        if (!structure)
            continue;
        gst_structure_remove_field(structure, "profile");
        gst_structure_set(
            structure,
            "width",  GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            NULL
        );
        gst_caps_merge_structure(decode->allowed_caps, structure);
    }

    gst_caps_unref(decode_caps);
    return TRUE;

    /* ERRORS */
error_no_display:
    {
        GST_ERROR("failed to retrieve VA display");
        return FALSE;
    }
error_no_decode_caps:
    {
        GST_ERROR("failed to retrieve VA decode caps");
        return FALSE;
    }
error_no_memory:
    {
        GST_ERROR("failed to allocate allowed-caps set");
        gst_caps_unref(decode_caps);
        return FALSE;
    }
}

static GstCaps *
gst_vaapidecode_get_caps(GstPad *pad)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!decode->is_ready)
        return gst_static_pad_template_get_caps(&gst_vaapidecode_sink_factory);

    if (!gst_vaapidecode_ensure_allowed_caps(decode))
        return gst_caps_new_empty();

    return gst_caps_ref(decode->allowed_caps);
}

static gboolean
gst_vaapidecode_query (GstPad *pad, GstQuery *query) {
    GstVaapiDecode *decode = GST_VAAPIDECODE (gst_pad_get_parent_element (pad));
    gboolean res;

    GST_DEBUG ("sharing display %p", decode->display);

    if (gst_vaapi_reply_to_query (query, decode->display))
      res = TRUE;
    else if (GST_PAD_IS_SINK(pad))
      res = decode->sinkpad_query(decode->sinkpad, query);
    else
      res = decode->srcpad_query(decode->srcpad, query);

    g_object_unref (decode);
    return res;
}

static void
gst_vaapidecode_init(GstVaapiDecode *decode)
{
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);

    decode->display             = NULL;
    decode->decoder             = NULL;
    decode->decoder_caps        = NULL;
    decode->allowed_caps        = NULL;
    decode->render_time_base    = 0;
    decode->last_buffer_time    = 0;
    decode->is_ready            = FALSE;

    g_mutex_init(&decode->decoder_mutex);
    g_cond_init(&decode->decoder_ready);

    gst_video_decoder_set_packetized(vdec, FALSE);

    /* Pad through which data comes in to the element */
    decode->sinkpad = GST_VIDEO_DECODER_SINK_PAD(vdec);
    decode->sinkpad_query = GST_PAD_QUERYFUNC(decode->sinkpad);
    gst_pad_set_query_function(decode->sinkpad, gst_vaapidecode_query);
    gst_pad_set_getcaps_function(decode->sinkpad, gst_vaapidecode_get_caps);

    /* Pad through which data goes out of the element */
    decode->srcpad = GST_VIDEO_DECODER_SRC_PAD(vdec);
    decode->srcpad_query = GST_PAD_QUERYFUNC(decode->srcpad);
    gst_pad_set_query_function(decode->srcpad, gst_vaapidecode_query);
}
