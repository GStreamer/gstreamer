/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include <gst/codecparsers/gstlcevcmeta.h>

#include <lcevc_eil.h>

#include "gstlcevcencoder.h"
#include "gstlcevcencoderutils.h"

GST_DEBUG_CATEGORY_STATIC (lcevcencoder_debug);
#define GST_CAT_DEFAULT (lcevcencoder_debug)

#define DEFAULT_MIN_BITRATE 0
#define DEFAULT_MAX_BITRATE 2048000
#define DEFAULT_BITRATE 0
#define DEFAULT_SEI_LCEVC TRUE
#define DEFAULT_MIN_GOP_LENGTH -2
#define DEFAULT_GOP_LENGTH -2
#define DEFAULT_DEBUG FALSE

/* The max number of frames the encoder can receive without encoding a frame */
#define MAX_DELAYED_FRAMES 65

struct GstEILContext_
{
  grefcount ref;
  EILContext context;
};
typedef struct GstEILContext_ GstEILContext;

static void
log_callback (void *userdata, int32_t level, const char *msg)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (userdata);
  gchar *new_msg = NULL;

  if (strlen (msg) <= 1)
    return;

  /* Remove \n from msg */
  new_msg = g_strdup (msg);
  new_msg[strlen (msg) - 1] = '\0';

  switch (level) {
    case EIL_LL_Error:
      GST_ERROR_OBJECT (eil, "EIL: %s", new_msg);
      break;
    case EIL_LL_Warning:
      GST_WARNING_OBJECT (eil, "EIL: %s", new_msg);
      break;
    case EIL_LL_Info:
      GST_INFO_OBJECT (eil, "EIL: %s", new_msg);
      break;
    case EIL_LL_Debug:
      GST_DEBUG_OBJECT (eil, "EIL: %s", new_msg);
      break;
    case EIL_LL_Verbose:
      GST_TRACE_OBJECT (eil, "EIL: %s", new_msg);
      break;
    default:
      break;
  }

  g_clear_pointer (&new_msg, g_free);
}

static GstEILContext *
gst_eil_context_new (GstLcevcEncoder * eil, const gchar * plugin_name,
    gboolean debug)
{
  GstEILContext *ctx = g_new0 (GstEILContext, 1);
  EILOpenSettings settings;
  EILReturnCode rc;

  g_return_val_if_fail (plugin_name, NULL);

  /* Initialize settings to default values */
  rc = EIL_OpenSettingsDefault (&settings);
  if (rc != EIL_RC_Success)
    return NULL;

  /* Set settings */
  settings.base_encoder = plugin_name;
  if (debug) {
    settings.log_callback = log_callback;
    settings.log_userdata = eil;
  }

  /* Open EIL context */
  if ((rc = EIL_Open (&settings, &ctx->context)) != EIL_RC_Success)
    return NULL;

  /* Init refcount */
  g_ref_count_init (&ctx->ref);
  return ctx;
}

static void
gst_eil_context_unref (GstEILContext * ctx)
{
  if (g_ref_count_dec (&ctx->ref)) {
    EIL_Close (ctx->context);
    g_free (ctx);
  }
}

static GstEILContext *
gst_eil_context_ref (GstEILContext * ctx)
{
  g_ref_count_inc (&ctx->ref);
  return ctx;
}

typedef struct
{
  GstEILContext *ctx;
  EILOutput *output;
} OutputData;

static OutputData *
output_data_new (GstEILContext * ctx, EILOutput * output)
{
  OutputData *data = g_new0 (OutputData, 1);
  data->ctx = gst_eil_context_ref (ctx);
  data->output = output;
  return data;
}

static void
output_data_free (gpointer p)
{
  OutputData *data = p;
  EIL_ReleaseOutput (data->ctx->context, data->output);
  g_clear_pointer (&data->ctx, gst_eil_context_unref);
  g_free (data);
}

enum
{
  PROP_0,
  PROP_PLUGIN_NAME,
  PROP_PLUGIN_PROPS,
  PROP_BITRATE,
  PROP_SEI_LCEVC,
  PROP_GOP_LENGTH,
  PROP_DEBUG,
};

typedef struct
{
  /* Props */
  gchar *plugin_name;
  gchar *plugin_props;
  guint bitrate;
  gboolean sei_lcevc;
  gint gop_length;
  gboolean debug;

  /* EIL */
  GstEILContext *ctx;
  GHashTable *plugin_props_spec;

  /* Input info */
  GstVideoInfo in_info;
  EILColourFormat in_format;
  EILFrameType in_frame_type;

  GstClockTime out_ts_offset;
} GstLcevcEncoderPrivate;

#define gst_lcevc_encoder_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstLcevcEncoder, gst_lcevc_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstLcevcEncoder);
    GST_DEBUG_CATEGORY_INIT (lcevcencoder_debug, "lcevcencoder", 0,
        "lcevcencoder"));

static GstStaticPadTemplate gst_lcevc_encoder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        (GST_LCEVC_ENCODER_UTILS_SUPPORTED_FORMATS))
    );

static void
gst_lcevc_encoder_finalize (GObject * obj)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (obj);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);

  g_clear_pointer (&priv->plugin_name, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_lcevc_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLcevcEncoder *self = GST_LCEVC_ENCODER (object);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (self);

  switch (prop_id) {
    case PROP_PLUGIN_NAME:
      g_clear_pointer (&priv->plugin_name, g_free);
      priv->plugin_name = g_value_dup_string (value);
      break;
    case PROP_PLUGIN_PROPS:
      g_clear_pointer (&priv->plugin_props, g_free);
      priv->plugin_props = g_value_dup_string (value);
      break;
    case PROP_BITRATE:
      priv->bitrate = g_value_get_uint (value);
      break;
    case PROP_SEI_LCEVC:
      priv->sei_lcevc = g_value_get_boolean (value);
      break;
    case PROP_GOP_LENGTH:
      priv->gop_length = g_value_get_int (value);
      break;
    case PROP_DEBUG:
      priv->debug = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lcevc_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstLcevcEncoder *self = GST_LCEVC_ENCODER (object);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (self);

  switch (prop_id) {
    case PROP_PLUGIN_NAME:
      g_value_set_string (value, priv->plugin_name);
      break;
    case PROP_PLUGIN_PROPS:
      g_value_set_string (value, priv->plugin_props);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, priv->bitrate);
      break;
    case PROP_SEI_LCEVC:
      g_value_set_boolean (value, priv->sei_lcevc);
      break;
    case PROP_GOP_LENGTH:
      g_value_set_int (value, priv->gop_length);
      break;
    case PROP_DEBUG:
      g_value_set_boolean (value, priv->debug);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GHashTable *
get_plugin_props_spec (GstEILContext * ctx, const gchar * plugin_name)
{
  EILReturnCode rc;
  GHashTable *res;
  EILPropertyGroups groups;

  rc = EIL_QueryPropertyGroups (ctx->context, &groups);
  if (rc != EIL_RC_Success)
    return NULL;

  res = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (guint32 i = 0; i < groups.group_count; i++) {
    EILPropertyGroup *group = &groups.group[i];

    for (guint32 j = 0; j < group->property_count; j++) {
      EILProperty *property = &group->properties[j];
      g_hash_table_insert (res, g_strdup (property->name),
          GINT_TO_POINTER (property->type));
    }
  }

  return res;
}

static gboolean
open_eil_context (GstLcevcEncoder * eil)
{
  GstLcevcEncoderClass *klass = GST_LCEVC_ENCODER_GET_CLASS (eil);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  const gchar *plugin_name;

  g_return_val_if_fail (!priv->ctx, FALSE);

  /* Get the plugin name */
  if (priv->plugin_name)
    plugin_name = priv->plugin_name;
  else if (klass->get_eil_plugin_name)
    plugin_name = klass->get_eil_plugin_name (eil);
  else
    return FALSE;

  /* Create the EIL context */
  priv->ctx = gst_eil_context_new (eil, plugin_name, priv->debug);
  if (!priv->ctx)
    return FALSE;

  /* Get the plugin properties spec */
  priv->plugin_props_spec = get_plugin_props_spec (priv->ctx, plugin_name);
  if (!priv->plugin_props_spec) {
    g_clear_pointer (&priv->ctx, gst_eil_context_unref);
    return FALSE;
  }

  return TRUE;
}

static void
close_eil_context (GstLcevcEncoder * eil)
{
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);

  /* Flush */
  EIL_Encode (priv->ctx->context, NULL);

  /* Clear properties spec */
  g_clear_pointer (&priv->plugin_props_spec, g_hash_table_unref);

  /* Clear context */
  g_clear_pointer (&priv->ctx, gst_eil_context_unref);
}

static gboolean
gst_lcevc_encoder_start (GstVideoEncoder * encoder)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (encoder);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);

  /* Open EIL context */
  if (!open_eil_context (eil)) {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT, (NULL),
        ("Couldn't initialize EIL context"));
    return FALSE;
  }

  /* Reset out TS offset */
  priv->out_ts_offset = 0;

  return TRUE;
}

static gboolean
gst_lcevc_encoder_stop (GstVideoEncoder * encoder)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (encoder);

  /* Close EIL context */
  close_eil_context (eil);

  return TRUE;
}

static gboolean
try_parse_number (const char *value, double *parsed)
{
  char *endptr;

  /* Skip leading spaces */
  while (g_ascii_isspace (*value))
    value++;

  /* Parse number */
  *parsed = g_strtod (value, &endptr);

  /* Allow trailing spaces */
  while (g_ascii_isspace (*endptr))
    value++;

  /* Ceck no extra characters after number and spaces */
  if (*endptr != '\0')
    return FALSE;

  return TRUE;
}

static GString *
build_json_props (GstLcevcEncoder * eil)
{
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  gchar *key_value;
  GString *res = g_string_new ("{");

  /* I/O props */
  if (priv->sei_lcevc) {
    /* separate_output=false */
    g_string_append_printf (res, "\"%s\"", "separate_output");
    g_string_append (res, ":");
    g_string_append (res, "false");
  } else {
    /* separate_output=true */
    g_string_append_printf (res, "\"%s\"", "separate_output");
    g_string_append (res, ": ");
    g_string_append (res, "true");
    g_string_append (res, ", ");
    /* output_format=raw */
    g_string_append_printf (res, "\"%s\"", "output_format");
    g_string_append (res, ": ");
    g_string_append_printf (res, "\"%s\"", "raw");
  }

  if (!priv->plugin_props)
    goto done;

  /* Plugin props */
  key_value = strtok (priv->plugin_props, ";");
  while (key_value != NULL) {
    const gchar *val_str = strchr (key_value, '=');
    if (val_str) {
      gsize key_size = val_str - key_value;
      if (key_size > 0) {
        gchar *key = g_strndup (key_value, key_size);
        gpointer p = g_hash_table_lookup (priv->plugin_props_spec, key);

        /* Add key */
        g_string_append (res, ", ");
        g_string_append_printf (res, "\"%s\"", key);
        g_string_append (res, ": ");

        /* Convert value to type defined by spec and add it, otherwise add the
         * value as it is */
        if (p) {
          EILPropertyType spec = GPOINTER_TO_INT (p);

          switch (spec) {
            case EIL_PT_Int8:
            case EIL_PT_Int16:
            case EIL_PT_Int32:
            case EIL_PT_Int64:{
              gint64 val = g_ascii_strtoll (val_str + 1, NULL, 10);
              g_string_append_printf (res, "%ld", val);
              break;
            }
            case EIL_PT_Uint8:
            case EIL_PT_Uint16:
            case EIL_PT_Uint32:
            case EIL_PT_Uint64:{
              guint64 val = g_ascii_strtoull (val_str + 1, NULL, 10);
              g_string_append_printf (res, "%lu", val);
              break;
            }
            case EIL_PT_Float:
            case EIL_PT_Double:{
              double val = g_ascii_strtod (val_str + 1, NULL);
              g_string_append_printf (res, "%f", val);
              break;
            }
            case EIL_PT_Boolean:{
              if (g_str_equal (val_str + 1, "TRUE") ||
                  g_str_equal (val_str + 1, "True") ||
                  g_str_equal (val_str + 1, "true") ||
                  g_str_equal (val_str + 1, "1"))
                g_string_append (res, "true");
              else
                g_string_append (res, "false");
              break;
            }
            case EIL_PT_String:
            default:
              g_string_append_printf (res, "\"%s\"", val_str + 1);
              break;
          }
        } else {
          double val;
          if (try_parse_number (val_str + 1, &val)) {
            if (val == ceil (val))
              g_string_append_printf (res, "%d", (gint) val);
            else
              g_string_append_printf (res, "%f", val);
          } else {
            g_string_append_printf (res, "\"%s\"", val_str + 1);
          }
        }

        g_free (key);
      } else {
        GST_WARNING_OBJECT (eil, "Key value pair %s does not have key",
            key_value);
        goto error;
      }
    } else {
      GST_WARNING_OBJECT (eil, "Key value pair %s does not have '=' char",
          key_value);
      goto error;
    }

    key_value = strtok (NULL, ";");
  }

done:
  res = g_string_append (res, "}");
  return res;

error:
  g_string_free (res, TRUE);
  return NULL;
}

static void
on_encoded_output (void *data, EILOutput * output)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (data);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  GstVideoCodecFrame *frame;
  GstClockTime pts;

  if (!output) {
    GST_INFO_OBJECT (eil, "All EIL Pictures processed");
    return;
  }

  frame = output->user_data;
  pts = frame->input_buffer->pts;

  GST_INFO_OBJECT (eil, "Received output frame %ld with lcevc size %d", pts,
      output->lcevc_length);

  /* The EIL DTS can be negative, we need to do the conversion so it can be
   * stored in a GstClockTime (guint64). The EIL PTS can never be negative
   * because it is set using the input buffer PTS, which is a GstClockTime. */
  if (output->dts < 0 && priv->out_ts_offset == 0) {
    priv->out_ts_offset = -1 * output->dts;
    GST_INFO_OBJECT (eil, "Output DTS offset set to %ld", priv->out_ts_offset);
  }

  /* Created output buffer with output data */
  frame->output_buffer = gst_buffer_new_wrapped_full (0,
      (gpointer) output->data, output->data_length, 0, output->data_length,
      output_data_new (priv->ctx, output), output_data_free);
  frame->pts = priv->out_ts_offset + output->pts;
  frame->dts = priv->out_ts_offset + output->dts;

  /* Add LCEVC metadata to output buffer if present */
  if (output->lcevc_length > 0) {
    GstBuffer *lcevc_data = gst_buffer_new_memdup ((gpointer) output->lcevc,
        output->lcevc_length);
    gst_buffer_add_lcevc_meta (frame->output_buffer, lcevc_data);
    gst_buffer_unref (lcevc_data);
  }

  /* Set Delta unit flag if this is not a key frame */
  if (!output->keyframe)
    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

  gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (eil), frame);
}

static void
gst_lcevc_encoder_set_latency (GstLcevcEncoder * eil, GstVideoInfo * info)
{
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  gint delayed_frames;
  GstClockTime latency;

  /* The GOP affects the number of delayed frames */
  if (priv->gop_length == -2 || priv->gop_length == -1)
    delayed_frames = MAX_DELAYED_FRAMES;
  else
    delayed_frames = MIN (5 + priv->gop_length, MAX_DELAYED_FRAMES);

  latency =
      gst_util_uint64_scale_ceil (GST_SECOND * GST_VIDEO_INFO_FPS_D (info),
      delayed_frames, GST_VIDEO_INFO_FPS_N (info));
  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (eil), latency, latency);

  GST_INFO_OBJECT (eil, "Updated latency to %" GST_TIME_FORMAT " (%d frames)",
      GST_TIME_ARGS (latency), delayed_frames);
}

static gboolean
gst_lcevc_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (encoder);
  GstLcevcEncoderClass *klass = GST_LCEVC_ENCODER_GET_CLASS (eil);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  EILInitSettings settings;
  GString *properties_json;
  GstVideoInterlaceMode interlace_mode;
  EILReturnCode rc;
  GstCaps *outcaps;
  GstVideoCodecState *s;
  gint width = GST_VIDEO_INFO_WIDTH (&state->info);
  gint height = GST_VIDEO_INFO_HEIGHT (&state->info);

  /* Set input info, format and frame type */
  priv->in_info = state->info;
  priv->in_format =
      gst_lcevc_encoder_utils_get_color_format (GST_VIDEO_INFO_FORMAT
      (&state->info));
  interlace_mode = GST_VIDEO_INFO_INTERLACE_MODE (&state->info);
  switch (interlace_mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
      priv->in_frame_type = EIL_FrameType_Progressive;
      break;
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      priv->in_frame_type = EIL_FrameType_Interlaced;
      break;
    case GST_VIDEO_INTERLACE_MODE_FIELDS:
      priv->in_frame_type = EIL_FrameType_Field;
      break;
    default:
      GST_ELEMENT_ERROR (eil, STREAM, FORMAT, (NULL),
          ("Interlace mode %s not supported",
              gst_video_interlace_mode_to_string (interlace_mode)));
      return FALSE;
  }

  /* Init EIL Settings to default values */
  rc = EIL_InitSettingsDefault (&settings);
  if (rc != EIL_RC_Success) {
    GST_ELEMENT_ERROR (eil, LIBRARY, INIT, (NULL),
        ("Unabled to initialize EIL Settings"));
    return FALSE;
  }

  /* Set basic EIL settings */
  settings.color_format = priv->in_format;
  settings.memory_type = EIL_MT_Host;
  settings.width = width;
  settings.height = height;
  settings.fps_num = GST_VIDEO_INFO_FPS_N (&state->info);
  settings.fps_denom = GST_VIDEO_INFO_FPS_D (&state->info);
  settings.bitrate = priv->bitrate;
  settings.gop_length = priv->gop_length;
  settings.external_input = 1;

  /* Set properties JSON EIL setting */
  properties_json = build_json_props (eil);
  if (!properties_json) {
    GST_ELEMENT_ERROR (eil, RESOURCE, SETTINGS, (NULL),
        ("Could not parse plugin properties to JSON"));
    return FALSE;
  }
  settings.properties_json = properties_json->str;
  GST_INFO_OBJECT (eil, "Properties JSON: %s", properties_json->str);

  /* Initialize EIL */
  rc = EIL_Initialise (priv->ctx->context, &settings);
  g_string_free (properties_json, TRUE);
  if (rc != EIL_RC_Success)
    return FALSE;

  /* Get output caps */
  g_assert (klass->get_output_caps);
  outcaps = klass->get_output_caps (eil);
  if (!outcaps) {
    GST_ELEMENT_ERROR (eil, RESOURCE, NOT_FOUND, (NULL),
        ("Could not get output caps"));
    return FALSE;
  }

  /* Set width, height and pixel aspect ration.
   * The values from settings are updated to base width and height after
   * initialization.
   */
  if (width != settings.width || height != settings.height) {
    GST_VIDEO_INFO_WIDTH (&state->info) = settings.width;
    GST_VIDEO_INFO_HEIGHT (&state->info) = settings.height;
    /* If changed, the new width and height values are always half of what they
     * used to be, so update the pixel aspect ratio accordingly */
    GST_VIDEO_INFO_PAR_N (&state->info) = width > settings.width ? 2 : 1;
    GST_VIDEO_INFO_PAR_D (&state->info) = height > settings.height ? 2 : 1;
    GST_INFO_OBJECT (eil,
        "Base resolution changed: w=%d h=%d -> w=%d h=%d (par_n=%d, par_d=%d)",
        width, height, settings.width, settings.height,
        GST_VIDEO_INFO_PAR_N (&state->info),
        GST_VIDEO_INFO_PAR_D (&state->info));
  }

  /* Set output state */
  s = gst_video_encoder_set_output_state (encoder, outcaps, state);
  if (!s) {
    GST_ELEMENT_ERROR (eil, STREAM, FORMAT, (NULL),
        ("Could not set output state"));
    return FALSE;
  }

  /* Set output callback */
  EIL_SetOnEncodedCallback (priv->ctx->context, eil, on_encoded_output);

  /* Update latency */
  gst_lcevc_encoder_set_latency (eil, &s->info);

  gst_video_codec_state_unref (s);
  return TRUE;
}

static gboolean
gst_lcevc_encoder_sink_event (GstVideoEncoder * encoder, GstEvent * event)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (encoder);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* Flush on EOS */
      GST_INFO_OBJECT (eil, "EOS received, flushing encoder");
      EIL_Encode (priv->ctx->context, NULL);
      break;

    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_event (encoder, event);
}

static GstFlowReturn
gst_lcevc_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstLcevcEncoder *eil = GST_LCEVC_ENCODER (encoder);
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);
  GstClockTime pts = frame->input_buffer->pts;
  GstVideoFrame video_frame = { 0, };
  EILPicture picture;

  /* Map the input buffer */
  if (!gst_video_frame_map (&video_frame, &priv->in_info, frame->input_buffer,
          GST_MAP_READ)) {
    GST_ELEMENT_ERROR (eil, STREAM, ENCODE, (NULL),
        ("Could not map input buffer %ld", pts));
    goto error;
  }

  /* Initialize EIL picture */
  if (EIL_InitPictureDefault (&picture) != EIL_RC_Success) {
    GST_ELEMENT_ERROR (eil, STREAM, ENCODE, (NULL),
        ("Could not initialize EIL picture %ld", pts));
    goto error;
  }

  /* Set frame values on EIL picture */
  if (!gst_lcevc_encoder_utils_init_eil_picture (priv->in_frame_type,
          &video_frame, pts, &picture)) {
    GST_ELEMENT_ERROR (eil, STREAM, ENCODE, (NULL),
        ("Could not set frame values on EIL picture %ld", pts));
    goto error;
  }

  /* Set input frame as user data. This will be set in the encoded output as
   * user data, which will help us getting the associated frame */
  picture.user_data = frame;

  /* Encode frame */
  if (EIL_Encode (priv->ctx->context, &picture) != EIL_RC_Success) {
    GST_ELEMENT_ERROR (eil, STREAM, ENCODE, (NULL),
        ("Could not encode input frame %ld", pts));
    goto error;
  }

  GST_INFO_OBJECT (eil, "Sent input frame %ld", pts);

  gst_video_frame_unmap (&video_frame);
  return GST_FLOW_OK;

error:
  gst_video_frame_unmap (&video_frame);
  return GST_FLOW_ERROR;
}

static void
gst_lcevc_encoder_class_init (GstLcevcEncoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_lcevc_encoder_sink_template);

  gst_type_mark_as_plugin_api (GST_TYPE_LCEVC_ENCODER, 0);

  gobject_class->finalize = gst_lcevc_encoder_finalize;
  gobject_class->set_property = gst_lcevc_encoder_set_property;
  gobject_class->get_property = gst_lcevc_encoder_get_property;

  video_encoder_class->start = gst_lcevc_encoder_start;
  video_encoder_class->stop = gst_lcevc_encoder_stop;
  video_encoder_class->set_format = gst_lcevc_encoder_set_format;
  video_encoder_class->sink_event = gst_lcevc_encoder_sink_event;
  video_encoder_class->handle_frame = gst_lcevc_encoder_handle_frame;

  g_object_class_install_property (gobject_class, PROP_PLUGIN_NAME,
      g_param_spec_string ("plugin-name", "Plugin Name",
          "The name of the EIL plugin to use (NULL = auto)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PLUGIN_PROPS,
      g_param_spec_string ("plugin-props", "Plugin Props",
          "A semi-colon list of key value pair properties for the EIL plugin",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate in kbit/sec (0 = auto)",
          DEFAULT_MIN_BITRATE, DEFAULT_MAX_BITRATE, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SEI_LCEVC,
      g_param_spec_boolean ("sei-lcevc", "SEI LCEVC",
          "Whether to have LCEVC data as SEI (in the video stream) or not (attached to buffers as GstMeta)",
          DEFAULT_SEI_LCEVC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GOP_LENGTH,
      g_param_spec_int ("gop-length", "GOP Length",
          "The group of pictures length (-2 = auto, -1 = infinite, 0 = intra-only)",
          DEFAULT_MIN_GOP_LENGTH, INT_MAX, DEFAULT_GOP_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Whether to show EIL SDK logs or not",
          DEFAULT_DEBUG, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_lcevc_encoder_init (GstLcevcEncoder * eil)
{
  GstLcevcEncoderPrivate *priv = gst_lcevc_encoder_get_instance_private (eil);

  /* Props */
  priv->plugin_name = NULL;
  priv->plugin_props = NULL;
  priv->bitrate = DEFAULT_BITRATE;
  priv->sei_lcevc = DEFAULT_SEI_LCEVC;
  priv->gop_length = DEFAULT_GOP_LENGTH;
}
