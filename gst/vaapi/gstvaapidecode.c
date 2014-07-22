/*
 *  gstvaapidecode.c - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#include "gstvaapidecode.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
#include "gstvaapivideometa_texture.h"
#endif
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"
#endif

#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>
#include <gst/vaapi/gstvaapidecoder_vp8.h>

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

#define GST_VAAPI_DECODE_FLOW_PARSE_DATA        GST_FLOW_CUSTOM_SUCCESS_2

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
    GST_CAPS_CODEC("video/x-vp8")
    GST_CAPS_CODEC("image/jpeg")
    ;

static const char gst_vaapidecode_src_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12 }") ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, "RGBA") ";"
    GST_VIDEO_CAPS_MAKE("{ NV12, I420, YV12 }");
#else
    GST_VAAPI_SURFACE_CAPS;
#endif

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

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiDecode,
    gst_vaapidecode,
    GST_TYPE_VIDEO_DECODER,
    GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES)

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode,
    const GstVideoCodecState *ref_state);

static void
gst_vaapi_decoder_state_changed(GstVaapiDecoder *decoder,
    const GstVideoCodecState *codec_state, gpointer user_data)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(user_data);
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);

    g_assert(decode->decoder == decoder);

    gst_vaapidecode_update_src_caps(decode, codec_state);
    gst_video_decoder_negotiate(vdec);
}

static inline gboolean
gst_vaapidecode_update_sink_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    gst_caps_replace(&decode->sinkpad_caps, caps);
    return TRUE;
}

#if GST_CHECK_VERSION(1,1,0)
static void
gst_vaapidecode_video_info_change_format(GstVideoInfo *info,
    GstVideoFormat format, guint width, guint height)
{
    GstVideoInfo vi = *info;

    gst_video_info_set_format (info, format, width, height);

    info->interlace_mode = vi.interlace_mode;
    info->flags = vi.flags;
    info->views = vi.views;
    info->par_n = vi.par_n;
    info->par_d = vi.par_d;
    info->fps_n = vi.fps_n;
    info->fps_d = vi.fps_d;
}
#endif

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode,
    const GstVideoCodecState *ref_state)
{
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);
    GstVideoCodecState *state;
    GstVideoInfo *vi, vis;
    GstVideoFormat format, out_format;
#if GST_CHECK_VERSION(1,1,0)
    GstCapsFeatures *features = NULL;
    GstVaapiCapsFeature feature;

    feature = gst_vaapi_find_preferred_caps_feature(
        GST_VIDEO_DECODER_SRC_PAD(vdec),
        GST_VIDEO_INFO_FORMAT(&ref_state->info));
#endif

    format = GST_VIDEO_INFO_FORMAT(&ref_state->info);

    state = gst_video_decoder_set_output_state(vdec, format,
        ref_state->info.width, ref_state->info.height,
        (GstVideoCodecState *)ref_state);
    if (!state)
        return FALSE;

    vi = &state->info;
    out_format = format;
    if (format == GST_VIDEO_FORMAT_ENCODED) {
#if GST_CHECK_VERSION(1,1,0)
        out_format = GST_VIDEO_FORMAT_NV12;
        if (feature == GST_VAAPI_CAPS_FEATURE_SYSTEM_MEMORY) {
            /* XXX: intercept with the preferred output format.
               Anyway, I420 is the minimum format that drivers
               should support to be useful */
            out_format = GST_VIDEO_FORMAT_I420;
        }
#endif
        gst_video_info_init(&vis);
        gst_video_info_set_format(&vis, out_format,
            GST_VIDEO_INFO_WIDTH(vi), GST_VIDEO_INFO_HEIGHT(vi));
        vi->size = vis.size;
    }
    gst_video_codec_state_unref(state);

#if GST_CHECK_VERSION(1,1,0)
    vis = *vi;
    switch (feature) {
    case GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META:
        gst_vaapidecode_video_info_change_format(&vis, GST_VIDEO_FORMAT_RGBA,
            GST_VIDEO_INFO_WIDTH(vi), GST_VIDEO_INFO_HEIGHT(vi));
        features = gst_caps_features_new(
            GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, NULL);
        break;
    default:
        if (format == GST_VIDEO_FORMAT_ENCODED) {
            /* XXX: this is a workaround until auto-plugging is fixed when
            format=ENCODED + memory:VASurface caps feature are provided.
            Meanwhile, providing a random format here works but this is
            a terribly wrong thing per se. */
            gst_vaapidecode_video_info_change_format(&vis, out_format,
                GST_VIDEO_INFO_WIDTH(vi), GST_VIDEO_INFO_HEIGHT(vi));
#if GST_CHECK_VERSION(1,3,0)
            if (feature == GST_VAAPI_CAPS_FEATURE_VAAPI_SURFACE)
                features = gst_caps_features_new(
                    GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, NULL);
#endif
        }
        break;
    }
    state->caps = gst_video_info_to_caps(&vis);
    if (features)
        gst_caps_set_features(state->caps, 0, features);
#else
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

    gst_caps_set_interlaced(state->caps, vi);
#endif
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

    /* Decode current frame */
    for (;;) {
        status = gst_vaapi_decoder_decode(decode->decoder, frame);
        if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) {
            GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);
            g_mutex_lock(&decode->decoder_mutex);
            g_cond_wait(&decode->decoder_ready, &decode->decoder_mutex);
            g_mutex_unlock(&decode->decoder_mutex);
            GST_VIDEO_DECODER_STREAM_LOCK(vdec);
            if (decode->decoder_loop_status < 0)
                goto error_decode_loop;
            continue;
        }
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto error_decode;
        break;
    }

    /* Try to report back early any error that occured in the decode task */
    GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);
    GST_VIDEO_DECODER_STREAM_LOCK(vdec);
    return decode->decoder_loop_status;

    /* ERRORS */
error_decode_loop:
    {
        GST_ERROR("decode loop error %d", decode->decoder_loop_status);
        gst_video_decoder_drop_frame(vdec, frame);
        return decode->decoder_loop_status;
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
            ret = GST_FLOW_ERROR;
            break;
        }
        gst_video_decoder_drop_frame(vdec, frame);
        return ret;
    }
}

static GstFlowReturn
gst_vaapidecode_push_decoded_frame(GstVideoDecoder *vdec,
    GstVideoCodecFrame *out_frame)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiSurfaceProxy *proxy;
    GstFlowReturn ret;
#if GST_CHECK_VERSION(1,0,0)
    const GstVaapiRectangle *crop_rect;
    GstVaapiVideoMeta *meta;
    guint flags;
#endif

    if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY(out_frame)) {
        proxy = gst_video_codec_frame_get_user_data(out_frame);

        gst_vaapi_surface_proxy_set_destroy_notify(proxy,
            (GDestroyNotify)gst_vaapidecode_release, decode);

#if GST_CHECK_VERSION(1,0,0)
        ret = gst_video_decoder_allocate_output_frame(vdec, out_frame);
        if (ret != GST_FLOW_OK)
            goto error_create_buffer;

        meta = gst_buffer_get_vaapi_video_meta(out_frame->output_buffer);
        if (!meta)
            goto error_get_meta;
        gst_vaapi_video_meta_set_surface_proxy(meta, proxy);

        flags = gst_vaapi_surface_proxy_get_flags(proxy);
        if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_INTERLACED) {
            guint out_flags = GST_VIDEO_BUFFER_FLAG_INTERLACED;
            if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_TFF)
                out_flags |= GST_VIDEO_BUFFER_FLAG_TFF;
            if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_RFF)
                out_flags |= GST_VIDEO_BUFFER_FLAG_RFF;
            if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_ONEFIELD)
                out_flags |= GST_VIDEO_BUFFER_FLAG_ONEFIELD;
            GST_BUFFER_FLAG_SET(out_frame->output_buffer, out_flags);
        }

        crop_rect = gst_vaapi_surface_proxy_get_crop_rect(proxy);
        if (crop_rect) {
            GstVideoCropMeta * const crop_meta =
                gst_buffer_add_video_crop_meta(out_frame->output_buffer);
            if (crop_meta) {
                crop_meta->x = crop_rect->x;
                crop_meta->y = crop_rect->y;
                crop_meta->width = crop_rect->width;
                crop_meta->height = crop_rect->height;
            }
        }

#if GST_CHECK_VERSION(1,1,0) && USE_GLX
        if (decode->has_texture_upload_meta)
            gst_buffer_ensure_texture_upload_meta(out_frame->output_buffer);
#endif
#else
        out_frame->output_buffer =
            gst_vaapi_video_buffer_new_with_surface_proxy(proxy);
        if (!out_frame->output_buffer)
            goto error_create_buffer;
#endif
    }

    ret = gst_video_decoder_finish_frame(vdec, out_frame);
    if (ret != GST_FLOW_OK)
        goto error_commit_buffer;

    gst_video_codec_frame_unref(out_frame);
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
        return GST_FLOW_ERROR;
    }
#if GST_CHECK_VERSION(1,0,0)
error_get_meta:
    {
        GST_ERROR("failed to get vaapi video meta attached to video buffer");
        gst_video_decoder_drop_frame(vdec, out_frame);
        gst_video_codec_frame_unref(out_frame);
        return GST_FLOW_ERROR;
    }
#endif
error_commit_buffer:
    {
        if (ret != GST_FLOW_FLUSHING)
            GST_ERROR("video sink rejected the video buffer (error %d)", ret);
        gst_video_codec_frame_unref(out_frame);
        return ret;
    }
}

static GstFlowReturn
gst_vaapidecode_handle_frame(GstVideoDecoder *vdec, GstVideoCodecFrame *frame)
{
    GstFlowReturn ret;

    /* Make sure to release the base class stream lock so that decode
       loop can call gst_video_decoder_finish_frame() without blocking */
    GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);
    ret = gst_vaapidecode_decode_frame(vdec, frame);
    GST_VIDEO_DECODER_STREAM_LOCK(vdec);
    return ret;
}

static void
gst_vaapidecode_decode_loop(GstVaapiDecode *decode)
{
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);
    GstVaapiDecoderStatus status;
    GstVideoCodecFrame *out_frame;
    GstFlowReturn ret;

    status = gst_vaapi_decoder_get_frame_with_timeout(decode->decoder,
        &out_frame, 100000);

    GST_VIDEO_DECODER_STREAM_LOCK(vdec);
    switch (status) {
    case GST_VAAPI_DECODER_STATUS_SUCCESS:
        ret = gst_vaapidecode_push_decoded_frame(vdec, out_frame);
        break;
    case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        ret = GST_VIDEO_DECODER_FLOW_NEED_DATA;
        break;
    default:
        ret = GST_FLOW_ERROR;
        break;
    }
    decode->decoder_loop_status = ret;
    GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);

    if (ret == GST_FLOW_OK)
        return;

    /* If invoked from gst_vaapidecode_finish(), then return right
       away no matter the errors, or the GstVaapiDecoder needs further
       data to complete decoding (there no more data to feed in) */
    if (decode->decoder_finish) {
        g_mutex_lock(&decode->decoder_mutex);
        g_cond_signal(&decode->decoder_finish_done);
        g_mutex_unlock(&decode->decoder_mutex);
        return;
    }

    /* Suspend the task if an error occurred */
    if (ret != GST_VIDEO_DECODER_FLOW_NEED_DATA)
        gst_pad_pause_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode));
}

static gboolean
gst_vaapidecode_flush(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiDecoderStatus status;

    /* If there is something in GstVideoDecoder's output adapter, then
       submit the frame for decoding */
    if (decode->current_frame_size) {
        gst_video_decoder_have_frame(vdec);
        decode->current_frame_size = 0;
    }

    status = gst_vaapi_decoder_flush(decode->decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        goto error_flush;
    return TRUE;

    /* ERRORS */
error_flush:
    {
        GST_ERROR("failed to flush decoder (status %d)", status);
        return FALSE;
    }
}

static GstFlowReturn
gst_vaapidecode_finish(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstFlowReturn ret = GST_FLOW_OK;

    if (!gst_vaapidecode_flush(vdec))
        ret = GST_FLOW_OK;

    /* Make sure the decode loop function has a chance to return, thus
       possibly unlocking gst_video_decoder_finish_frame() */
    GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);
    g_mutex_lock(&decode->decoder_mutex);
    decode->decoder_finish = TRUE;
    g_cond_wait(&decode->decoder_finish_done, &decode->decoder_mutex);
    g_mutex_unlock(&decode->decoder_mutex);
    gst_pad_stop_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode));
    GST_VIDEO_DECODER_STREAM_LOCK(vdec);
    return ret;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapidecode_decide_allocation(GstVideoDecoder *vdec, GstQuery *query)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstCaps *caps = NULL;
    GstBufferPool *pool;
    GstStructure *config;
    GstVideoInfo vi;
    guint size, min, max;
    gboolean need_pool, update_pool;
    gboolean has_video_meta = FALSE;
    GstVideoCodecState *state;
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
    gboolean has_texture_upload_meta = FALSE;
    GstCapsFeatures *features, *features2;
#endif

    gst_query_parse_allocation(query, &caps, &need_pool);

    if (!caps)
        goto error_no_caps;

    state = gst_video_decoder_get_output_state(vdec);

    decode->has_texture_upload_meta = FALSE;
    has_video_meta = gst_query_find_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
    if (has_video_meta)
        decode->has_texture_upload_meta = gst_query_find_allocation_meta(query,
            GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);

    features = gst_caps_get_features(state->caps, 0);
    features2 = gst_caps_features_new(GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, NULL);

    has_texture_upload_meta =
        gst_vaapi_find_preferred_caps_feature(GST_VIDEO_DECODER_SRC_PAD(vdec),
            GST_VIDEO_FORMAT_ENCODED) ==
        GST_VAAPI_CAPS_FEATURE_GL_TEXTURE_UPLOAD_META;

    /* Update src caps if feature is not handled downstream */
    if (!decode->has_texture_upload_meta &&
        gst_caps_features_is_equal(features, features2))
        gst_vaapidecode_update_src_caps (decode, state);
    else if (has_texture_upload_meta &&
             !gst_caps_features_is_equal(features, features2)) {
        gst_video_info_set_format(&state->info, GST_VIDEO_FORMAT_RGBA,
                                  state->info.width,
                                  state->info.height);
        gst_vaapidecode_update_src_caps(decode, state);
    }
    gst_caps_features_free(features2);
#endif

    gst_video_codec_state_unref(state);

    gst_video_info_init(&vi);
    gst_video_info_from_caps(&vi, caps);
    if (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_ENCODED)
        gst_video_info_set_format(&vi, GST_VIDEO_FORMAT_NV12,
            GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));

    g_return_val_if_fail(GST_VAAPI_PLUGIN_BASE_DISPLAY(decode) != NULL, FALSE);

    if (gst_query_get_n_allocation_pools(query) > 0) {
        gst_query_parse_nth_allocation_pool(query, 0, &pool, &size, &min, &max);
        size = MAX(size, vi.size);
        update_pool = TRUE;
    }
    else {
        pool = NULL;
        size = vi.size;
        min = max = 0;
        update_pool = FALSE;
    }

    if (!pool || !gst_buffer_pool_has_option(pool,
            GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META)) {
        GST_INFO("no pool or doesn't support GstVaapiVideoMeta, "
            "making new pool");
        if (pool)
            gst_object_unref(pool);
        pool = gst_vaapi_video_buffer_pool_new(
            GST_VAAPI_PLUGIN_BASE_DISPLAY(decode));
        if (!pool)
            goto error_create_pool;

        config = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_set_params(config, caps, size, min, max);
        gst_buffer_pool_config_add_option(config,
            GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
        gst_buffer_pool_set_config(pool, config);
    }

    if (has_video_meta) {
        config = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_add_option(config,
            GST_BUFFER_POOL_OPTION_VIDEO_META);
#if GST_CHECK_VERSION(1,1,0) && USE_GLX
        if (decode->has_texture_upload_meta)
            gst_buffer_pool_config_add_option(config,
                GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);
#endif
        gst_buffer_pool_set_config(pool, config);
    }

    if (update_pool)
        gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
    else
        gst_query_add_allocation_pool(query, pool, size, min, max);
    if (pool)
        gst_object_unref(pool);
    return TRUE;

    /* ERRORS */
error_no_caps:
    {
        GST_ERROR("no caps specified");
        return FALSE;
    }
error_create_pool:
    {
        GST_ERROR("failed to create buffer pool");
        return FALSE;
    }
}
#endif

static inline gboolean
gst_vaapidecode_ensure_display(GstVaapiDecode *decode)
{
    return gst_vaapi_plugin_base_ensure_display(GST_VAAPI_PLUGIN_BASE(decode));
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
    dpy = GST_VAAPI_PLUGIN_BASE_DISPLAY(decode);

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

        /* Set the stream buffer alignment for better optimizations */
        if (decode->decoder && caps) {
            GstStructure * const structure = gst_caps_get_structure(caps, 0);
            const gchar *str = NULL;

            if ((str = gst_structure_get_string(structure, "alignment"))) {
                GstVaapiStreamAlignH264 alignment;
                if (g_strcmp0(str, "au") == 0)
                    alignment = GST_VAAPI_STREAM_ALIGN_H264_AU;
                else if (g_strcmp0(str, "nal") == 0)
                    alignment = GST_VAAPI_STREAM_ALIGN_H264_NALU;
                else
                    alignment = GST_VAAPI_STREAM_ALIGN_H264_NONE;
                gst_vaapi_decoder_h264_set_alignment(
                    GST_VAAPI_DECODER_H264(decode->decoder), alignment);
            }
        }
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
#if USE_VP8_DECODER
    case GST_VAAPI_CODEC_VP8:
        decode->decoder = gst_vaapi_decoder_vp8_new(dpy, caps);
        break;
#endif
    default:
        decode->decoder = NULL;
        break;
    }
    if (!decode->decoder)
        return FALSE;

    gst_vaapi_decoder_set_codec_state_changed_func(decode->decoder,
        gst_vaapi_decoder_state_changed, decode);

    decode->decoder_caps = gst_caps_ref(caps);
    return gst_pad_start_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode),
        (GstTaskFunction)gst_vaapidecode_decode_loop, decode, NULL);
}

static void
gst_vaapidecode_destroy(GstVaapiDecode *decode)
{
    gst_pad_stop_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode));
    gst_vaapi_decoder_replace(&decode->decoder, NULL);
    gst_caps_replace(&decode->decoder_caps, NULL);
    gst_vaapidecode_release(decode);
}

static gboolean
gst_vaapidecode_reset_full(GstVaapiDecode *decode, GstCaps *caps, gboolean hard)
{
    GstVaapiCodec codec;

    decode->has_texture_upload_meta = FALSE;

    /* Reset tracked frame size */
    decode->current_frame_size = 0;

    /* Reset timers if hard reset was requested (e.g. seek) */
    if (hard) {
        GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);
        GstVideoCodecFrame *out_frame = NULL;

        gst_vaapi_decoder_flush(decode->decoder);
        GST_VIDEO_DECODER_STREAM_UNLOCK(vdec);
        gst_pad_stop_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode));
        GST_VIDEO_DECODER_STREAM_LOCK(vdec);
        decode->decoder_loop_status = GST_FLOW_OK;

        /* Purge all decoded frames as we don't need them (e.g. seek) */
        while (gst_vaapi_decoder_get_frame_with_timeout(decode->decoder,
                   &out_frame, 0) == GST_VAAPI_DECODER_STATUS_SUCCESS) {
            gst_video_codec_frame_unref(out_frame);
            out_frame = NULL;
        }
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

    gst_caps_replace(&decode->sinkpad_caps, NULL);
    gst_caps_replace(&decode->srcpad_caps,  NULL);
    gst_caps_replace(&decode->allowed_caps, NULL);

    g_cond_clear(&decode->decoder_finish_done);
    g_cond_clear(&decode->decoder_ready);
    g_mutex_clear(&decode->decoder_mutex);

    gst_vaapi_plugin_base_finalize(GST_VAAPI_PLUGIN_BASE(object));
    G_OBJECT_CLASS(gst_vaapidecode_parent_class)->finalize(object);
}

static gboolean
gst_vaapidecode_open(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);
    GstVaapiDisplay * const old_display = GST_VAAPI_PLUGIN_BASE_DISPLAY(decode);
    gboolean success;

    if (!gst_vaapi_plugin_base_open(GST_VAAPI_PLUGIN_BASE(decode)))
        return FALSE;

    /* Let GstVideoContext ask for a proper display to its neighbours */
    /* Note: steal old display that may be allocated from get_caps()
       so that to retain a reference to it, thus avoiding extra
       initialization steps if we turn out to simply re-use the
       existing (cached) VA display */
    GST_VAAPI_PLUGIN_BASE_DISPLAY(decode) = NULL;
    success = gst_vaapidecode_ensure_display(decode);
    if (old_display)
        gst_vaapi_display_unref(old_display);
    return success;
}

static gboolean
gst_vaapidecode_close(GstVideoDecoder *vdec)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    gst_vaapidecode_destroy(decode);
    gst_vaapi_plugin_base_close(GST_VAAPI_PLUGIN_BASE(decode));
    return TRUE;
}

static gboolean
gst_vaapidecode_reset(GstVideoDecoder *vdec, gboolean hard)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    /* In GStreamer 1.0 context, this means a flush */
    if (decode->decoder && !hard && !gst_vaapidecode_flush(vdec))
        return FALSE;
    return gst_vaapidecode_reset_full(decode, decode->sinkpad_caps, hard);
}

static gboolean
gst_vaapidecode_set_format(GstVideoDecoder *vdec, GstVideoCodecState *state)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(vdec);
    GstVaapiDecode * const decode = GST_VAAPIDECODE(vdec);

    if (!gst_vaapidecode_update_sink_caps(decode, state->caps))
        return FALSE;
    if (!gst_vaapidecode_update_src_caps(decode, state))
        return FALSE;
    if (!gst_video_decoder_negotiate(vdec))
        return FALSE;
    if (!gst_vaapi_plugin_base_set_caps(plugin, decode->sinkpad_caps,
            decode->srcpad_caps))
        return FALSE;
    if (!gst_vaapidecode_reset_full(decode, decode->sinkpad_caps, FALSE))
        return FALSE;
    return TRUE;
}

static GstFlowReturn
gst_vaapidecode_parse_frame(GstVideoDecoder *vdec,
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
        if (got_unit_size > 0) {
            gst_video_decoder_add_to_frame(vdec, got_unit_size);
            decode->current_frame_size += got_unit_size;
        }
        if (got_frame) {
            ret = gst_video_decoder_have_frame(vdec);
            decode->current_frame_size = 0;
        }
        else
            ret = GST_VAAPI_DECODE_FLOW_PARSE_DATA;
        break;
    case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        ret = GST_VIDEO_DECODER_FLOW_NEED_DATA;
        break;
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
    case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
        GST_WARNING("parse error %d", status);
        ret = GST_FLOW_NOT_SUPPORTED;
        decode->current_frame_size = 0;
        break;
    default:
        GST_ERROR("parse error %d", status);
        ret = GST_FLOW_EOS;
        decode->current_frame_size = 0;
        break;
    }
    return ret;
}

static GstFlowReturn
gst_vaapidecode_parse(GstVideoDecoder *vdec,
    GstVideoCodecFrame *frame, GstAdapter *adapter, gboolean at_eos)
{
    GstFlowReturn ret;

    do {
        ret = gst_vaapidecode_parse_frame(vdec, frame, adapter, at_eos);
    } while (ret == GST_VAAPI_DECODE_FLOW_PARSE_DATA);
    return ret;
}

static GstStateChangeReturn
gst_vaapidecode_change_state (GstElement * element, GstStateChange transition)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(element);

    switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_pad_stop_task(GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode));
        break;
    default:
        break;
    }
    return GST_ELEMENT_CLASS(gst_vaapidecode_parent_class)->change_state(
        element, transition);
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

    gst_vaapi_plugin_base_class_init(GST_VAAPI_PLUGIN_BASE_CLASS(klass));

    object_class->finalize   = gst_vaapidecode_finalize;

    element_class->change_state =
        GST_DEBUG_FUNCPTR(gst_vaapidecode_change_state);

    vdec_class->open         = GST_DEBUG_FUNCPTR(gst_vaapidecode_open);
    vdec_class->close        = GST_DEBUG_FUNCPTR(gst_vaapidecode_close);
    vdec_class->set_format   = GST_DEBUG_FUNCPTR(gst_vaapidecode_set_format);
    vdec_class->reset        = GST_DEBUG_FUNCPTR(gst_vaapidecode_reset);
    vdec_class->parse        = GST_DEBUG_FUNCPTR(gst_vaapidecode_parse);
    vdec_class->handle_frame = GST_DEBUG_FUNCPTR(gst_vaapidecode_handle_frame);
    vdec_class->finish       = GST_DEBUG_FUNCPTR(gst_vaapidecode_finish);

#if GST_CHECK_VERSION(1,0,0)
    vdec_class->decide_allocation =
        GST_DEBUG_FUNCPTR(gst_vaapidecode_decide_allocation);
#endif

    gst_element_class_set_static_metadata(element_class,
        "VA-API decoder",
        "Codec/Decoder/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
}

static gboolean
gst_vaapidecode_ensure_allowed_caps(GstVaapiDecode *decode)
{
    GstCaps *caps, *allowed_caps;
    GArray *profiles;
    guint i;

    if (decode->allowed_caps)
        return TRUE;

    if (!gst_vaapidecode_ensure_display(decode))
        goto error_no_display;

    profiles = gst_vaapi_display_get_decode_profiles(
        GST_VAAPI_PLUGIN_BASE_DISPLAY(decode));
    if (!profiles)
        goto error_no_profiles;

    allowed_caps = gst_caps_new_empty();
    if (!allowed_caps)
        goto error_no_memory;

    for (i = 0; i < profiles->len; i++) {
        const GstVaapiProfile profile =
            g_array_index(profiles, GstVaapiProfile, i);
        const gchar *media_type_name;

        media_type_name = gst_vaapi_profile_get_media_type_name(profile);
        if (!media_type_name)
            continue;

        caps = gst_caps_from_string(media_type_name);
        if (!caps)
            continue;
        allowed_caps = gst_caps_merge(allowed_caps, caps);
    }
    decode->allowed_caps = allowed_caps;

    g_array_unref(profiles);
    return TRUE;

    /* ERRORS */
error_no_display:
    {
        GST_ERROR("failed to retrieve VA display");
        return FALSE;
    }
error_no_profiles:
    {
        GST_ERROR("failed to retrieve VA decode profiles");
        return FALSE;
    }
error_no_memory:
    {
        GST_ERROR("failed to allocate allowed-caps set");
        g_array_unref(profiles);
        return FALSE;
    }
}

static GstCaps *
gst_vaapidecode_get_caps(GstPad *pad)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!gst_vaapidecode_ensure_allowed_caps(decode))
        return gst_caps_new_empty();

    return gst_caps_ref(decode->allowed_caps);
}

static gboolean
gst_vaapidecode_query(GST_PAD_QUERY_FUNCTION_ARGS)
{
    GstVaapiDecode * const decode =
        GST_VAAPIDECODE(gst_pad_get_parent_element(pad));
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(decode);
    gboolean res;

    GST_INFO_OBJECT(decode, "query type %s", GST_QUERY_TYPE_NAME(query));

    if (gst_vaapi_reply_to_query(query, plugin->display)) {
        GST_DEBUG("sharing display %p", plugin->display);
        res = TRUE;
    }
    else if (GST_PAD_IS_SINK(pad)) {
        switch (GST_QUERY_TYPE(query)) {
#if GST_CHECK_VERSION(1,0,0)
        case GST_QUERY_CAPS: {
            GstCaps * const caps = gst_vaapidecode_get_caps(pad);
            gst_query_set_caps_result(query, caps);
            gst_caps_unref(caps);
            res = TRUE;
            break;
        }
#endif
        default:
            res = GST_PAD_QUERY_FUNCTION_CALL(plugin->sinkpad_query, pad,
                parent, query);
            break;
        }
    }
    else
        res = GST_PAD_QUERY_FUNCTION_CALL(plugin->srcpad_query, pad,
            parent, query);

    gst_object_unref(decode);
    return res;
}

static void
gst_vaapidecode_init(GstVaapiDecode *decode)
{
    GstVideoDecoder * const vdec = GST_VIDEO_DECODER(decode);
    GstPad *pad;

    gst_vaapi_plugin_base_init(GST_VAAPI_PLUGIN_BASE(decode), GST_CAT_DEFAULT);

    decode->decoder             = NULL;
    decode->decoder_caps        = NULL;
    decode->allowed_caps        = NULL;
    decode->decoder_loop_status = GST_FLOW_OK;

    g_mutex_init(&decode->decoder_mutex);
    g_cond_init(&decode->decoder_ready);
    g_cond_init(&decode->decoder_finish_done);

    gst_video_decoder_set_packetized(vdec, FALSE);

    /* Pad through which data comes in to the element */
    pad = GST_VAAPI_PLUGIN_BASE_SINK_PAD(decode);
    gst_pad_set_query_function(pad, gst_vaapidecode_query);
#if !GST_CHECK_VERSION(1,0,0)
    gst_pad_set_getcaps_function(pad, gst_vaapidecode_get_caps);
#endif

    /* Pad through which data goes out of the element */
    pad = GST_VAAPI_PLUGIN_BASE_SRC_PAD(decode);
    gst_pad_set_query_function(pad, gst_vaapidecode_query);
}
