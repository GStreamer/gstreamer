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

typedef struct _GstOMXCore GstOMXCore;
typedef struct _GstOMXPort GstOMXPort;
typedef enum _GstOMXPortDirection GstOMXPortDirection;
typedef struct _GstOMXComponent GstOMXComponent;
typedef struct _GstOMXBuffer GstOMXBuffer;

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
   * settings_changed, flushing, flushed, enabled_changed.
   *
   * Signalled if pending_buffers gets a
   * new buffer or flushing/flushed is set
   * to TRUE or an error happens. Always
   * check comp->last_error after being
   * signalled!
   *
   * Note: flushed==TRUE implies flushing==TRUE!
   *
   * Note: This lock must always be taken before
   * the component's state lock if both are needed!
   */
  GMutex *port_lock;
  GCond *port_cond;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GPtrArray *buffers;
  GQueue *pending_buffers;
  /* If TRUE we need to get the new caps
   * of this port */
  gboolean settings_changed;
  gboolean flushing;
  gboolean flushed; /* TRUE after OMX_CommandFlush was done */
  gboolean enabled_changed; /* TRUE after OMX_Command{En,Dis}able was done */
};

struct _GstOMXComponent {
  GstObject *parent;
  OMX_HANDLETYPE handle;
  GstOMXCore *core;

  GPtrArray *ports;

  /* Protecting state, pending_state and last_error
   * Signalled if one of them changes
   */
  GMutex *state_lock;
  GCond *state_cond;
  OMX_STATETYPE state;
  /* OMX_StateInvalid if no pending state */
  OMX_STATETYPE pending_state;
  /* OMX_ErrorNone usually, if different nothing will work */
  OMX_ERRORTYPE last_error;
};

struct _GstOMXBuffer {
  GstOMXPort *port;
  OMX_BUFFERHEADERTYPE *omx_buf;

  /* TRUE if the buffer is used by the port, i.e.
   * between {Empty,Fill}ThisBuffer and the callback
   */
  gboolean used;
};

GstOMXCore *      gst_omx_core_acquire (const gchar * filename);
void              gst_omx_core_release (GstOMXCore * core);


GstOMXComponent * gst_omx_component_new  (GstObject *parent, const gchar * core_name, const gchar * component_name);
void              gst_omx_component_free (GstOMXComponent * comp);

OMX_ERRORTYPE     gst_omx_component_set_state (GstOMXComponent * comp, OMX_STATETYPE state);
OMX_STATETYPE     gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout);

void              gst_omx_component_set_last_error (GstOMXComponent * comp, OMX_ERRORTYPE err);
OMX_ERRORTYPE     gst_omx_component_get_last_error (GstOMXComponent * comp);

GstOMXPort *      gst_omx_component_add_port (GstOMXComponent * comp, guint32 index);
GstOMXPort *      gst_omx_component_get_port (GstOMXComponent * comp, guint32 index);


void              gst_omx_port_get_port_definition (GstOMXPort * port, OMX_PARAM_PORTDEFINITIONTYPE * port_def);
gboolean          gst_omx_port_update_port_definition (GstOMXPort *port, OMX_PARAM_PORTDEFINITIONTYPE *port_definition);

GstOMXBuffer *    gst_omx_port_acquire_buffer (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_release_buffer (GstOMXPort *port, GstOMXBuffer *buf);

OMX_ERRORTYPE     gst_omx_port_set_flushing (GstOMXPort *port, gboolean flush);
gboolean          gst_omx_port_is_flushing (GstOMXPort *port);

OMX_ERRORTYPE     gst_omx_port_allocate_buffers (GstOMXPort *port);
OMX_ERRORTYPE     gst_omx_port_deallocate_buffers (GstOMXPort *port);

OMX_ERRORTYPE     gst_omx_port_reconfigure (GstOMXPort * port);

OMX_ERRORTYPE     gst_omx_port_set_enabled (GstOMXPort * port, gboolean enabled);
gboolean          gst_omx_port_is_enabled (GstOMXPort * port);

gboolean          gst_omx_port_is_settings_changed (GstOMXPort * port);

G_END_DECLS

#endif /* __GST_OMX_H__ */
