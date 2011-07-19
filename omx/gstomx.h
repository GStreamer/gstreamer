/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_H__
#define __GST_OMX_H__

#include <gst/gst.h>
#include <OMX_Core.h>
#include <OMX_Component.h>

G_BEGIN_DECLS

/* Different hacks that are required to work around
 * bugs in different OpenMAX implementations
 */
/* In the EventSettingsChanged callback use nData2 instead of nData1 for
 * the port index. Happens with Bellagio.
 */
#define GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP G_GUINT64_CONSTANT (0x0000000000000001)
/* In the EventSettingsChanged callback assume that port index 0 really
 * means port index 1. Happens with the Bellagio ffmpegdist video decoder.
 */
#define GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1 G_GUINT64_CONSTANT          (0x0000000000000002)

typedef struct _GstOMXCore GstOMXCore;
typedef struct _GstOMXPort GstOMXPort;
typedef enum _GstOMXPortDirection GstOMXPortDirection;
typedef struct _GstOMXComponent GstOMXComponent;
typedef struct _GstOMXBuffer GstOMXBuffer;

typedef enum {
  /* Everything good and the buffer is valid */
  GST_OMX_ACQUIRE_BUFFER_OK = 0,
  /* The port is flushing, exit ASAP */
  GST_OMX_ACQUIRE_BUFFER_FLUSHING,
  /* The port must be reconfigured */
  GST_OMX_ACQUIRE_BUFFER_RECONFIGURE,
  /* The port was reconfigured and the caps might have changed
   * NOTE: This is only returned a single time! */
  GST_OMX_ACQUIRE_BUFFER_RECONFIGURED,
  /* A fatal error happened */
  GST_OMX_ACQUIRE_BUFFER_ERROR
} GstOMXAcquireBufferReturn;

struct _GstOMXCore {
  /* Handle to the OpenMAX IL core shared library */
  GModule *module;

  /* Current number of users, transitions from/to 0
   * call init/deinit */
  GMutex *lock;
  gint user_count; /* LOCK */

  /* OpenMAX core library functions, protected with LOCK */
  /* FIXME: OpenMAX spec does not specify that this is required
   * but gst-openmax does it */
  OMX_ERRORTYPE (*init) (void);
  OMX_ERRORTYPE (*deinit) (void);
  OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
  OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

struct _GstOMXPort {
  GstOMXComponent *comp;
  guint32 index;

  /* Protects port_def, buffers, pending_buffers,
   * settings_changed, flushing, flushed, enabled_changed
   * and settings_cookie.
   *
   * Signalled if pending_buffers gets a
   * new buffer or flushing/flushed is set
   * to TRUE or the port is enabled/disabled
   * or the settings change or an error happens.
   *
   * Note: Always check comp->last_error before
   * waiting and after being signalled!
   *
   * Note: flushed==TRUE implies flushing==TRUE!
   *
   * Note: This lock must always be taken before
   * the component's state lock if both are needed!
   */
  GMutex *port_lock;
  GCond *port_cond;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GPtrArray *buffers; /* Contains GstOMXBuffer* */
  GQueue *pending_buffers; /* Contains GstOMXBuffer* */
  /* If TRUE we need to get the new caps of this port */
  gboolean settings_changed;
  gboolean flushing;
  gboolean flushed; /* TRUE after OMX_CommandFlush was done */
  gboolean enabled_changed; /* TRUE after OMX_Command{En,Dis}able was done */

  /* If not equal to comp->settings_cookie we need
   * to reconfigure this port */
  gint settings_cookie;
};

struct _GstOMXComponent {
  GstObject *parent;
  OMX_HANDLETYPE handle;
  GstOMXCore *core;

  guint64 hacks; /* Flags, GST_OMX_HACK_* */

  GPtrArray *ports; /* Contains GstOMXPort* */
  gint n_in_ports, n_out_ports;

  /* Protecting state, pending_state, last_error
   * and settings_cookie.
   * Signalled if one of them changes
   */
  GMutex *state_lock;
  GCond *state_cond;
  OMX_STATETYPE state;
  /* OMX_StateInvalid if no pending state */
  OMX_STATETYPE pending_state;
  /* OMX_ErrorNone usually, if different nothing will work */
  OMX_ERRORTYPE last_error;
  /* Updated whenever settings of any port are changing.
   * We always reconfigure all ports */
  gint settings_cookie;
  /* Number of output ports that must still be reconfigured.
   * If any are pending no input port will be reconfigured
   * or will accept any data and wait.
   */
  gint reconfigure_out_pending;
};

struct _GstOMXBuffer {
  GstOMXPort *port;
  OMX_BUFFERHEADERTYPE *omx_buf;

  /* TRUE if the buffer is used by the port, i.e.
   * between {Empty,Fill}ThisBuffer and the callback
   */
  gboolean used;

  /* Cookie of the settings when this buffer was allocated */
  gint settings_cookie;
};

extern GQuark     gst_omx_element_name_quark;

GKeyFile *        gst_omx_get_configuration (void);

const gchar *     gst_omx_error_to_string (OMX_ERRORTYPE err);
guint64           gst_omx_parse_hacks (gchar ** hacks);

GstOMXCore *      gst_omx_core_acquire (const gchar * filename);
void              gst_omx_core_release (GstOMXCore * core);


GstOMXComponent * gst_omx_component_new  (GstObject *parent, const gchar * core_name, const gchar * component_name, const gchar *component_role, guint64 hacks);
void              gst_omx_component_free (GstOMXComponent * comp);

OMX_ERRORTYPE     gst_omx_component_set_state (GstOMXComponent * comp, OMX_STATETYPE state);
OMX_STATETYPE     gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout);

void              gst_omx_component_set_last_error (GstOMXComponent * comp, OMX_ERRORTYPE err);
OMX_ERRORTYPE     gst_omx_component_get_last_error (GstOMXComponent * comp);
const gchar *     gst_omx_component_get_last_error_string (GstOMXComponent * comp);

GstOMXPort *      gst_omx_component_add_port (GstOMXComponent * comp, guint32 index);
GstOMXPort *      gst_omx_component_get_port (GstOMXComponent * comp, guint32 index);

gint              gst_omx_component_get_settings_cookie (GstOMXComponent * comp);
void              gst_omx_component_trigger_settings_changed (GstOMXComponent * comp);


void              gst_omx_port_get_port_definition (GstOMXPort * port, OMX_PARAM_PORTDEFINITIONTYPE * port_def);
gboolean          gst_omx_port_update_port_definition (GstOMXPort *port, OMX_PARAM_PORTDEFINITIONTYPE *port_definition);

GstOMXAcquireBufferReturn gst_omx_port_acquire_buffer (GstOMXPort *port, GstOMXBuffer **buf);
OMX_ERRORTYPE     gst_omx_port_release_buffer (GstOMXPort *port, GstOMXBuffer *buf);

OMX_ERRORTYPE     gst_omx_port_set_flushing (GstOMXPort *port, gboolean flush);
gboolean          gst_omx_port_is_flushing (GstOMXPort *port);

OMX_ERRORTYPE     gst_omx_port_allocate_buffers (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_deallocate_buffers (GstOMXPort *port);

OMX_ERRORTYPE     gst_omx_port_reconfigure (GstOMXPort * port);

OMX_ERRORTYPE     gst_omx_port_set_enabled (GstOMXPort * port, gboolean enabled);
gboolean          gst_omx_port_is_enabled (GstOMXPort * port);

G_END_DECLS

#endif /* __GST_OMX_H__ */
