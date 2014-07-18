/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerutils.c: tracing subsystem
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

/**
 * SECTION:gsttracerutils
 * @short_description: Tracing subsystem
 *
 * The tracing subsystem provides hooks in the core library and API for modules
 * to attach to them.
 *
 * The user can activate tracers by setting the environment variable GST_TRACE
 * to a ';' separated list of tracers.
 */

#define GST_USE_UNSTABLE_API

#include "gst_private.h"
#include "gsttracer.h"
#include "gsttracerfactory.h"
#include "gsttracerutils.h"

#ifndef GST_DISABLE_GST_DEBUG

/* tracing helpers */

gboolean _priv_tracer_enabled = FALSE;
/* TODO(ensonic): use array of GPtrArray* ? */
GList *_priv_tracers[GST_TRACER_HOOK_ID_LAST] = { NULL, };

/* Initialize the tracing system */
void
_priv_gst_tracer_init (void)
{
  const gchar *env = g_getenv ("GST_TRACE");

  if (env != NULL && *env != '\0') {
    GstRegistry *registry = gst_registry_get ();
    GstPluginFeature *feature;
    GstTracerFactory *factory;
    GstTracerHook mask;
    GstObject *tracer;
    gchar **t = g_strsplit_set (env, ";", 0);
    gint i = 0, j;
    gchar *params;

    GST_INFO ("enabling tracers: '%s'", env);

    while (t[i]) {
      // check t[i] for params
      if ((params = strchr (t[i], '('))) {
        gchar *end = strchr (&params[1], ')');
        *params = '\0';
        params++;
        if (end)
          *end = '\0';
      } else {
        params = NULL;
      }

      GST_INFO ("checking tracer: '%s'", t[i]);

      if ((feature = gst_registry_lookup_feature (registry, t[i]))) {
        factory = GST_TRACER_FACTORY (gst_plugin_feature_load (feature));
        if (factory) {
          GST_INFO_OBJECT (factory, "creating tracer: type-id=%u",
              (guint) factory->type);

          tracer = g_object_new (factory->type, "params", params, NULL);
          g_object_get (tracer, "mask", &mask, NULL);

          if (mask) {
            /* add to lists according to mask */
            j = 0;
            while (mask && (j < GST_TRACER_HOOK_ID_LAST)) {
              if (mask & 1) {
                _priv_tracers[j] = g_list_prepend (_priv_tracers[j],
                    gst_object_ref (tracer));
                GST_WARNING_OBJECT (tracer, "added tracer to hook %d", j);
              }
              mask >>= 1;
              j++;
            }

            _priv_tracer_enabled = TRUE;
          } else {
            GST_WARNING_OBJECT (tracer,
                "tracer with zero mask won't have any effect");
          }
          gst_object_unref (tracer);
        } else {
          GST_WARNING_OBJECT (feature,
              "loading plugin containing feature %s failed!", t[i]);
        }
      } else {
        GST_WARNING ("no tracer named '%s'", t[i]);
      }
      i++;
    }
    g_strfreev (t);
  }
}

void
_priv_gst_tracer_deinit (void)
{
  gint i;
  GList *node;

  /* shutdown tracers for final reports */
  for (i = 0; i < GST_TRACER_HOOK_ID_LAST; i++) {
    for (node = _priv_tracers[i]; node; node = g_list_next (node)) {
      gst_object_unref (node->data);
    }
    g_list_free (_priv_tracers[i]);
    _priv_tracers[i] = NULL;
  }
  _priv_tracer_enabled = FALSE;
}

void
gst_tracer_dispatch (GstTracerHookId hid, GstTracerMessageId mid, ...)
{
  va_list var_args;
  GList *node;

  for (node = _priv_tracers[hid]; node; node = g_list_next (node)) {
    va_start (var_args, mid);
    gst_tracer_invoke (node->data, hid, mid, var_args);
    va_end (var_args);
  }
}

#endif /* GST_DISABLE_GST_DEBUG */
