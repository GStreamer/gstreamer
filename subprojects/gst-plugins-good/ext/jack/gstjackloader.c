/* GStreamer
 * Copyright (C) 2023 Jordan Petridis <jordan@centricular.com>
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

#include "gstjackloader.h"
#include <stdio.h>
#include <gmodule.h>

#ifdef __APPLE__
#define JACK_LIBNAME "libjack.0.dylib"
#elif defined(G_OS_WIN32)
#ifdef _WIN64
#define JACK_LIBNAME "libjack64.dll"
#else
#define JACK_LIBNAME "libjack.dll"
#endif /* End ifdef _WIN64 */
#else /* End ifdef G_OS_WIN32 */
#define JACK_LIBNAME "libjack.so.0"
#endif

#define LOAD_SYMBOL(name,func,mandatory) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    if (mandatory) { \
      GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
      goto error; \
    } \
    GST_WARNING ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
  } \
} G_STMT_END;

typedef struct _GstJackVTable
{
  gboolean loaded;

  gint major_version;
  gint minor_version;
  gint micro_version;

  const char *(*GstJackGetVersionString) (void);

  jack_client_t *(*GstJackClientOpen) (const char *client_name,
      jack_options_t options, jack_status_t * status, ...);

  jack_client_t *(*GstJackClientNew) (const char *client_name);

  int (*GstJackClientClose) (jack_client_t * client);

  int (*GstJackActivate) (jack_client_t * client);

  int (*GstJackDeactivate) (jack_client_t * client);

  void (*GstJackOnShutdown) (jack_client_t * client,
      JackShutdownCallback shutdown_callback, void *arg);

  int (*GstJackSetProcessCallback) (jack_client_t * client,
      JackProcessCallback process_callback, void *arg);


  int (*GstJackSetBufferSizeCallback) (jack_client_t * client,
      JackBufferSizeCallback bufsize_callback, void *arg);

  int (*GstJackSetSampleRateCallback) (jack_client_t * client,
      JackSampleRateCallback srate_callback, void *arg);

  int (*GstJackSetBufferSize) (jack_client_t * client, jack_nframes_t nframes);

    jack_nframes_t (*GstJackGetSampleRate) (jack_client_t *);

    jack_nframes_t (*GstJackGetBufferSize) (jack_client_t *);

  jack_port_t *(*GstJackPortRegister) (jack_client_t * client,
      const char *port_name,
      const char *port_type, unsigned long flags, unsigned long buffer_size);

  int (*GstJackPortUnregister) (jack_client_t * client, jack_port_t * port);

  void *(*GstJackPortGetBuffer) (jack_port_t * port, jack_nframes_t nframes);

  const char *(*GstJackPortName) (const jack_port_t * port);

  int (*GstJackPortFlags) (const jack_port_t * port);

  int (*GstJackConnect) (jack_client_t * client,
      const char *source_port, const char *destination_port);

  void (*GstJackPortGetLatencyRange) (jack_port_t * port,
      jack_latency_callback_mode_t mode, jack_latency_range_t * range);

  const char **(*GstJackGetPorts) (jack_client_t * client,
      const char *port_name_pattern,
      const char *type_name_pattern, unsigned long flags);

  jack_port_t *(*GstJackPortByName) (jack_client_t * client,
      const char *port_name);

  void (*GstJackSetErrorFunction) (void (*func) (const char *));

  void (*GstJackSetInfoFunction) (void (*func) (const char *));

  void (*GstJackFree) (void *ptr);

  void (*GstJackTransportStart) (jack_client_t * client);

  void (*GstJackTransportStop) (jack_client_t * client);

    jack_transport_state_t (*GstJackTransportQuery) (const jack_client_t *
      client, jack_position_t * pos);

} GstJackVTable;

static GstJackVTable gst_jack_vtable = { 0, };

static const char *
gst_jack_get_version_string (void)
{
  g_assert (gst_jack_vtable.GstJackGetVersionString != NULL);

  const char *ret = gst_jack_vtable.GstJackGetVersionString ();
  g_assert (ret != NULL);
  return ret;
}

static gboolean
gst_jack_check_api_version (void)
{
  /* hardcoded minimum supported version */
  gint supported_major_ver = 1;
  gint minimum_minor_ver = 9;
  gint minimum_micro_ver = 7;

  const char *jack_version = gst_jack_get_version_string ();

  if (jack_version == NULL || *jack_version == '\0') {
    GST_ERROR ("No JACK version string");
    return FALSE;
  }

  GST_INFO ("Checking JACK client library version: %s", jack_version);

  if (strstr (jack_version, "PipeWire")) {
    GST_INFO ("Using Pipewire as the Jack server: %s", jack_version);
  } else {
    int major, minor, micro;
    if (sscanf (jack_version, "%u.%u.%u", &major, &minor, &micro) == 3 &&
        major == supported_major_ver && ((minor == minimum_minor_ver
                && micro >= minimum_micro_ver) || minor > minimum_minor_ver)) {
      GST_INFO ("Compatible Jack Server version: %s", jack_version);
    } else {
      GST_ERROR ("Unsupported Jack version: %s", jack_version);
    }
  }

  return TRUE;
}

gboolean
gst_jack_load_library (void)
{
  GModule *module;
  const gchar *filename = JACK_LIBNAME;
  GstJackVTable *vtable;

  if (gst_jack_vtable.loaded)
    return TRUE;

  module = g_module_open (filename, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    return FALSE;
  }

  vtable = &gst_jack_vtable;
  LOAD_SYMBOL (jack_get_version_string, GstJackGetVersionString, TRUE);

  if (!gst_jack_check_api_version ())
    goto error;

  LOAD_SYMBOL (jack_client_open, GstJackClientOpen, TRUE);
  LOAD_SYMBOL (jack_client_new, GstJackClientNew, TRUE);
  LOAD_SYMBOL (jack_client_close, GstJackClientClose, TRUE);
  LOAD_SYMBOL (jack_activate, GstJackActivate, TRUE);
  LOAD_SYMBOL (jack_deactivate, GstJackDeactivate, TRUE);
  LOAD_SYMBOL (jack_on_shutdown, GstJackOnShutdown, TRUE);
  LOAD_SYMBOL (jack_set_process_callback, GstJackSetProcessCallback, TRUE);
  LOAD_SYMBOL (jack_set_buffer_size_callback, GstJackSetBufferSizeCallback,
      TRUE);
  LOAD_SYMBOL (jack_set_sample_rate_callback, GstJackSetSampleRateCallback,
      TRUE);
  LOAD_SYMBOL (jack_set_buffer_size, GstJackSetBufferSize, TRUE);
  LOAD_SYMBOL (jack_get_sample_rate, GstJackGetSampleRate, TRUE);
  LOAD_SYMBOL (jack_get_buffer_size, GstJackGetBufferSize, TRUE);
  LOAD_SYMBOL (jack_port_register, GstJackPortRegister, TRUE);
  LOAD_SYMBOL (jack_port_unregister, GstJackPortUnregister, TRUE);
  LOAD_SYMBOL (jack_port_get_buffer, GstJackPortGetBuffer, TRUE);
  LOAD_SYMBOL (jack_port_name, GstJackPortName, TRUE);
  LOAD_SYMBOL (jack_port_flags, GstJackPortFlags, TRUE);
  LOAD_SYMBOL (jack_connect, GstJackConnect, TRUE);
  LOAD_SYMBOL (jack_port_get_latency_range, GstJackPortGetLatencyRange, TRUE);
  LOAD_SYMBOL (jack_get_ports, GstJackGetPorts, TRUE);
  LOAD_SYMBOL (jack_port_by_name, GstJackPortByName, TRUE);
  LOAD_SYMBOL (jack_set_error_function, GstJackSetErrorFunction, TRUE);
  LOAD_SYMBOL (jack_set_info_function, GstJackSetInfoFunction, TRUE);
  LOAD_SYMBOL (jack_free, GstJackFree, TRUE);
  LOAD_SYMBOL (jack_transport_start, GstJackTransportStart, TRUE);
  LOAD_SYMBOL (jack_transport_stop, GstJackTransportStop, TRUE);
  LOAD_SYMBOL (jack_transport_query, GstJackTransportQuery, TRUE);

  vtable->loaded = TRUE;

  return TRUE;

error:
  g_module_close (module);

  return FALSE;
}

#define _gst_jack_client_open(client_name,options,status,...) (gst_jack_vtable.GstJackClientOpen(client_name, options, status, ##__VA_ARGS__))

jack_client_t *
gst_jack_client_open (const char *client_name,
    jack_options_t options, jack_status_t * status, ...)
{
  g_assert (gst_jack_vtable.GstJackClientOpen != NULL);

  return _gst_jack_client_open (client_name, options, status);
};

jack_client_t *
gst_jack_client_new (const char *client_name)
{
  g_assert (gst_jack_vtable.GstJackClientNew != NULL);

  return gst_jack_vtable.GstJackClientNew (client_name);
}

int
gst_jack_client_close (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackClientClose != NULL);

  return gst_jack_vtable.GstJackClientClose (client);
}

int
gst_jack_activate (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackActivate != NULL);

  return gst_jack_vtable.GstJackActivate (client);
};

int
gst_jack_deactivate (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackDeactivate != NULL);

  return gst_jack_vtable.GstJackDeactivate (client);
};


void
gst_jack_on_shutdown (jack_client_t * client,
    JackShutdownCallback shutdown_callback, void *arg)
{
  g_assert (gst_jack_vtable.GstJackOnShutdown != NULL);

  gst_jack_vtable.GstJackOnShutdown (client, shutdown_callback, arg);
};

int
gst_jack_set_process_callback (jack_client_t * client,
    JackProcessCallback process_callback, void *arg)
{
  g_assert (gst_jack_vtable.GstJackSetProcessCallback != NULL);

  return gst_jack_vtable.GstJackSetProcessCallback (client, process_callback,
      arg);
};


int
gst_jack_set_buffer_size_callback (jack_client_t * client,
    JackBufferSizeCallback bufsize_callback, void *arg)
{
  g_assert (gst_jack_vtable.GstJackSetBufferSizeCallback != NULL);

  return gst_jack_vtable.GstJackSetBufferSizeCallback (client, bufsize_callback,
      arg);
};

int
gst_jack_set_sample_rate_callback (jack_client_t * client,
    JackSampleRateCallback srate_callback, void *arg)
{
  g_assert (gst_jack_vtable.GstJackSetSampleRateCallback != NULL);

  return gst_jack_vtable.GstJackSetSampleRateCallback (client, srate_callback,
      arg);
};

int
gst_jack_set_buffer_size (jack_client_t * client, jack_nframes_t nframes)
{
  g_assert (gst_jack_vtable.GstJackSetBufferSize != NULL);

  return gst_jack_vtable.GstJackSetBufferSize (client, nframes);
};

jack_nframes_t
gst_jack_get_sample_rate (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackGetSampleRate != NULL);

  return gst_jack_vtable.GstJackGetSampleRate (client);
};

jack_nframes_t
gst_jack_get_buffer_size (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackGetBufferSize != NULL);

  return gst_jack_vtable.GstJackGetBufferSize (client);
};

jack_port_t *
gst_jack_port_register (jack_client_t * client,
    const char *port_name,
    const char *port_type, unsigned long flags, unsigned long buffer_size)
{
  g_assert (gst_jack_vtable.GstJackPortRegister != NULL);

  return gst_jack_vtable.GstJackPortRegister (client, port_name, port_type,
      flags, buffer_size);

};

int
gst_jack_port_unregister (jack_client_t * client, jack_port_t * port)
{
  g_assert (gst_jack_vtable.GstJackPortUnregister != NULL);

  return gst_jack_vtable.GstJackPortUnregister (client, port);
};

void *
gst_jack_port_get_buffer (jack_port_t * port, jack_nframes_t nframes)
{
  g_assert (gst_jack_vtable.GstJackPortGetBuffer != NULL);

  return gst_jack_vtable.GstJackPortGetBuffer (port, nframes);
};

const char *
gst_jack_port_name (const jack_port_t * port)
{
  g_assert (gst_jack_vtable.GstJackPortName != NULL);

  return gst_jack_vtable.GstJackPortName (port);
};

int
gst_jack_port_flags (const jack_port_t * port)
{
  g_assert (gst_jack_vtable.GstJackPortFlags != NULL);

  return gst_jack_vtable.GstJackPortFlags (port);
};

int
gst_jack_connect (jack_client_t * client,
    const char *source_port, const char *destination_port)
{
  g_assert (gst_jack_vtable.GstJackConnect != NULL);

  return gst_jack_vtable.GstJackConnect (client, source_port, destination_port);
};

void
gst_jack_port_get_latency_range (jack_port_t * port,
    jack_latency_callback_mode_t mode, jack_latency_range_t * range)
{
  g_assert (gst_jack_vtable.GstJackPortGetLatencyRange != NULL);

  gst_jack_vtable.GstJackPortGetLatencyRange (port, mode, range);
};

const char **
gst_jack_get_ports (jack_client_t * client,
    const char *port_name_pattern,
    const char *type_name_pattern, unsigned long flags)
{
  g_assert (gst_jack_vtable.GstJackGetPorts != NULL);

  return gst_jack_vtable.GstJackGetPorts (client, port_name_pattern,
      type_name_pattern, flags);
};

jack_port_t *
gst_jack_port_by_name (jack_client_t * client, const char *port_name)
{
  g_assert (gst_jack_vtable.GstJackPortByName != NULL);

  return gst_jack_vtable.GstJackPortByName (client, port_name);
};

void
gst_jack_set_error_function (void (*func) (const char *))
{
  g_assert (gst_jack_vtable.GstJackSetErrorFunction != NULL);

  gst_jack_vtable.GstJackSetErrorFunction (func);
};

void
gst_jack_set_info_function (void (*func) (const char *))
{
  g_assert (gst_jack_vtable.GstJackSetInfoFunction != NULL);

  gst_jack_vtable.GstJackSetInfoFunction (func);
};

void
gst_jack_free (void *ptr)
{
  g_assert (gst_jack_vtable.GstJackFree != NULL);

  gst_jack_vtable.GstJackFree (ptr);
}

void
gst_jack_transport_start (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackTransportStart != NULL);

  gst_jack_vtable.GstJackTransportStart (client);
};

void
gst_jack_transport_stop (jack_client_t * client)
{
  g_assert (gst_jack_vtable.GstJackTransportStop != NULL);

  gst_jack_vtable.GstJackTransportStop (client);
};

jack_transport_state_t
gst_jack_transport_query (const jack_client_t * client, jack_position_t * pos)
{
  g_assert (gst_jack_vtable.GstJackTransportQuery != NULL);

  return gst_jack_vtable.GstJackTransportQuery (client, pos);
};
