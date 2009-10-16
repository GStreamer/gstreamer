/*-*- Mode: C; c-basic-offset: 2 -*-*/

/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifndef __GST_PULSEPROBE_H__
#define __GST_PULSEPROBE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#include <gst/interfaces/propertyprobe.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

typedef struct _GstPulseProbe GstPulseProbe;

struct _GstPulseProbe
{
  GObject *object;
  gchar *server;

  GList *devices;
  gboolean devices_valid:1;

  gboolean operation_success:1;

  gboolean enumerate_sinks:1;
  gboolean enumerate_sources:1;

  pa_threaded_mainloop *mainloop;
  pa_context *context;

  GList *properties;
  guint prop_id;
};

GstPulseProbe *gst_pulseprobe_new (GObject *object, GObjectClass * klass,
    guint prop_id, const gchar * server, gboolean sinks, gboolean sources);
void gst_pulseprobe_free (GstPulseProbe * probe);

const GList *gst_pulseprobe_get_properties (GstPulseProbe * probe);

gboolean gst_pulseprobe_needs_probe (GstPulseProbe * probe, guint prop_id,
    const GParamSpec * pspec);
void gst_pulseprobe_probe_property (GstPulseProbe * probe, guint prop_id,
    const GParamSpec * pspec);
GValueArray *gst_pulseprobe_get_values (GstPulseProbe * probe, guint prop_id,
    const GParamSpec * pspec);

void gst_pulseprobe_set_server (GstPulseProbe * c, const gchar * server);

#define GST_IMPLEMENT_PULSEPROBE_METHODS(Type, interface_as_function)           \
static const GList*                                                             \
interface_as_function ## _get_properties(GstPropertyProbe * probe)              \
{                                                                               \
  Type *this = (Type*) probe;                                                   \
                                                                                \
  g_return_val_if_fail(this != NULL, NULL);                                     \
  g_return_val_if_fail(this->probe != NULL, NULL);                              \
                                                                                \
  return gst_pulseprobe_get_properties(this->probe);                            \
}                                                                               \
static gboolean                                                                 \
interface_as_function ## _needs_probe(GstPropertyProbe *probe, guint prop_id,   \
    const GParamSpec *pspec)                                                    \
{                                                                               \
  Type *this = (Type*) probe;                                                   \
                                                                                \
  g_return_val_if_fail(this != NULL, FALSE);                                    \
  g_return_val_if_fail(this->probe != NULL, FALSE);                             \
                                                                                \
  return gst_pulseprobe_needs_probe(this->probe, prop_id, pspec);               \
}                                                                               \
static void                                                                     \
interface_as_function ## _probe_property(GstPropertyProbe *probe,               \
    guint prop_id, const GParamSpec *pspec)                                     \
{                                                                               \
  Type *this = (Type*) probe;                                                   \
                                                                                \
  g_return_if_fail(this != NULL);                                               \
  g_return_if_fail(this->probe != NULL);                                        \
                                                                                \
  gst_pulseprobe_probe_property(this->probe, prop_id, pspec);                   \
}                                                                               \
static GValueArray*                                                             \
interface_as_function ## _get_values(GstPropertyProbe *probe, guint prop_id,    \
    const GParamSpec *pspec)                                                    \
{                                                                               \
  Type *this = (Type*) probe;                                                   \
                                                                                \
  g_return_val_if_fail(this != NULL, NULL);                                     \
  g_return_val_if_fail(this->probe != NULL, NULL);                              \
                                                                                \
  return gst_pulseprobe_get_values(this->probe, prop_id, pspec);                \
}                                                                               \
static void                                                                     \
interface_as_function ## _property_probe_interface_init(GstPropertyProbeInterface *iface)\
{                                                                               \
  iface->get_properties = interface_as_function ## _get_properties;             \
  iface->needs_probe = interface_as_function ## _needs_probe;                   \
  iface->probe_property = interface_as_function ## _probe_property;             \
  iface->get_values = interface_as_function ## _get_values;                     \
}

G_END_DECLS

#endif
