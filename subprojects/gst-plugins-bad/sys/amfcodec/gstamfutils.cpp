/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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

#include <core/Factory.h>
#include "gstamfutils.h"
#include <gmodule.h>
#include <cstring>
#include <mutex>

/* Only for the GST_CAPS_FEATURE_MEMORY_D3D11/D3D12_MEMORY strings below;
 * this file has no other D3D dependency and builds on every platform. */
#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#ifdef HAVE_GST_D3D12
#include <gst/d3d12/gstd3d12.h>
#endif
#endif

using namespace amf;

GST_DEBUG_CATEGORY_STATIC (gst_amf_runtime_debug);

#define GST_AMF_TRACE_WRITER_ID L"GStreamer"

static AMFFactory *_factory = nullptr;
static amf_uint64 _version = 0;
static gboolean loaded = FALSE;

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category ()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once_flag;

  /* *INDENT-OFF* */
  std::call_once (once_flag, [&]() {
    cat = _gst_debug_category_new ("amfutils", 0, "amf utility functions");
  });
  /* *INDENT-ON* */

  return cat;
}

/* wchar_t's encoding differs by platform: UTF-16 on Windows, UCS-4
 * elsewhere. Returns a newly-allocated UTF-8 string, or NULL if @str is
 * NULL. */
static gchar *
gst_amf_wchar_to_utf8 (const wchar_t *str)
{
  if (!str)
    return nullptr;

#ifdef G_OS_WIN32
  return g_utf16_to_utf8 ((const gunichar2 *) str, -1, NULL, NULL, NULL);
#else
  return g_ucs4_to_utf8 ((const gunichar *) str, -1, NULL, NULL, NULL);
#endif
}

/* Forwards AMF's internal trace output into the GStreamer debug system
 * instead of dumping it to the console. The "amfruntime" category threshold
 * controls what AMF forwards to Write() at all (see
 * gst_amf_init_amf_runtime_logs()); every forwarded message is logged at
 * GST_LEVEL_DEBUG. */
/* *INDENT-OFF* */
class GstAmfTraceWriter : public AMFTraceWriter
{
public:
  virtual ~GstAmfTraceWriter () = default;

  void AMF_CDECL_CALL Write (const wchar_t * scope,
      const wchar_t * message) override
  {
    gchar *msg_str = gst_amf_wchar_to_utf8 (message);

    if (msg_str)
      g_strchomp (msg_str);

    GST_CAT_LEVEL_LOG (gst_amf_runtime_debug, GST_LEVEL_DEBUG, NULL, "%s",
        GST_STR_NULL (msg_str));

    g_free (msg_str);
  }

  void AMF_CDECL_CALL Flush () override
  {
  }
};
/* *INDENT-ON* */

static amf_int32
gst_amf_trace_level_from_gst (GstDebugLevel level)
{
  switch (level) {
    case GST_LEVEL_NONE:
      return AMF_TRACE_NOLOG;
    case GST_LEVEL_ERROR:
      return AMF_TRACE_ERROR;
    case GST_LEVEL_WARNING:
    case GST_LEVEL_FIXME:
      return AMF_TRACE_WARNING;
    case GST_LEVEL_INFO:
      return AMF_TRACE_INFO;
    case GST_LEVEL_DEBUG:
      return AMF_TRACE_DEBUG;
    case GST_LEVEL_LOG:
    case GST_LEVEL_TRACE:
      return AMF_TRACE_TRACE;
    default:
      break;
  }

  return AMF_TRACE_TRACE;
}

static void
gst_amf_init_amf_runtime_logs (void)
{
  static gsize init_once = 0;

  if (g_once_init_enter (&init_once)) {
    AMFTrace *trace = nullptr;

    GST_DEBUG_CATEGORY_INIT (gst_amf_runtime_debug, "amfruntime", 0,
        "AMF runtime trace messages");

    if (_factory && _factory->GetTrace (&trace) == AMF_OK && trace) {
      GstDebugLevel gst_level =
          gst_debug_category_get_threshold (gst_amf_runtime_debug);

      if (gst_level > GST_LEVEL_NONE) {
        static GstAmfTraceWriter writer;
        amf_int32 amf_level = gst_amf_trace_level_from_gst (gst_level);

        /* Route AMF's own logging through the GStreamer debug system
         * (category "amfruntime") rather than the console; AMF only emits
         * up to the level matching the category threshold, and each
         * message is then logged at GST_LEVEL_DEBUG. */
        trace->EnableWriter (AMF_TRACE_WRITER_CONSOLE, false);
        trace->EnableWriter (AMF_TRACE_WRITER_DEBUG_OUTPUT, false);
        trace->RegisterWriter (GST_AMF_TRACE_WRITER_ID, &writer, true);
        trace->SetWriterLevel (GST_AMF_TRACE_WRITER_ID, amf_level);
      }
    }

    g_once_init_leave (&init_once, 1);
  }
}
#else
#define gst_amf_init_amf_runtime_logs() G_STMT_START{ }G_STMT_END
#endif /* GST_DISABLE_GST_DEBUG */

static gboolean
gst_amf_load_library (void)
{
  AMF_RESULT result;
  GModule *amf_module = nullptr;
  AMFInit_Fn init_func = nullptr;
  AMFQueryVersion_Fn ver_func = nullptr;

  amf_module = g_module_open (AMF_DLL_NAMEA, G_MODULE_BIND_LAZY);
  if (!amf_module)
    return FALSE;

  if (!g_module_symbol (amf_module, AMF_INIT_FUNCTION_NAME, (gpointer *)
          & init_func)) {
    goto fail;
  }

  if (!g_module_symbol (amf_module, AMF_QUERY_VERSION_FUNCTION_NAME,
          (gpointer *) & ver_func)) {
    goto fail;
  }

  result = ver_func (&_version);
  if (result != AMF_OK) {
    goto fail;
  }

  result = init_func (AMF_FULL_VERSION, &_factory);
  if (result != AMF_OK) {
    goto fail;
  }

  gst_amf_init_amf_runtime_logs ();

  return TRUE;
fail:
  g_module_close (amf_module);
  _factory = nullptr;
  return FALSE;
}

gboolean
gst_amf_init_once (void)
{
  static gsize init_once = 0;

  if (g_once_init_enter (&init_once)) {
    loaded = gst_amf_load_library ();
    g_once_init_leave (&init_once, 1);
  }

  return loaded;
}

gpointer
gst_amf_get_factory (void)
{
  return (gpointer) _factory;
}

guint64
gst_amf_get_version (void)
{
  return (guint64) _version;
}

/* Creates a fresh, uninitialized AMFContext (amf::AMFFactory::CreateContext())
 * as a gpointer, avoiding an AMF core header dependency here -- callers cast
 * back to amf::AMFContext* */
gboolean
gst_amf_context_new (GstElement * element, gpointer * context)
{
  AMFContext *ctx = nullptr;
  AMF_RESULT result;

  if (!_factory) {
    GST_ERROR_OBJECT (element, "AMF factory is not available");
    return FALSE;
  }

  result = _factory->CreateContext (&ctx);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (element, "Failed to create AMF context, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  *context = (gpointer) ctx;
  return TRUE;
}

/**
 * GstAmfApi:
 *
 * Native graphics API the AMF context is opened with.
 *
 * Since: 1.30
 */
GType
gst_amf_api_get_type (void)
{
  static GType api_type = 0;
  static const GEnumValue api_types[] = {
    {GST_AMF_API_AUTO, "Automatic (resolved from negotiated caps)", "auto"},
#ifdef G_OS_WIN32
    {GST_AMF_API_D3D11, "Direct3D 11", "d3d11"},
#ifdef HAVE_GST_D3D12
    {GST_AMF_API_D3D12, "Direct3D 12", "d3d12"},
#endif
#endif
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&api_type)) {
    GType type = g_enum_register_static ("GstAmfApi", api_types);
    g_once_init_leave (&api_type, type);
  }

  return api_type;
}

/* Pipeline-wide (there is no per-element override) default from the
 * GST_AMF_API env var (d3d11/d3d12/auto). Falls back to
 * GST_AMF_DEFAULT_API when unset or invalid. */
GstAmfApi
gst_amf_get_default_api (void)
{
  const gchar *env = g_getenv ("GST_AMF_API");
  GEnumClass *enum_class;
  GEnumValue *val;
  GstAmfApi ret = GST_AMF_DEFAULT_API;

  if (!env)
    return ret;

  enum_class = (GEnumClass *) g_type_class_ref (GST_TYPE_AMF_API);
  val = g_enum_get_value_by_nick (enum_class, env);
  if (val) {
    ret = (GstAmfApi) val->value;
  } else {
    GST_WARNING ("Unknown value \"%s\" for GST_AMF_API environment "
        "variable, ignoring", env);
  }
  g_type_class_unref (enum_class);

  return ret;
}

/* Like gst_amf_get_default_api(), but never returns AUTO -- "auto" or an
 * unset env var resolve to GST_AMF_API_D3D11 here. Used to resolve a
 * configured AUTO to a concrete backend when nothing else (e.g.
 * negotiated caps) determines one. */
GstAmfApi
gst_amf_get_concrete_default_api (void)
{
  GstAmfApi ret = gst_amf_get_default_api ();

  if (ret == GST_AMF_API_AUTO)
    return GST_AMF_API_D3D11;

  return ret;
}

typedef struct
{
  GstAmfApi api;
  const gchar *feature;
  const gchar *nick;
} GstAmfFeatureMapEntry;

/* Maps each concrete backend to its caps memory feature and property
 * nick. */
static const GstAmfFeatureMapEntry k_amf_feature_map[] = {
#ifdef G_OS_WIN32
  {GST_AMF_API_D3D11, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, "d3d11"},
#ifdef HAVE_GST_D3D12
  {GST_AMF_API_D3D12, GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, "d3d12"},
#endif
#endif
  /* Sentinel, not a real entry: on a non-Windows build the two entries
   * above vanish under #ifdef G_OS_WIN32, and this file builds there too
   * (it's cross-platform), so without this the array would be empty --
   * not valid array-size deduction. Loops below skip it via the NULL
   * feature check rather than relying on a separate bound. */
  {GST_AMF_API_AUTO, nullptr, nullptr},
};

static const GstAmfFeatureMapEntry *
gst_amf_feature_map_find_by_api (GstAmfApi api)
{
  for (guint i = 0; i < G_N_ELEMENTS (k_amf_feature_map); i++) {
    if (!k_amf_feature_map[i].feature)
      continue;
    if (k_amf_feature_map[i].api == api)
      return &k_amf_feature_map[i];
  }
  return nullptr;
}

/* Returns the concrete backend whose feature @features carries, or
 * GST_AMF_API_AUTO if none of the known backends' features are present
 * (e.g. plain system memory, or @features is NULL). */
static GstAmfApi
gst_amf_api_for_features (GstCapsFeatures * features)
{
  if (!features)
    return GST_AMF_API_AUTO;

  for (guint i = 0; i < G_N_ELEMENTS (k_amf_feature_map); i++) {
    if (!k_amf_feature_map[i].feature)
      continue;
    if (gst_caps_features_contains (features, k_amf_feature_map[i].feature))
      return k_amf_feature_map[i].api;
  }

  return GST_AMF_API_AUTO;
}

void
gst_amf_api_state_init (GstAmfApiState * state)
{
  state->configured = gst_amf_get_default_api ();
  gst_amf_resolve_active_api (state);
}

/* Recomputes @state->active from @state->configured (AUTO resolves via
 * gst_amf_get_concrete_default_api()). The "no caps yet" resolution --
 * call from a property setter; see gst_amf_resolve_active_api_from_caps()
 * for the caps-aware version used to actually open a backend. */
void
gst_amf_resolve_active_api (GstAmfApiState * state)
{
  state->active = (state->configured == GST_AMF_API_AUTO) ?
      gst_amf_get_concrete_default_api () : state->configured;
}

/* Like gst_amf_resolve_active_api(), but resolves AUTO from @caps's
 * negotiated memory feature instead of the concrete default (falls back
 * to it if @caps has no known feature, or is NULL). Call from
 * set_caps()/set_format() once caps are known, before opening the
 * backend context. */
void
gst_amf_resolve_active_api_from_caps (GstAmfApiState * state, GstCaps * caps)
{
  GstAmfApi from_caps;

  if (state->configured != GST_AMF_API_AUTO) {
    state->active = state->configured;
    return;
  }

  from_caps =
      gst_amf_api_for_features (caps ? gst_caps_get_features (caps,
          0) : nullptr);

  state->active = (from_caps != GST_AMF_API_AUTO) ?
      from_caps : gst_amf_get_concrete_default_api ();
}

/* Warns when @in_caps/@out_caps (either nullable) negotiated a different
 * backend's memory feature than @state->active -- that combination
 * silently defeats zero-copy with nothing else logging why. */
void
gst_amf_check_caps_api_mismatch (GstElement * element, GstAmfApiState * state,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstAmfApi in_api =
      gst_amf_api_for_features (in_caps ? gst_caps_get_features (in_caps,
          0) : nullptr);
  GstAmfApi out_api =
      gst_amf_api_for_features (out_caps ? gst_caps_get_features (out_caps,
          0) : nullptr);
  GstAmfApi mismatched_api = GST_AMF_API_AUTO;

  if (in_api != GST_AMF_API_AUTO && in_api != state->active)
    mismatched_api = in_api;
  else if (out_api != GST_AMF_API_AUTO && out_api != state->active)
    mismatched_api = out_api;

  if (mismatched_api != GST_AMF_API_AUTO) {
    const GstAmfFeatureMapEntry *mismatched_entry =
        gst_amf_feature_map_find_by_api (mismatched_api);
    const GstAmfFeatureMapEntry *active_entry =
        gst_amf_feature_map_find_by_api (state->active);

    GST_WARNING_OBJECT (element, "Negotiated caps use %s memory but api=%s "
        "is active (incaps %" GST_PTR_FORMAT ", outcaps %" GST_PTR_FORMAT
        "); zero-copy is unavailable on the mismatched pad(s) and every "
        "buffer will go through a host-memory copy instead. Set api to "
        "match, or let upstream/downstream negotiate the matching memory "
        "feature.", mismatched_entry ? mismatched_entry->feature : "?",
        active_entry ? active_entry->nick : "?", in_caps, out_caps);
  }
}

/* Drops caps structures carrying a different known backend's memory
 * feature than @api, so negotiation can never settle on a mismatch (see
 * gst_amf_check_caps_api_mismatch(), which only detects it after the
 * fact). @api == GST_AMF_API_AUTO is a deliberate no-op -- call from
 * transform_caps/getcaps with the *configured* api, not the resolved
 * one. */
GstCaps *
gst_amf_filter_caps_by_api (GstCaps * caps, GstAmfApi api)
{
  GstCaps *result;
  guint i, n;

  if (api == GST_AMF_API_AUTO)
    return gst_caps_ref (caps);

  result = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstCapsFeatures *features = gst_caps_get_features (caps, i);
    GstAmfApi structure_api = gst_amf_api_for_features (features);

    if (structure_api != GST_AMF_API_AUTO && structure_api != api)
      continue;

    gst_caps_append_structure_full (result,
        gst_structure_copy (gst_caps_get_structure (caps, i)),
        features ? gst_caps_features_copy (features) : nullptr);
  }

  return result;
}

const gchar *
gst_amf_result_to_string (AMF_RESULT result)
{
#define CASE(err) \
    case err: \
    return G_STRINGIFY (err);

  switch (result) {
      CASE (AMF_OK);
      CASE (AMF_FAIL);
      CASE (AMF_UNEXPECTED);
      CASE (AMF_ACCESS_DENIED);
      CASE (AMF_INVALID_ARG);
      CASE (AMF_OUT_OF_RANGE);
      CASE (AMF_OUT_OF_MEMORY);
      CASE (AMF_INVALID_POINTER);
      CASE (AMF_NO_INTERFACE);
      CASE (AMF_NOT_IMPLEMENTED);
      CASE (AMF_NOT_SUPPORTED);
      CASE (AMF_NOT_FOUND);
      CASE (AMF_ALREADY_INITIALIZED);
      CASE (AMF_NOT_INITIALIZED);
      CASE (AMF_INVALID_FORMAT);
      CASE (AMF_WRONG_STATE);
      CASE (AMF_FILE_NOT_OPEN);
      CASE (AMF_NO_DEVICE);
      CASE (AMF_DIRECTX_FAILED);
      CASE (AMF_OPENCL_FAILED);
      CASE (AMF_GLX_FAILED);
      CASE (AMF_XV_FAILED);
      CASE (AMF_ALSA_FAILED);
      CASE (AMF_EOF);
      CASE (AMF_REPEAT);
      CASE (AMF_INPUT_FULL);
      CASE (AMF_RESOLUTION_CHANGED);
      CASE (AMF_RESOLUTION_UPDATED);
      CASE (AMF_INVALID_DATA_TYPE);
      CASE (AMF_INVALID_RESOLUTION);
      CASE (AMF_CODEC_NOT_SUPPORTED);
      CASE (AMF_SURFACE_FORMAT_NOT_SUPPORTED);
      CASE (AMF_SURFACE_MUST_BE_SHARED);
      CASE (AMF_DECODER_NOT_PRESENT);
      CASE (AMF_DECODER_SURFACE_ALLOCATION_FAILED);
      CASE (AMF_DECODER_NO_FREE_SURFACES);
      CASE (AMF_ENCODER_NOT_PRESENT);
      CASE (AMF_DEM_ERROR);
      CASE (AMF_DEM_PROPERTY_READONLY);
      CASE (AMF_DEM_REMOTE_DISPLAY_CREATE_FAILED);
      CASE (AMF_DEM_START_ENCODING_FAILED);
      CASE (AMF_DEM_QUERY_OUTPUT_FAILED);
      CASE (AMF_TAN_CLIPPING_WAS_REQUIRED);
      CASE (AMF_TAN_UNSUPPORTED_VERSION);
      CASE (AMF_NEED_MORE_INPUT);
    default:
      break;
  }
#undef CASE
  return "Unknown";
}
