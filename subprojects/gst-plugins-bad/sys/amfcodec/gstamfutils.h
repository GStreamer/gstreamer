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

#pragma once

#include <gst/gst.h>
#include <core/Result.h>

G_BEGIN_DECLS

/**
 * GstAmfApi:
 * @GST_AMF_API_AUTO: Resolved from negotiated caps at set_caps()/
 *   set_format() time; falls back to a concrete default (see
 *   gst_amf_get_concrete_default_api()) when caps carry no D3D memory
 *   feature hint (e.g. plain system memory).
 * @GST_AMF_API_D3D11: Direct3D 11
 * @GST_AMF_API_D3D12: Direct3D 12
 *
 * Native graphics API the AMF context is opened with. Exactly one
 * concrete backend (D3D11 or D3D12 -- never AUTO itself) is active per
 * element instance once resolved.
 *
 * Since: 1.30
 */
typedef enum {
  GST_AMF_API_AUTO,
  GST_AMF_API_D3D11,
  GST_AMF_API_D3D12,
} GstAmfApi;

#define GST_TYPE_AMF_API (gst_amf_api_get_type ())
GType gst_amf_api_get_type (void);

#define GST_AMF_DEFAULT_API GST_AMF_API_AUTO

GstAmfApi     gst_amf_get_default_api (void);

GstAmfApi     gst_amf_get_concrete_default_api (void);

/* Configured (raw, from the GST_AMF_API env var, may be AUTO) vs.
 * resolved (concrete, never AUTO) api state. Embed in a backend-specific
 * private struct; see gstamfutils.cpp for the functions that operate on
 * it. */
typedef struct _GstAmfApiState
{
  /* Raw configured api, from gst_amf_get_default_api() (the GST_AMF_API
   * env var); may be GST_AMF_API_AUTO. Pipeline-wide -- there is no
   * per-element override. */
  GstAmfApi configured;
  /* The concrete backend actually in use -- never AUTO. Equal to
   * configured when that's already concrete; resolved separately when
   * configured is AUTO. All internal dispatch should read this field,
   * never configured. */
  GstAmfApi active;
} GstAmfApiState;

void gst_amf_api_state_init (GstAmfApiState * state);

void gst_amf_resolve_active_api (GstAmfApiState * state);

void gst_amf_resolve_active_api_from_caps (GstAmfApiState * state,
    GstCaps * caps);

gboolean      gst_amf_init_once (void);

gpointer      gst_amf_get_factory (void);

guint64       gst_amf_get_version (void);

gboolean      gst_amf_context_new (GstElement * element, gpointer * context);

const gchar * gst_amf_result_to_string (AMF_RESULT result);
#define GST_AMF_RESULT_FORMAT "s (%d)"
#define GST_AMF_RESULT_ARGS(r) gst_amf_result_to_string (r), r

G_END_DECLS
