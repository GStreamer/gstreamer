/* Jack

  Jack definitions copied from:
    - jack/jack.h
    - jack/types.h
    - jack/transport.h

  Copyright (C) 2001 Paul Davis
  Copyright (C) 2003 Jack O'Quin
  Copyright (C) 2004 Jack O'Quin

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef _GST_JACK_WRAPPER_H_
#define _GST_JACK_WRAPPER_H_

#include <stdint.h>
#include <gst/gst.h>
#include "gstjackloader.h"

G_BEGIN_DECLS

typedef uint32_t                   jack_nframes_t;
typedef struct _jack_client        jack_client_t;
typedef struct _jack_port          jack_port_t;
typedef struct _jack_position      jack_position_t;
typedef struct _jack_latency_range jack_latency_range_t;

typedef void (* JackShutdownCallback)   (void *arg);
typedef int  (* JackProcessCallback)    (jack_nframes_t nframes, void *arg);
typedef int  (* JackBufferSizeCallback) (jack_nframes_t nframes, void *arg);
typedef int  (* JackSampleRateCallback) (jack_nframes_t nframes, void *arg);

#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"

struct _jack_latency_range
 {
     jack_nframes_t min;
     jack_nframes_t max;
 };

enum JackLatencyCallbackMode {
    JackCaptureLatency,
    JackPlaybackLatency
};

typedef enum JackLatencyCallbackMode jack_latency_callback_mode_t;

enum JackOptions {
     JackNullOption = 0x00,
     JackNoStartServer = 0x01,
     JackUseExactName = 0x02,
     JackServerName = 0x04,
     JackLoadName = 0x08,
     JackLoadInit = 0x10,
     JackSessionID = 0x20
};

typedef enum JackOptions jack_options_t;

enum JackStatus {
    JackFailure = 0x01,
    JackInvalidOption = 0x02,
    JackNameNotUnique = 0x04,
    JackServerStarted = 0x08,
    JackServerFailed = 0x10,
    JackServerError = 0x20,
    JackNoSuchClient = 0x40,
    JackLoadFailure = 0x80,
    JackInitFailure = 0x100,
    JackShmFailure = 0x200,
    JackVersionError = 0x400,
    /*
    * BackendError
    */
    JackBackendError = 0x800,
    /*
    * Client is being shutdown against its will
    */
    JackClientZombie = 0x1000
};

typedef enum JackStatus jack_status_t;

typedef float jack_default_audio_sample_t;

 enum JackPortFlags {
      JackPortIsInput = 0x1,
      JackPortIsOutput = 0x2,
      JackPortIsPhysical = 0x4,
      JackPortCanMonitor = 0x8,
      JackPortIsTerminal = 0x10
 };

typedef enum {
    /* the order matters for binary compatibility */
    JackTransportStopped = 0,       /* Transport halted */
    JackTransportRolling = 1,       /* Transport playing */
    JackTransportLooping = 2,       /* For OLD_TRANSPORT, now ignored */
    JackTransportStarting = 3,      /* Waiting for sync ready */
    JackTransportNetStarting = 4,   /* Waiting for sync ready on the network*/
} jack_transport_state_t;


gboolean gst_jack_load_library (void);

//
// jack/jack.h
//

jack_client_t * gst_jack_client_open          (const char *client_name,
                                               jack_options_t options,
                                               jack_status_t *status, ...);

jack_client_t * gst_jack_client_new           (const char *client_name) ;

int             gst_jack_client_close         (jack_client_t *client);

int             gst_jack_activate             (jack_client_t *client);

int             gst_jack_deactivate           (jack_client_t *client);

void            gst_jack_on_shutdown          (jack_client_t * client,
                                               JackShutdownCallback shutdown_callback,
                                               void *arg);

int             gst_jack_set_process_callback    (jack_client_t * client,
                                                  JackProcessCallback process_callback,
                                                  void *arg);


int            gst_jack_set_buffer_size_callback (jack_client_t *client,
                                                  JackBufferSizeCallback bufsize_callback,
                                                  void *arg);

int            gst_jack_set_sample_rate_callback (jack_client_t * client,
                                                  JackSampleRateCallback srate_callback, void *arg);

int gst_jack_set_buffer_size (jack_client_t * client, jack_nframes_t nframes);

jack_nframes_t gst_jack_get_sample_rate (jack_client_t * client);

jack_nframes_t gst_jack_get_buffer_size (jack_client_t * client);

jack_port_t *  gst_jack_port_register   (jack_client_t *client,
                                         const char *port_name,
                                         const char *port_type,
                                         unsigned long flags,
                                         unsigned long buffer_size);

int            gst_jack_port_unregister (jack_client_t *client, jack_port_t *port);


void * gst_jack_port_get_buffer (jack_port_t* port, jack_nframes_t nframes);

const char * gst_jack_port_name (const jack_port_t* port);

int gst_jack_port_flags (const jack_port_t*port);

int gst_jack_connect (jack_client_t * client,
                      const char *source_port,
                      const char *destination_port);

void gst_jack_port_get_latency_range (jack_port_t* port,
                                      jack_latency_callback_mode_t mode,
                                      jack_latency_range_t * range);

const char **  gst_jack_get_ports              (jack_client_t *client,
                                                const char *port_name_pattern,
                                                const char *type_name_pattern,
                                                unsigned long flags);

jack_port_t *  gst_jack_port_by_name           (jack_client_t * client,
                                                const char *port_name);

void gst_jack_set_error_function (void (*func)(const char *));

void gst_jack_set_info_function (void (*func)(const char *));

void gst_jack_free (void* ptr);

//
// jack/transport.h
//

void                   gst_jack_transport_start (jack_client_t *client);

void                   gst_jack_transport_stop  (jack_client_t *client);

jack_transport_state_t gst_jack_transport_query (const jack_client_t *client,
                                                 jack_position_t *pos);

G_END_DECLS
#endif  // _GST_JACK_UTIL_H_
