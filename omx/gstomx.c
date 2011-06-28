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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstomx.h"
#include "gstomxmpeg4videodec.h"

GST_DEBUG_CATEGORY (gstomx_debug);
#define GST_CAT_DEFAULT gstomx_debug

G_LOCK_DEFINE_STATIC (core_handles);
static GHashTable *core_handles;

GstOMXCore *
gst_omx_core_acquire (const gchar * filename)
{
  GstOMXCore *core;

  G_LOCK (core_handles);
  if (!core_handles)
    core_handles =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  core = g_hash_table_lookup (core_handles, filename);
  if (!core) {
    core = g_slice_new0 (GstOMXCore);
    core->lock = g_mutex_new ();
    core->user_count = 0;
    g_hash_table_insert (core_handles, g_strdup (filename), core);

    core->module = g_module_open (filename, G_MODULE_BIND_LAZY);
    if (!core->module)
      goto load_failed;

    if (!g_module_symbol (core->module, "OMX_Init", (gpointer *) & core->init))
      goto symbol_error;
    if (!g_module_symbol (core->module, "OMX_Deinit",
            (gpointer *) & core->deinit))
      goto symbol_error;
    if (!g_module_symbol (core->module, "OMX_GetHandle",
            (gpointer *) & core->get_handle))
      goto symbol_error;
    if (!g_module_symbol (core->module, "OMX_FreeHandle",
            (gpointer *) & core->free_handle))
      goto symbol_error;

    GST_DEBUG ("Successfully loaded core '%s'", filename);
  }

  g_mutex_lock (core->lock);
  core->user_count++;
  if (core->user_count == 1) {
    OMX_ERRORTYPE err;

    err = core->init ();
    if (err != OMX_ErrorNone) {
      GST_ERROR ("Failed to initialize core '%s': 0x%08x", filename, err);
      g_mutex_unlock (core->lock);
      goto error;
    }

    GST_DEBUG ("Successfully initialized core '%s'", filename);
  }

  g_mutex_unlock (core->lock);
  G_UNLOCK (core_handles);

  return core;

load_failed:
  {
    GST_ERROR ("Failed to load module '%s': %s", filename, g_module_error ());
    goto error;
  }
symbol_error:
  {
    GST_ERROR ("Failed to locate required OpenMAX symbol in '%s': %s", filename,
        g_module_error ());
    g_module_close (core->module);
    core->module = NULL;
    goto error;
  }
error:
  {
    g_hash_table_remove (core_handles, filename);
    g_mutex_free (core->lock);
    g_slice_free (GstOMXCore, core);

    G_UNLOCK (core_handles);

    return NULL;
  }
}

void
gst_omx_core_release (GstOMXCore * core)
{
  g_return_if_fail (core != NULL);

  G_LOCK (core_handles);

  g_mutex_lock (core->lock);

  GST_DEBUG ("Releasing core %p", core);

  core->user_count--;
  if (core->user_count == 0) {
    GST_DEBUG ("Deinit core %p", core);
    core->deinit ();
  }

  g_mutex_unlock (core->lock);

  G_UNLOCK (core_handles);
}

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
  GstOMXComponent *comp = (GstOMXComponent *) pAppData;

  switch (eEvent) {
    case OMX_EventCmdComplete:
    {
      OMX_COMMANDTYPE cmd = (OMX_COMMANDTYPE) nData1;

      GST_DEBUG_OBJECT (comp->parent, "Command %d complete", cmd);

      switch (cmd) {
        case OMX_CommandStateSet:{
          /* Notify everything that waits for
           * a state change to be finished */
          GST_DEBUG_OBJECT (comp->parent, "State change to %d finished",
              nData2);
          g_mutex_lock (comp->state_lock);
          comp->state = (OMX_STATETYPE) nData2;
          if (comp->state == comp->pending_state)
            comp->pending_state = OMX_StateInvalid;
          g_cond_broadcast (comp->state_cond);
          g_mutex_unlock (comp->state_lock);
          break;
        }
        case OMX_CommandFlush:{
          GstOMXPort *port = NULL;
          OMX_U32 index = nData2;

          port = gst_omx_component_get_port (comp, index);
          if (!port)
            break;

          GST_DEBUG_OBJECT (comp->parent, "Port %u flushed", port->index);

          /* Now notify gst_omx_port_set_flushing()
           * that the port is really flushed now and
           * we can continue
           */
          g_mutex_lock (port->port_lock);
          /* If this is ever called when the port
           * was not set to flushing something went
           * wrong but it happens for some reason.
           */
          if (port->flushing) {
            port->flushed = TRUE;
            g_cond_broadcast (port->port_cond);
          } else {
            g_debug ("Port %u is not flushing\n", (guint32) port->index);
          }
          g_mutex_unlock (port->port_lock);
          break;
        }
        case OMX_CommandPortEnable:
        case OMX_CommandPortDisable:{
          GstOMXPort *port = NULL;
          OMX_U32 index = nData2;

          port = gst_omx_component_get_port (comp, index);
          if (!port)
            break;

          GST_DEBUG_OBJECT (comp->parent, "Port %u %s", port->index,
              (cmd == OMX_CommandPortEnable ? "enabled" : "disabled"));

          g_mutex_lock (port->port_lock);
          g_cond_broadcast (port->port_cond);
          g_mutex_unlock (port->port_lock);

          break;
        }
        default:
          break;
      }
      break;
    }
    case OMX_EventError:
    {
      gint i, n;
      OMX_ERRORTYPE err = nData1;

      if (err == OMX_ErrorNone)
        break;

      GST_ERROR_OBJECT (comp->parent, "Got error %d\n", err);

      /* Error events are always fatal, notify all
       * condition variables that something went
       * wrong
       */
      g_mutex_lock (comp->state_lock);
      comp->last_error = err;
      g_cond_broadcast (comp->state_cond);
      g_mutex_unlock (comp->state_lock);

      /* Now notify all ports, no locking needed
       * here because the ports are allocated in the
       * very beginning and never change again until
       * component destruction.
       */
      n = comp->ports->len;
      for (i = 0; i < n; i++) {
        GstOMXPort *tmp = g_ptr_array_index (comp->ports, i);

        g_mutex_lock (tmp->port_lock);
        g_cond_broadcast (tmp->port_cond);
        g_mutex_unlock (tmp->port_lock);
      }
      break;
    }
    case OMX_EventPortSettingsChanged:
    {
      OMX_U32 index;
      GstOMXPort *port = NULL;

      /* FIXME XXX: WTF? Bellagio passes
       * the port index as *second* parameter
       * instead of first...
       */
      index = nData2;

      port = gst_omx_component_get_port (comp, index);
      if (!port)
        break;

      GST_DEBUG_OBJECT (comp->parent, "Settings of port %u changed", index);

      g_mutex_lock (port->port_lock);
      port->settings_changed = TRUE;
      g_cond_broadcast (port->port_cond);
      g_mutex_unlock (port->port_lock);

      /* FIXME XXX: Bellagio only sends the event for the
       * input port even if the output port settings change
       * too...
       */
      {
        gint i, n;

        n = comp->ports->len;
        for (i = 0; i < n; i++) {
          port = g_ptr_array_index (comp->ports, i);

          /* Don't notify the same port twice */
          if (port->index == index)
            continue;

          g_mutex_lock (port->port_lock);
          port->settings_changed = TRUE;
          g_cond_broadcast (port->port_cond);
          g_mutex_unlock (port->port_lock);
        }
      }

      break;
    }
    case OMX_EventPortFormatDetected:
    case OMX_EventBufferFlag:
    default:
      break;
  }

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstOMXBuffer *buf = pBuffer->pAppPrivate;

  g_assert (buf->omx_buf == pBuffer);

  /* Input buffer is empty again and can
   * be used to contain new input */
  g_mutex_lock (buf->port->port_lock);
  GST_DEBUG_OBJECT (buf->port->comp->parent, "Port %u emptied buffer %p",
      buf->port->index, buf);
  buf->used = FALSE;
  g_queue_push_tail (buf->port->pending_buffers, buf);
  g_cond_broadcast (buf->port->port_cond);
  g_mutex_unlock (buf->port->port_lock);
  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstOMXBuffer *buf = pBuffer->pAppPrivate;

  g_assert (buf->omx_buf == pBuffer);

  /* Output buffer contains output now or
   * the port was flushed */
  g_mutex_lock (buf->port->port_lock);
  GST_DEBUG_OBJECT (buf->port->comp->parent, "Port %u filled buffer %p",
      buf->port->index, buf);
  buf->used = FALSE;
  g_queue_push_tail (buf->port->pending_buffers, buf);
  g_cond_broadcast (buf->port->port_cond);
  g_mutex_unlock (buf->port->port_lock);
  return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

GstOMXComponent *
gst_omx_component_new (GstObject * parent, const gchar * core_name,
    const gchar * component_name)
{
  OMX_ERRORTYPE err;
  GstOMXCore *core;
  GstOMXComponent *comp;

  core = gst_omx_core_acquire (core_name);
  if (!core)
    return NULL;

  comp = g_slice_new0 (GstOMXComponent);
  comp->core = core;

  err =
      core->get_handle (&comp->handle, (OMX_STRING) component_name, comp,
      &callbacks);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (parent,
        "Failed to get component handle '%s' from core '%s': 0x%08x",
        component_name, core_name, err);
    gst_omx_core_release (core);
    g_slice_free (GstOMXComponent, comp);
    return NULL;
  }
  GST_DEBUG_OBJECT (parent,
      "Successfully got component handle %p (%s) from core '%s'", comp->handle,
      component_name, core_name);
  comp->parent = gst_object_ref (parent);

  comp->ports = g_ptr_array_new ();

  comp->state_lock = g_mutex_new ();
  comp->state_cond = g_cond_new ();
  comp->pending_state = OMX_StateInvalid;
  comp->last_error = OMX_ErrorNone;

  OMX_GetState (comp->handle, &comp->state);

  return comp;
}

void
gst_omx_component_free (GstOMXComponent * comp)
{
  gint i, n;

  g_return_if_fail (comp != NULL);

  GST_DEBUG_OBJECT (comp->parent, "Unloading component %p", comp);

  if (comp->ports) {
    n = comp->ports->len;
    for (i = 0; i < n; i++) {
      GstOMXPort *port = g_ptr_array_index (comp->ports, i);

      g_assert (!port->buffers || port->buffers->len == 0);

      g_mutex_free (port->port_lock);
      g_cond_free (port->port_cond);
      g_queue_free (port->pending_buffers);

      g_slice_free (GstOMXPort, port);
    }
    g_ptr_array_unref (comp->ports);
  }

  g_cond_free (comp->state_cond);
  g_mutex_free (comp->state_lock);

  comp->core->free_handle (comp->handle);
  gst_omx_core_release (comp->core);
  gst_object_unref (comp->parent);
}

OMX_ERRORTYPE
gst_omx_component_set_state (GstOMXComponent * comp, OMX_STATETYPE state)
{
  OMX_STATETYPE old_state;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);

  g_mutex_lock (comp->state_lock);
  old_state = comp->state;
  GST_DEBUG_OBJECT (comp->parent, "Setting state from %d to %d", old_state,
      state);
  if ((err = comp->last_error) != OMX_ErrorNone)
    goto done;
  if (old_state == state || comp->pending_state == state)
    goto done;

  comp->pending_state = state;
  err = OMX_SendCommand (comp->handle, OMX_CommandStateSet, state, NULL);

done:
  g_mutex_unlock (comp->state_lock);

  if (err != OMX_ErrorNone)
    GST_ERROR_OBJECT (comp->parent, "Error setting state from %d to %d: %d",
        old_state, state, err);
  return err;
}

OMX_STATETYPE
gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout)
{
  OMX_STATETYPE ret;
  GTimeVal *timeval, abstimeout;
  gboolean signalled;

  g_return_val_if_fail (comp != NULL, OMX_StateInvalid);

  GST_DEBUG_OBJECT (comp->parent, "Getting state");

  g_mutex_lock (comp->state_lock);
  ret = comp->state;
  if (comp->pending_state == OMX_StateInvalid)
    goto done;

  if (comp->last_error != OMX_ErrorNone) {
    ret = OMX_StateInvalid;
    goto done;
  }

  if (timeout != GST_CLOCK_TIME_NONE) {
    glong add = timeout / 1000;

    if (add == 0)
      goto done;

    g_get_current_time (&abstimeout);
    g_time_val_add (&abstimeout, add);
    timeval = &abstimeout;
    GST_DEBUG_OBJECT (comp->parent, "Waiting for %ld us", add);
  } else {
    timeval = NULL;
    GST_DEBUG_OBJECT (comp->parent, "Waiting for signal");
  }

  do {
    signalled = g_cond_timed_wait (comp->state_cond, comp->state_lock, timeval);
  } while (signalled && comp->last_error == OMX_ErrorNone
      && comp->pending_state != OMX_StateInvalid);

  if (signalled) {
    if (comp->last_error != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Got error while waiting for state change: %d", comp->last_error);
      ret = OMX_StateInvalid;
    } else if (comp->pending_state == OMX_StateInvalid) {
      ret = comp->state;
    } else {
      ret = OMX_StateInvalid;
      g_assert_not_reached ();
    }
  } else {
    ret = OMX_StateInvalid;
    comp->state = comp->pending_state = OMX_StateInvalid;
    GST_WARNING_OBJECT (comp->parent, "Timeout while waiting for state change");
  }

done:
  g_mutex_unlock (comp->state_lock);

  GST_DEBUG_OBJECT (comp->parent, "Returning state %d", ret);

  return ret;
}

GstOMXPort *
gst_omx_component_add_port (GstOMXComponent * comp, guint32 index)
{
  gint i, n;
  GstOMXPort *port;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, NULL);

  /* Check if this port exists already */
  n = comp->ports->len;
  for (i = 0; i < n; i++) {
    port = g_ptr_array_index (comp->ports, i);
    g_return_val_if_fail (port->index != index, NULL);
  }

  GST_DEBUG_OBJECT (comp->parent, "Adding port %u", index);

  port_def.nSize = sizeof (port_def);
  port_def.nVersion.s.nVersionMajor = 1;
  port_def.nVersion.s.nVersionMinor = 1;
  port_def.nPortIndex = index;
  err = OMX_GetParameter (comp->handle, OMX_IndexParamPortDefinition,
      &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Failed to add port %u: %d", index, err);
    return NULL;
  }

  port = g_slice_new0 (GstOMXPort);
  port->comp = comp;
  port->index = index;

  port->port_def = port_def;

  port->port_lock = g_mutex_new ();
  port->port_cond = g_cond_new ();
  port->pending_buffers = g_queue_new ();
  port->flushing = TRUE;
  port->flushed = FALSE;
  port->settings_changed = FALSE;

  g_ptr_array_add (comp->ports, port);

  return port;
}

GstOMXPort *
gst_omx_component_get_port (GstOMXComponent * comp, guint32 index)
{
  gint i, n;

  /* No need for locking here because the
   * ports are all added directly after
   * creating the component and are removed
   * when the component is destroyed.
   */

  n = comp->ports->len;
  for (i = 0; i < n; i++) {
    GstOMXPort *tmp = g_ptr_array_index (comp->ports, i);

    if (tmp->index == index)
      return tmp;
  }
  return NULL;
}

OMX_ERRORTYPE
gst_omx_component_get_last_error (GstOMXComponent * comp)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);

  g_mutex_lock (comp->state_lock);
  err = comp->last_error;
  g_mutex_unlock (comp->state_lock);

  GST_DEBUG_OBJECT (comp->parent, "Returning last error: %d", err);

  return err;
}

void
gst_omx_port_get_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  g_return_if_fail (port != NULL);

  memset (port_def, 0, sizeof (*port_def));
  port_def->nSize = sizeof (*port_def);
  port_def->nVersion.s.nVersionMajor = 1;
  port_def->nVersion.s.nVersionMinor = 1;
  port_def->nPortIndex = port->index;

  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition, port_def);
}

gboolean
gst_omx_port_update_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, FALSE);

  g_mutex_lock (port->port_lock);
  if (port_def)
    err =
        OMX_SetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
        port_def);
  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
      &port->port_def);

  GST_DEBUG_OBJECT (port->comp->parent, "Updated port %u definition: %d",
      port->index, err);

  g_mutex_unlock (port->port_lock);

  return (err == OMX_ErrorNone);
}

GstOMXBuffer *
gst_omx_port_acquire_buffer (GstOMXPort * port)
{
  GstOMXBuffer *buf = NULL;

  GST_DEBUG_OBJECT (port->comp->parent, "Acquiring buffer from port %u",
      port->index);

  g_mutex_lock (port->port_lock);
  if (port->flushing)
    goto done;

  /* Check if the component is in an error state */
  g_mutex_lock (port->comp->state_lock);
  if (port->comp->last_error != OMX_ErrorNone) {
    g_mutex_unlock (port->comp->state_lock);
    goto done;
  }
  g_mutex_unlock (port->comp->state_lock);

  /* Wait until there's something in the queue
   * or something else happened that requires
   * to return a NULL buffer, e.g. an error
   */
  if (g_queue_is_empty (port->pending_buffers))
    g_cond_wait (port->port_cond, port->port_lock);

  /* Check if the component is in an error state */
  g_mutex_lock (port->comp->state_lock);
  if (port->comp->last_error != OMX_ErrorNone) {
    g_mutex_unlock (port->comp->state_lock);
    goto done;
  }
  g_mutex_unlock (port->comp->state_lock);

  if (!g_queue_is_empty (port->pending_buffers))
    buf = g_queue_pop_head (port->pending_buffers);

done:
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (port->comp->parent, "Acquired buffer %p from port %u", buf,
      port->index);

  return buf;
}

OMX_ERRORTYPE
gst_omx_port_release_buffer (GstOMXPort * port, GstOMXBuffer * buffer)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;

  GST_DEBUG_OBJECT (port->comp->parent, "Releasing buffer %p to port %u",
      buffer, port->index);

  g_mutex_lock (port->port_lock);

  if (port->flushing)
    goto done;

  buffer->used = TRUE;
  if (port->port_def.eDir == OMX_DirInput) {
    err = OMX_EmptyThisBuffer (port->comp->handle, buffer->omx_buf);
  } else {
    err = OMX_FillThisBuffer (port->comp->handle, buffer->omx_buf);
  }

done:
  GST_DEBUG_OBJECT (port->comp->parent, "Released buffer %p to port %u: %d",
      buffer, port->index, err);
  g_mutex_unlock (port->port_lock);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_set_flushing (GstOMXPort * port, gboolean flush)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (port->comp->parent, "Setting port %d to %sflushing",
      port->index, (flush ? "" : "not "));

  g_mutex_lock (port->port_lock);
  if (! !flush == ! !port->flushing) {
    GST_DEBUG_OBJECT (port->comp->parent, "Port %u was %sflushing already",
        port->index, (flush ? "" : "not "));
    goto done;
  }

  g_mutex_lock (port->comp->state_lock);
  if ((port->comp->state != OMX_StateIdle
          && port->comp->state != OMX_StateExecuting)
      || port->comp->last_error != OMX_ErrorNone) {

    if (port->comp->last_error != OMX_ErrorNone) {
      err = port->comp->last_error;
      GST_ERROR_OBJECT (port->comp->parent, "Component is in error state: %d",
          err);
    } else {
      GST_ERROR_OBJECT (port->comp->parent, "Component is in wrong state: %d",
          port->comp->state);
      err = OMX_ErrorUndefined;
    }

    g_mutex_unlock (port->comp->state_lock);
    goto done;
  }
  g_mutex_unlock (port->comp->state_lock);

  port->flushing = flush;
  if (flush)
    g_cond_broadcast (port->port_cond);

  if (flush) {
    GTimeVal abstimeout, *timeval;
    gboolean signalled;
    OMX_ERRORTYPE last_error;

    port->flushed = FALSE;
    err =
        OMX_SendCommand (port->comp->handle, OMX_CommandFlush, port->index,
        NULL);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (port->comp->parent,
          "Error sending flush command to port %u: %d", port->index, err);
      goto done;
    }

    g_get_current_time (&abstimeout);
    g_time_val_add (&abstimeout, 5 * 10000000);
    timeval = &abstimeout;
    GST_DEBUG_OBJECT (port->comp->parent, "Waiting for 5s");

    /* Retry until timeout or until an error happend or
     * until all buffers were released by the component and
     * the flush command completed */
    do {
      signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);

      g_mutex_lock (port->comp->state_lock);
      last_error = port->comp->last_error;
      g_mutex_unlock (port->comp->state_lock);
    } while (signalled && last_error == OMX_ErrorNone && !port->flushed
        && port->buffers->len != g_queue_get_length (port->pending_buffers));
    port->flushed = FALSE;

    GST_DEBUG_OBJECT (port->comp->parent, "Port %d flushed", port->index);
    if (last_error != OMX_ErrorNone) {
      GST_ERROR_OBJECT (port->comp->parent,
          "Got error while flushing port %u: %d", port->index, last_error);
      err = last_error;
      goto done;
    } else if (!signalled) {
      GST_ERROR_OBJECT (port->comp->parent, "Timeout while flushing port %u",
          port->index);
      err = OMX_ErrorTimeout;
      goto done;
    }
  } else {
    if (port->port_def.eDir == OMX_DirOutput && port->buffers) {
      gint i, n;

      /* Enqueue all buffers for the component to fill */
      n = port->buffers->len;
      for (i = 0; i < n; i++) {
        GstOMXBuffer *buf = g_ptr_array_index (port->buffers, i);

        g_assert (!buf->used);

        err = OMX_FillThisBuffer (port->comp->handle, buf->omx_buf);
        if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (port->comp->parent,
              "Failed to pass buffer %p to port %u: %d", buf, port->index, err);
          g_mutex_lock (port->comp->state_lock);
          port->comp->last_error = err;
          g_mutex_unlock (port->comp->state_lock);
          goto done;
        }
      }

      g_queue_clear (port->pending_buffers);
    }
  }

done:
  GST_DEBUG_OBJECT (port->comp->parent, "Set port %u to %sflushing: %d",
      port->index, (flush ? "" : "not "), err);
  g_mutex_unlock (port->port_lock);

  return err;
}

gboolean
gst_omx_port_is_flushing (GstOMXPort * port)
{
  gboolean flushing;

  g_return_val_if_fail (port != NULL, FALSE);

  g_mutex_lock (port->port_lock);
  flushing = port->flushing;
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (port->comp->parent, "Port %u is flushing: %d", port->index,
      flushing);

  return flushing;
}

/* Must be called while holding port->lock */
static OMX_ERRORTYPE
gst_omx_port_allocate_buffers_unlocked (GstOMXPort * port)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i, n;

  g_assert (!port->buffers || port->buffers->len == 0);

  /* Update the port definition to check if we need more
   * buffers after the port configuration was done and to
   * update the buffer size
   */
  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
      &port->port_def);

  /* If the configured, actual number of buffers is less than
   * the minimal number of buffers required, use the minimal
   * number of buffers
   */
  if (port->port_def.nBufferCountActual < port->port_def.nBufferCountMin) {
    port->port_def.nBufferCountActual = port->port_def.nBufferCountMin;
    OMX_SetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
        &port->port_def);
    OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
        &port->port_def);
  }

  n = port->port_def.nBufferCountActual;
  GST_DEBUG_OBJECT (port->comp->parent,
      "Allocating %d buffers of size %u for port %u", n,
      port->port_def.nBufferSize, port->index);

  if (!port->buffers)
    port->buffers = g_ptr_array_sized_new (n);

  for (i = 0; i < n; i++) {
    GstOMXBuffer *buf;

    buf = g_slice_new0 (GstOMXBuffer);
    buf->port = port;
    buf->used = FALSE;
    g_ptr_array_add (port->buffers, buf);

    err =
        OMX_AllocateBuffer (port->comp->handle, &buf->omx_buf, port->index, buf,
        port->port_def.nBufferSize);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (port->comp->parent, "Failed to allocate buffer: %d",
          err);
      port->comp->last_error = err;
      break;
    }

    /* In the beginning all buffers are not owned by the component */
    g_queue_push_tail (port->pending_buffers, buf);
  }
  g_cond_broadcast (port->port_cond);

  GST_DEBUG_OBJECT (port->comp->parent, "Allocated buffers for port %u: %d",
      port->index, err);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_allocate_buffers (GstOMXPort * port)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (port->port_lock);
  err = gst_omx_port_allocate_buffers_unlocked (port);
  g_mutex_unlock (port->port_lock);

  return err;
}

/* Must be called while holding port->lock */
static OMX_ERRORTYPE
gst_omx_port_deallocate_buffers_unlocked (GstOMXPort * port)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i, n;

  GST_DEBUG_OBJECT (port->comp->parent, "Deallocating buffers of port %u",
      port->index);

  if (!port->buffers) {
    GST_DEBUG_OBJECT (port->comp->parent, "No buffers allocated for port %u",
        port->index);
    goto done;
  }

  /* We only allow deallocation of buffers after they
   * were all released from the port, either by flushing
   * the port or by disabling it.
   */
  g_assert (g_queue_get_length (port->pending_buffers) == port->buffers->len);

  n = port->buffers->len;

  for (i = 0; i < n; i++) {
    GstOMXBuffer *buf = g_ptr_array_index (port->buffers, i);
    OMX_ERRORTYPE tmp = OMX_ErrorNone;

    g_assert (!buf->used);

    /* omx_buf can be NULL if allocation failed earlier
     * and we're just shutting down
     *
     * errors do not cause exiting this loop because we want
     * to deallocate as much as possible.
     */
    if (buf->omx_buf) {
      tmp = OMX_FreeBuffer (port->comp->handle, port->index, buf->omx_buf);
      if (tmp != OMX_ErrorNone && err == OMX_ErrorNone)
        err = tmp;

    }
    g_slice_free (GstOMXBuffer, buf);
  }

  g_queue_clear (port->pending_buffers);
  g_ptr_array_unref (port->buffers);
  port->buffers = NULL;
done:
  GST_DEBUG_OBJECT (port->comp->parent, "Deallocated buffers of port %u: %d",
      port->index, err);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_deallocate_buffers (GstOMXPort * port)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (port->port_lock);
  err = gst_omx_port_deallocate_buffers_unlocked (port);
  g_mutex_unlock (port->port_lock);

  return err;
}

/* Must be called while holding port->lock */
static OMX_ERRORTYPE
gst_omx_port_set_enabled_unlocked (GstOMXPort * port, gboolean enabled)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GTimeVal abstimeout, *timeval;
  gboolean signalled;
  OMX_ERRORTYPE last_error;

  GST_DEBUG_OBJECT (port->comp->parent, "Setting port %u to %s", port->index,
      (enabled ? "enabled" : "disabled"));

  /* Check if the port is already enabled/disabled first */
  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
      &port->port_def);
  if (! !port->port_def.bEnabled == ! !enabled)
    goto done;

  if (enabled)
    err =
        OMX_SendCommand (port->comp->handle, OMX_CommandPortEnable, port->index,
        NULL);
  else
    err =
        OMX_SendCommand (port->comp->handle, OMX_CommandPortDisable,
        port->index, NULL);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (port->comp->parent,
        "Failed to send enable/disable command to port %u: %d", port->index,
        err);
    goto done;
  }

  g_get_current_time (&abstimeout);
  g_time_val_add (&abstimeout, 5 * 10000000);
  timeval = &abstimeout;
  GST_DEBUG_OBJECT (port->comp->parent, "Waiting for 5s");

  /* FIXME XXX: The spec says that bEnabled should be set *immediately*
   * but bellagio sets bEnabled after all buffers are allocated/deallocated
   */

  /* First wait until all buffers are released by the port */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  while (signalled && last_error == OMX_ErrorNone && (port->buffers
          && port->buffers->len !=
          g_queue_get_length (port->pending_buffers))) {
    signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);
    g_mutex_lock (port->comp->state_lock);
    last_error = port->comp->last_error;
    g_mutex_unlock (port->comp->state_lock);
  }

  if (last_error != OMX_ErrorNone) {
    err = last_error;
    GST_ERROR_OBJECT (port->comp->parent,
        "Got error while waiting for port %u to release all buffers: %d",
        port->index, err);
  } else if (!signalled) {
    GST_ERROR_OBJECT (port->comp->parent,
        "Timeout waiting for port %u to release all buffers", port->index);
    err = OMX_ErrorTimeout;
  }

  /* Allocate/deallocate all buffers for the port to finish
   * the enable/disable command */
  if (enabled) {
    /* If allocation fails this component can't really be used anymore */
    if ((err = gst_omx_port_allocate_buffers_unlocked (port)) != OMX_ErrorNone) {
      g_mutex_lock (port->comp->state_lock);
      port->comp->last_error = err;
      g_cond_broadcast (port->comp->state_cond);
      g_mutex_unlock (port->comp->state_lock);
      goto done;
    }
  } else {
    /* If deallocation fails this component can't really be used anymore */
    if ((err =
            gst_omx_port_deallocate_buffers_unlocked (port)) != OMX_ErrorNone) {
      g_mutex_lock (port->comp->state_lock);
      port->comp->last_error = err;
      g_cond_broadcast (port->comp->state_cond);
      g_mutex_unlock (port->comp->state_lock);
      goto done;
    }
  }

  /* And now wait until the enable/disable command is finished */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
      &port->port_def);
  while (signalled && last_error == OMX_ErrorNone
      && (! !port->port_def.bEnabled != ! !enabled)) {
    signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);
    g_mutex_lock (port->comp->state_lock);
    last_error = port->comp->last_error;
    g_mutex_unlock (port->comp->state_lock);
    OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
        &port->port_def);
  }

  if (!signalled) {
    GST_ERROR_OBJECT (port->comp->parent,
        "Timeout waiting for port %u to be enabled/disabled", port->index);
    err = OMX_ErrorTimeout;
  } else if (last_error != OMX_ErrorNone) {
    GST_ERROR_OBJECT (port->comp->parent,
        "Got error while waiting for port %u to be enabled/disabled: %d",
        port->index, err);
    err = last_error;
  }

done:
  GST_DEBUG_OBJECT (port->comp->parent, "Port %u is %s%s: %d", port->index,
      (err == OMX_ErrorNone ? "" : "not "),
      (enabled ? "enabled" : "disabled"), err);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_set_enabled (GstOMXPort * port, gboolean enabled)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (port->port_lock);
  err = gst_omx_port_set_enabled_unlocked (port, enabled);
  g_mutex_unlock (port->port_lock);

  return err;
}

gboolean
gst_omx_port_is_enabled (GstOMXPort * port)
{
  gboolean enabled;

  g_return_val_if_fail (port != NULL, FALSE);

  g_mutex_lock (port->port_lock);
  OMX_GetParameter (port->comp->handle, OMX_IndexParamPortDefinition,
      &port->port_def);
  enabled = port->port_def.bEnabled;
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (port->comp->parent, "Port %u is enabled: %d", port->index,
      enabled);

  return enabled;
}

gboolean
gst_omx_port_is_settings_changed (GstOMXPort * port)
{
  gboolean settings_changed;

  g_return_val_if_fail (port != NULL, FALSE);

  g_mutex_lock (port->port_lock);
  settings_changed = port->settings_changed;
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (port->comp->parent, "Port %u has settings-changed: %d",
      port->index, settings_changed);

  return settings_changed;
}

OMX_ERRORTYPE
gst_omx_port_reconfigure (GstOMXPort * port)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (port->comp->parent, "Reconfiguring port %u", port->index);

  g_mutex_lock (port->port_lock);

  if (!port->settings_changed)
    goto done;

  /* Disable and enable the port. This already takes
   * care of deallocating and allocating buffers.
   */
  err = gst_omx_port_set_enabled_unlocked (port, FALSE);
  if (err != OMX_ErrorNone)
    goto done;

  err = gst_omx_port_set_enabled_unlocked (port, TRUE);
  if (err != OMX_ErrorNone)
    goto done;

  port->settings_changed = FALSE;

done:
  GST_DEBUG_OBJECT (port->comp->parent, "Reconfigured port %u: %d", port->index,
      err);

  g_mutex_unlock (port->port_lock);
  return err;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  GST_DEBUG_CATEGORY_INIT (gstomx_debug, "omx", 0, "gst-omx");

  /* TODO: Use configuration file */
  ret |=
      gst_element_register (plugin, "omxmpeg4videodec", GST_RANK_PRIMARY,
      GST_TYPE_OMX_MPEG4_VIDEO_DEC);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "omx",
    "GStreamer OpenMAX Plug-ins",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
