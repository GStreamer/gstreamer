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
#include "gstomxh264dec.h"
#include "gstomxh263dec.h"
#include "gstomxwmvdec.h"
#include "gstomxmpeg4videoenc.h"
#include "gstomxh264enc.h"
#include "gstomxh263enc.h"

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
          /* FIXME: If this is ever called when the port
           * was not set to flushing something went
           * wrong but it happens for some reason.
           */
          if (port->flushing) {
            port->flushed = TRUE;
            g_cond_broadcast (port->port_cond);
          } else {
            GST_ERROR_OBJECT (comp->parent, "Port %u was not flushing",
                port->index);
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
          port->enabled_changed = TRUE;
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
      OMX_ERRORTYPE err = nData1;

      if (err == OMX_ErrorNone)
        break;

      GST_ERROR_OBJECT (comp->parent, "Got error: %s (0x%08x)",
          gst_omx_error_to_string (err), err);

      /* Error events are always fatal */
      gst_omx_component_set_last_error (comp, err);

      break;
    }
    case OMX_EventPortSettingsChanged:
    {
      gint i, n;
      guint32 port_index;
      GList *outports = NULL, *l, *k;

      if (!(comp->hacks &
              GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP)) {
        port_index = nData1;
      } else {
        port_index = nData2;
      }

      if (port_index == 0
          && (comp->hacks &
              GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1))
        port_index = 1;

      GST_DEBUG_OBJECT (comp->parent, "Settings changed (port index: %d)",
          port_index);

      /* Now update the ports' states */
      n = comp->ports->len;
      for (i = 0; i < n; i++) {
        GstOMXPort *port = g_ptr_array_index (comp->ports, i);

        g_mutex_lock (port->port_lock);
        if (port_index == OMX_ALL || port_index == port->index) {
          port->settings_cookie++;
          if (port->port_def.eDir == OMX_DirOutput)
            outports = g_list_prepend (outports, port);
          g_cond_broadcast (port->port_cond);
        }
        g_mutex_unlock (port->port_lock);
      }

      g_mutex_lock (comp->state_lock);
      for (k = outports; k; k = k->next) {
        gboolean found = FALSE;

        for (l = comp->pending_reconfigure_outports; l; l = l->next) {
          if (l->data == k->data) {
            found = TRUE;
            break;
          }
        }

        if (!found)
          comp->pending_reconfigure_outports =
              g_list_prepend (comp->pending_reconfigure_outports, k->data);
      }

      if (comp->pending_reconfigure_outports)
        g_atomic_int_set (&comp->have_pending_reconfigure_outports, 1);
      g_mutex_unlock (comp->state_lock);

      g_list_free (outports);

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
  GstOMXPort *port = buf->port;
  GstOMXComponent *comp = port->comp;

  g_assert (buf->omx_buf == pBuffer);

  /* Input buffer is empty again and can
   * be used to contain new input */
  g_mutex_lock (port->port_lock);
  GST_DEBUG_OBJECT (comp->parent, "Port %u emptied buffer %p",
      port->index, buf);
  buf->used = FALSE;
  g_queue_push_tail (port->pending_buffers, buf);
  g_cond_broadcast (port->port_cond);
  g_mutex_unlock (port->port_lock);

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstOMXBuffer *buf = pBuffer->pAppPrivate;
  GstOMXPort *port = buf->port;
  GstOMXComponent *comp = port->comp;

  g_assert (buf->omx_buf == pBuffer);

  /* Output buffer contains output now or
   * the port was flushed */
  g_mutex_lock (port->port_lock);
  GST_DEBUG_OBJECT (comp->parent, "Port %u filled buffer %p", port->index, buf);
  buf->used = FALSE;
  g_queue_push_tail (port->pending_buffers, buf);
  g_cond_broadcast (port->port_cond);
  g_mutex_unlock (port->port_lock);

  return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

GstOMXComponent *
gst_omx_component_new (GstObject * parent, const gchar * core_name,
    const gchar * component_name, const gchar * component_role, guint64 hacks)
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
  comp->hacks = hacks;

  comp->ports = g_ptr_array_new ();
  comp->n_in_ports = 0;
  comp->n_out_ports = 0;

  comp->state_lock = g_mutex_new ();
  comp->state_cond = g_cond_new ();
  comp->pending_state = OMX_StateInvalid;
  comp->last_error = OMX_ErrorNone;

  /* Set component role if any */
  if (component_role) {
    OMX_PARAM_COMPONENTROLETYPE param;

    GST_OMX_INIT_STRUCT (&param);

    g_strlcpy ((gchar *) param.cRole, component_role, sizeof (param.cRole));
    err =
        gst_omx_component_set_parameter (comp,
        OMX_IndexParamStandardComponentRole, &param);

    GST_DEBUG_OBJECT (parent, "Setting component role to '%s': %s (0x%08x)",
        component_role, gst_omx_error_to_string (err), err);

    /* If setting the role failed this component is unusable */
    if (err != OMX_ErrorNone) {
      gst_omx_component_free (comp);
      return NULL;
    }
  }

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

      gst_omx_port_deallocate_buffers (port);

      g_mutex_free (port->port_lock);
      g_cond_free (port->port_cond);
      g_queue_free (port->pending_buffers);

      g_slice_free (GstOMXPort, port);
    }
#if GLIB_CHECK_VERSION(2,22,0)
    g_ptr_array_unref (comp->ports);
#else
    g_ptr_array_free (comp->ports, TRUE);
#endif
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
  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto done;
  }

  if (old_state == state || comp->pending_state == state) {
    GST_DEBUG_OBJECT (comp->parent, "Component already in state %d", state);
    goto done;
  }

  comp->pending_state = state;
  err = OMX_SendCommand (comp->handle, OMX_CommandStateSet, state, NULL);

  /* Reset some things */
  if (old_state == OMX_StateExecuting && state == OMX_StateIdle) {
    g_atomic_int_set (&comp->have_pending_reconfigure_outports, 0);
    g_list_free (comp->pending_reconfigure_outports);
    comp->pending_reconfigure_outports = NULL;
    /* Notify all inports that are still waiting */
    g_cond_broadcast (comp->state_cond);
  }

done:
  g_mutex_unlock (comp->state_lock);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Error setting state from %d to %d: %s (0x%08x)", old_state, state,
        gst_omx_error_to_string (err), err);
    gst_omx_component_set_last_error (comp, err);
  }
  return err;
}

OMX_STATETYPE
gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout)
{
  OMX_STATETYPE ret;
  GTimeVal *timeval, abstimeout;
  gboolean signalled = TRUE;

  g_return_val_if_fail (comp != NULL, OMX_StateInvalid);

  GST_DEBUG_OBJECT (comp->parent, "Getting state");

  g_mutex_lock (comp->state_lock);
  ret = comp->state;
  if (comp->pending_state == OMX_StateInvalid)
    goto done;

  if (comp->last_error != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component in error state: %s (0x%08x)",
        gst_omx_error_to_string (comp->last_error), comp->last_error);
    ret = OMX_StateInvalid;
    goto done;
  }

  if (timeout != GST_CLOCK_TIME_NONE) {
    glong add = timeout / (GST_SECOND / G_USEC_PER_SEC);

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
          "Got error while waiting for state change: %s (0x%08x)",
          gst_omx_error_to_string (comp->last_error), comp->last_error);
      ret = OMX_StateInvalid;
    } else if (comp->pending_state == OMX_StateInvalid) {
      /* State change finished and everything's fine */
      ret = comp->state;
    } else {
      ret = OMX_StateInvalid;
      g_assert_not_reached ();
    }
  } else {
    ret = OMX_StateInvalid;
    GST_WARNING_OBJECT (comp->parent, "Timeout while waiting for state change");
  }

done:
  g_mutex_unlock (comp->state_lock);

  /* If we waited and timed out this component is unusable now */
  if (!signalled)
    gst_omx_component_set_last_error (comp, OMX_ErrorTimeout);

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

  GST_OMX_INIT_STRUCT (&port_def);
  port_def.nPortIndex = index;

  err = gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Failed to add port %u: %s (0x%08x)", index,
        gst_omx_error_to_string (err), err);
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
  port->enabled_changed = FALSE;

  if (port->port_def.eDir == OMX_DirInput)
    comp->n_in_ports++;
  else
    comp->n_out_ports++;

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

void
gst_omx_component_trigger_settings_changed (GstOMXComponent * comp,
    guint32 port_index)
{
  g_return_if_fail (comp != NULL);

  /* Reverse hacks */
  if (port_index == 1
      && (comp->hacks & GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1))
    port_index = 0;

  if (!(comp->hacks &
          GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP)) {
    EventHandler (comp->handle, comp, OMX_EventPortSettingsChanged, port_index,
        0, NULL);
  } else {
    EventHandler (comp->handle, comp, OMX_EventPortSettingsChanged, 0,
        port_index, NULL);
  }
}

/* NOTE: This takes comp->state_lock *and* *all* port->port_lock! */
void
gst_omx_component_set_last_error (GstOMXComponent * comp, OMX_ERRORTYPE err)
{
  gint i, n;

  g_return_if_fail (comp != NULL);

  if (err == OMX_ErrorNone)
    return;

  GST_ERROR_OBJECT (comp->parent, "Setting last error: %s (0x%08x)",
      gst_omx_error_to_string (err), err);
  g_mutex_lock (comp->state_lock);
  /* We only set the first error ever from which
   * we can't recover anymore.
   */
  if (comp->last_error == OMX_ErrorNone)
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
}

OMX_ERRORTYPE
gst_omx_component_get_last_error (GstOMXComponent * comp)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);

  g_mutex_lock (comp->state_lock);
  err = comp->last_error;
  g_mutex_unlock (comp->state_lock);

  GST_DEBUG_OBJECT (comp->parent, "Returning last error: %s (0x%08x)",
      gst_omx_error_to_string (err), err);

  return err;
}

const gchar *
gst_omx_component_get_last_error_string (GstOMXComponent * comp)
{
  g_return_val_if_fail (comp != NULL, NULL);

  return gst_omx_error_to_string (gst_omx_component_get_last_error (comp));
}

OMX_ERRORTYPE
gst_omx_component_get_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (param != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Getting parameter at index 0x%08x", index);
  err = OMX_GetParameter (comp->handle, index, param);
  GST_DEBUG_OBJECT (comp->parent, "Got parameter at index 0x%08x: %s (0x%08x)",
      index, gst_omx_error_to_string (err), err);

  return err;
}

OMX_ERRORTYPE
gst_omx_component_set_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (param != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Setting parameter at index 0x%08x", index);
  err = OMX_SetParameter (comp->handle, index, param);
  GST_DEBUG_OBJECT (comp->parent, "Set parameter at index 0x%08x: %s (0x%08x)",
      index, gst_omx_error_to_string (err), err);

  return err;
}

OMX_ERRORTYPE
gst_omx_component_get_config (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer config)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (config != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Getting configuration at index 0x%08x",
      index);
  err = OMX_GetConfig (comp->handle, index, config);
  GST_DEBUG_OBJECT (comp->parent, "Got parameter at index 0x%08x: %s (0x%08x)",
      index, gst_omx_error_to_string (err), err);

  return err;
}

OMX_ERRORTYPE
gst_omx_component_set_config (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer config)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (config != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Setting configuration at index 0x%08x",
      index);
  err = OMX_SetConfig (comp->handle, index, config);
  GST_DEBUG_OBJECT (comp->parent, "Set parameter at index 0x%08x: %s (0x%08x)",
      index, gst_omx_error_to_string (err), err);

  return err;
}

void
gst_omx_port_get_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  GstOMXComponent *comp;

  g_return_if_fail (port != NULL);

  comp = port->comp;

  GST_OMX_INIT_STRUCT (port_def);
  port_def->nPortIndex = port->index;

  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      port_def);
}

gboolean
gst_omx_port_update_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GstOMXComponent *comp;

  g_return_val_if_fail (port != NULL, FALSE);

  comp = port->comp;

  g_mutex_lock (port->port_lock);
  if (port_def)
    err =
        gst_omx_component_set_parameter (comp, OMX_IndexParamPortDefinition,
        port_def);
  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);

  GST_DEBUG_OBJECT (comp->parent, "Updated port %u definition: %s (0x%08x)",
      port->index, gst_omx_error_to_string (err), err);

  g_mutex_unlock (port->port_lock);

  return (err == OMX_ErrorNone);
}

GstOMXAcquireBufferReturn
gst_omx_port_acquire_buffer (GstOMXPort * port, GstOMXBuffer ** buf)
{
  GstOMXAcquireBufferReturn ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXComponent *comp;
  OMX_ERRORTYPE err;
  GstOMXBuffer *_buf = NULL;

  g_return_val_if_fail (port != NULL, GST_OMX_ACQUIRE_BUFFER_ERROR);
  g_return_val_if_fail (buf != NULL, GST_OMX_ACQUIRE_BUFFER_ERROR);

  *buf = NULL;

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Acquiring buffer from port %u", port->index);

  g_mutex_lock (port->port_lock);

retry:

  /* Check if the component is in an error state */
  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component is in error state: %s",
        gst_omx_error_to_string (err));
    ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
    goto done;
  }

  /* Check if the port is flushing */
  if (port->flushing) {
    ret = GST_OMX_ACQUIRE_BUFFER_FLUSHING;
    goto done;
  }

  /* If this is an input port and at least one of the output ports
   * needs to be reconfigured, we wait until all output ports are
   * reconfigured. Afterwards this port is reconfigured if required
   * or buffers are returned to be filled as usual.
   */
  if (port->port_def.eDir == OMX_DirInput) {
    if (g_atomic_int_get (&comp->have_pending_reconfigure_outports)) {
      g_mutex_unlock (port->port_lock);
      g_mutex_lock (comp->state_lock);
      while (g_atomic_int_get (&comp->have_pending_reconfigure_outports) &&
          (err = comp->last_error) == OMX_ErrorNone && !port->flushing) {
        GST_DEBUG_OBJECT (comp->parent,
            "Waiting for output ports to reconfigure");
        g_cond_wait (comp->state_cond, comp->state_lock);
      }
      g_mutex_unlock (comp->state_lock);
      g_mutex_lock (port->port_lock);
      goto retry;
    }

    /* Only if this port needs to be reconfigured too notify
     * the caller about it */
    if (port->settings_cookie != port->configured_settings_cookie) {
      ret = GST_OMX_ACQUIRE_BUFFER_RECONFIGURE;
      port->settings_changed = TRUE;
      goto done;
    }
  }

  /* If we have an output port that needs to be reconfigured
   * and it still has buffers pending for the old configuration
   * we first return them.
   * NOTE: If buffers for this configuration arrive later
   * we have to drop them... */
  if (port->port_def.eDir == OMX_DirOutput &&
      port->settings_cookie != port->configured_settings_cookie) {
    if (!g_queue_is_empty (port->pending_buffers)) {
      GST_DEBUG_OBJECT (comp->parent,
          "Output port %u needs reconfiguration but has buffers pending",
          port->index);
      _buf = g_queue_pop_head (port->pending_buffers);
      g_assert (_buf != NULL);
      ret = GST_OMX_ACQUIRE_BUFFER_OK;
      goto done;
    }

    ret = GST_OMX_ACQUIRE_BUFFER_RECONFIGURE;
    port->settings_changed = TRUE;
    goto done;
  }

  if (port->settings_changed) {
    GST_DEBUG_OBJECT (comp->parent,
        "Port %u has settings changed, need new caps", port->index);
    ret = GST_OMX_ACQUIRE_BUFFER_RECONFIGURED;
    port->settings_changed = FALSE;
    goto done;
  }

  /* 
   * At this point we have no error or flushing port
   * and a properly configured port.
   *
   */

  /* If the queue is empty we wait until a buffer
   * arrives, an error happens, the port is flushing
   * or the port needs to be reconfigured.
   */
  if (g_queue_is_empty (port->pending_buffers)) {
    GST_DEBUG_OBJECT (comp->parent, "Queue of port %u is empty", port->index);
    g_cond_wait (port->port_cond, port->port_lock);
  } else {
    GST_DEBUG_OBJECT (comp->parent, "Port %u has pending buffers");
    _buf = g_queue_pop_head (port->pending_buffers);
    ret = GST_OMX_ACQUIRE_BUFFER_OK;
    goto done;
  }

  /* And now check everything again and maybe get a buffer */
  goto retry;

done:
  g_mutex_unlock (port->port_lock);

  if (_buf)
    *buf = _buf;

  GST_DEBUG_OBJECT (comp->parent, "Acquired buffer %p from port %u: %d", _buf,
      port->index, ret);

  return ret;
}

OMX_ERRORTYPE
gst_omx_port_release_buffer (GstOMXPort * port, GstOMXBuffer * buf)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (buf != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (buf->port == port, OMX_ErrorUndefined);

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Releasing buffer %p to port %u",
      buf, port->index);

  g_mutex_lock (port->port_lock);

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component is in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto done;
  }

  if (port->flushing) {
    GST_DEBUG_OBJECT (comp->parent, "Port %u is flushing, not releasing buffer",
        port->index);
    goto done;
  }

  /* FIXME: What if the settings cookies don't match? */

  buf->used = TRUE;
  if (port->port_def.eDir == OMX_DirInput) {
    err = OMX_EmptyThisBuffer (comp->handle, buf->omx_buf);
  } else {
    err = OMX_FillThisBuffer (comp->handle, buf->omx_buf);
  }

done:
  GST_DEBUG_OBJECT (comp->parent, "Released buffer %p to port %u: %s (0x%08x)",
      buf, port->index, gst_omx_error_to_string (err), err);
  g_mutex_unlock (port->port_lock);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_set_flushing (GstOMXPort * port, gboolean flush)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;
  GST_DEBUG_OBJECT (comp->parent, "Setting port %d to %sflushing",
      port->index, (flush ? "" : "not "));

  g_mutex_lock (port->port_lock);
  if (! !flush == ! !port->flushing) {
    GST_DEBUG_OBJECT (comp->parent, "Port %u was %sflushing already",
        port->index, (flush ? "" : "not "));
    goto done;
  }

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component is in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto done;
  }

  g_mutex_lock (comp->state_lock);
  if (comp->state != OMX_StateIdle && comp->state != OMX_StateExecuting) {

    GST_ERROR_OBJECT (comp->parent, "Component is in wrong state: %d",
        comp->state);
    err = OMX_ErrorUndefined;

    g_mutex_unlock (comp->state_lock);
    goto done;
  }
  g_mutex_unlock (comp->state_lock);

  port->flushing = flush;
  if (flush) {
    g_cond_broadcast (port->port_cond);

    /* We also need to signal the state cond because
     * an input port might wait on this for the output
     * ports to reconfigure. This will not confuse
     * other waiters on the state cond because they will
     * additionally check if the condition they're waiting
     * for is true after waking up.
     */
    g_mutex_lock (comp->state_lock);
    g_cond_broadcast (comp->state_cond);
    g_mutex_unlock (comp->state_lock);
  }

  if (flush) {
    GTimeVal abstimeout, *timeval;
    gboolean signalled;
    OMX_ERRORTYPE last_error;

    port->flushed = FALSE;
    err = OMX_SendCommand (comp->handle, OMX_CommandFlush, port->index, NULL);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Error sending flush command to port %u: %s (0x%08x)", port->index,
          gst_omx_error_to_string (err), err);
      goto done;
    }

    g_get_current_time (&abstimeout);
    g_time_val_add (&abstimeout, 5 * G_USEC_PER_SEC);
    timeval = &abstimeout;
    GST_DEBUG_OBJECT (comp->parent, "Waiting for 5s");

    /* Retry until timeout or until an error happend or
     * until all buffers were released by the component and
     * the flush command completed */
    do {
      signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);

      last_error = gst_omx_component_get_last_error (comp);
    } while (signalled && last_error == OMX_ErrorNone && !port->flushed
        && port->buffers->len != g_queue_get_length (port->pending_buffers));
    port->flushed = FALSE;

    GST_DEBUG_OBJECT (comp->parent, "Port %d flushed", port->index);
    if (last_error != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Got error while flushing port %u: %s (0x%08x)", port->index,
          gst_omx_error_to_string (last_error), last_error);
      err = last_error;
      goto done;
    } else if (!signalled) {
      GST_ERROR_OBJECT (comp->parent, "Timeout while flushing port %u",
          port->index);
      err = OMX_ErrorTimeout;
      goto done;
    }
  } else {
    if (port->port_def.eDir == OMX_DirOutput && port->buffers) {
      GstOMXBuffer *buf;

      if (comp->hacks & GST_OMX_HACK_QCOM_7x30_FILL_THIS_BUFFER_RACE)
        g_usleep (G_USEC_PER_SEC / 10);

      /* Enqueue all buffers for the component to fill */
      while ((buf = g_queue_pop_head (port->pending_buffers))) {
        g_assert (!buf->used);

        err = OMX_FillThisBuffer (comp->handle, buf->omx_buf);
        if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (comp->parent,
              "Failed to pass buffer %p to port %u: %s (0x%08x)", buf,
              port->index, gst_omx_error_to_string (err), err);
          goto error;
        }
      }
    }
  }

done:
  GST_DEBUG_OBJECT (comp->parent, "Set port %u to %sflushing: %s (0x%08x)",
      port->index, (flush ? "" : "not "), gst_omx_error_to_string (err), err);
  g_mutex_unlock (port->port_lock);

  return err;

error:
  {
    /* Need to unlock the port lock here because
     * set_last_error() needs all port locks.
     * This is safe here because we're just going
     * to error out anyway */
    g_mutex_unlock (port->port_lock);
    gst_omx_component_set_last_error (comp, err);
    g_mutex_lock (port->port_lock);
    goto done;
  }
}

gboolean
gst_omx_port_is_flushing (GstOMXPort * port)
{
  GstOMXComponent *comp;
  gboolean flushing;

  g_return_val_if_fail (port != NULL, FALSE);

  comp = port->comp;

  g_mutex_lock (port->port_lock);
  flushing = port->flushing;
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (comp->parent, "Port %u is flushing: %d", port->index,
      flushing);

  return flushing;
}

/* Must be called while holding port->lock */
static OMX_ERRORTYPE
gst_omx_port_allocate_buffers_unlocked (GstOMXPort * port)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i, n;

  g_assert (!port->buffers || port->buffers->len == 0);

  comp = port->comp;

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto done;
  }

  /* Update the port definition to check if we need more
   * buffers after the port configuration was done and to
   * update the buffer size
   */
  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);

  /* If the configured, actual number of buffers is less than
   * the minimal number of buffers required, use the minimal
   * number of buffers
   */
  if (port->port_def.nBufferCountActual < port->port_def.nBufferCountMin) {
    port->port_def.nBufferCountActual = port->port_def.nBufferCountMin;
    err = gst_omx_component_set_parameter (comp, OMX_IndexParamPortDefinition,
        &port->port_def);
    gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
        &port->port_def);
  }

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Failed to configure number of buffers of port %u: %s (0x%08x)",
        port->index, gst_omx_error_to_string (err), err);
    goto error;
  }

  n = port->port_def.nBufferCountActual;
  GST_DEBUG_OBJECT (comp->parent,
      "Allocating %d buffers of size %u for port %u", n,
      port->port_def.nBufferSize, port->index);

  if (!port->buffers)
    port->buffers = g_ptr_array_sized_new (n);

  for (i = 0; i < n; i++) {
    GstOMXBuffer *buf;

    buf = g_slice_new0 (GstOMXBuffer);
    buf->port = port;
    buf->used = FALSE;
    buf->settings_cookie = port->settings_cookie;
    g_ptr_array_add (port->buffers, buf);

    err =
        OMX_AllocateBuffer (comp->handle, &buf->omx_buf, port->index, buf,
        port->port_def.nBufferSize);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Failed to allocate buffer for port %u: %s (0x%08x)", port->index,
          gst_omx_error_to_string (err), err);
      goto error;
    }

    g_assert (buf->omx_buf->pAppPrivate == buf);

    /* In the beginning all buffers are not owned by the component */
    g_queue_push_tail (port->pending_buffers, buf);
  }

done:
  GST_DEBUG_OBJECT (comp->parent, "Allocated buffers for port %u: %s (0x%08x)",
      port->index, gst_omx_error_to_string (err), err);

  return err;

error:
  {
    /* Need to unlock the port lock here because
     * set_last_error() needs all port locks.
     * This is safe here because we're just going
     * to error out anyway */
    g_mutex_unlock (port->port_lock);
    gst_omx_component_set_last_error (comp, err);
    g_mutex_lock (port->port_lock);
    goto done;
  }
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
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i, n;

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Deallocating buffers of port %u",
      port->index);

  if (!port->buffers) {
    GST_DEBUG_OBJECT (comp->parent, "No buffers allocated for port %u",
        port->index);
    goto done;
  }

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    /* We still try to deallocate all buffers */
  }

  /* We only allow deallocation of buffers after they
   * were all released from the port, either by flushing
   * the port or by disabling it.
   */
  n = port->buffers->len;
  for (i = 0; i < n; i++) {
    GstOMXBuffer *buf = g_ptr_array_index (port->buffers, i);
    OMX_ERRORTYPE tmp = OMX_ErrorNone;

    if (buf->used)
      GST_ERROR_OBJECT (comp->parent,
          "Trying to free used buffer %p of port %u", buf, port->index);

    /* omx_buf can be NULL if allocation failed earlier
     * and we're just shutting down
     *
     * errors do not cause exiting this loop because we want
     * to deallocate as much as possible.
     */
    if (buf->omx_buf) {
      tmp = OMX_FreeBuffer (comp->handle, port->index, buf->omx_buf);
      if (tmp != OMX_ErrorNone) {
        GST_ERROR_OBJECT (comp->parent,
            "Failed to deallocate buffer %d of port %u: %s (0x%08x)", i,
            port->index, gst_omx_error_to_string (tmp), tmp);
        if (err == OMX_ErrorNone)
          err = tmp;
      }
    }
    g_slice_free (GstOMXBuffer, buf);
  }

  g_queue_clear (port->pending_buffers);
#if GLIB_CHECK_VERSION(2,22,0)
  g_ptr_array_unref (port->buffers);
#else
  g_ptr_array_free (port->buffers, TRUE);
#endif
  port->buffers = NULL;

done:
  GST_DEBUG_OBJECT (comp->parent, "Deallocated buffers of port %u: %s (0x%08x)",
      port->index, gst_omx_error_to_string (err), err);

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
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GTimeVal abstimeout, *timeval;
  gboolean signalled;
  OMX_ERRORTYPE last_error;

  comp = port->comp;

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component in error state: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto done;
  }

  GST_DEBUG_OBJECT (comp->parent, "Setting port %u to %s", port->index,
      (enabled ? "enabled" : "disabled"));

  /* Check if the port is already enabled/disabled first */
  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);
  if (! !port->port_def.bEnabled == ! !enabled)
    goto done;

  port->enabled_changed = FALSE;

  if (enabled)
    err =
        OMX_SendCommand (comp->handle, OMX_CommandPortEnable, port->index,
        NULL);
  else
    err =
        OMX_SendCommand (comp->handle, OMX_CommandPortDisable,
        port->index, NULL);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Failed to send enable/disable command to port %u: %s (0x%08x)",
        port->index, gst_omx_error_to_string (err), err);
    goto error;
  }

  g_get_current_time (&abstimeout);
  g_time_val_add (&abstimeout, 5 * G_USEC_PER_SEC);
  timeval = &abstimeout;
  GST_DEBUG_OBJECT (comp->parent, "Waiting for 5s");

  /* First wait until all buffers are released by the port */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  while (signalled && last_error == OMX_ErrorNone && (port->buffers
          && port->buffers->len !=
          g_queue_get_length (port->pending_buffers))) {
    signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);
    last_error = gst_omx_component_get_last_error (comp);
  }

  if (last_error != OMX_ErrorNone) {
    err = last_error;
    GST_ERROR_OBJECT (comp->parent,
        "Got error while waiting for port %u to release all buffers: %s (0x%08x)",
        port->index, gst_omx_error_to_string (err), err);
    goto done;
  } else if (!signalled) {
    GST_ERROR_OBJECT (comp->parent,
        "Timeout waiting for port %u to release all buffers", port->index);
    err = OMX_ErrorTimeout;
    goto error;
  }

  /* Allocate/deallocate all buffers for the port to finish
   * the enable/disable command */
  if (enabled) {
    /* If allocation fails this component can't really be used anymore */
    if ((err = gst_omx_port_allocate_buffers_unlocked (port)) != OMX_ErrorNone) {
      goto error;
    }
  } else {
    /* If deallocation fails this component can't really be used anymore */
    if ((err =
            gst_omx_port_deallocate_buffers_unlocked (port)) != OMX_ErrorNone) {
      goto error;
    }
  }

  /* And now wait until the enable/disable command is finished */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);
  while (signalled && last_error == OMX_ErrorNone
      && (! !port->port_def.bEnabled != ! !enabled || !port->enabled_changed)) {
    signalled = g_cond_timed_wait (port->port_cond, port->port_lock, timeval);
    last_error = gst_omx_component_get_last_error (comp);
    gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
        &port->port_def);
  }

  port->enabled_changed = FALSE;

  if (!signalled) {
    GST_ERROR_OBJECT (comp->parent,
        "Timeout waiting for port %u to be %s", port->index,
        (enabled ? "enabled" : "disabled"));
    err = OMX_ErrorTimeout;
    goto error;
  } else if (last_error != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Got error while waiting for port %u to be %s: %s (0x%08x)",
        port->index, (enabled ? "enabled" : "disabled"),
        gst_omx_error_to_string (err), err);
    err = last_error;
  } else {
    /* If everything went fine and we have an output port we
     * should provide all newly allocated buffers to the port
     */
    if (enabled && port->port_def.eDir == OMX_DirOutput) {
      GstOMXBuffer *buf;

      if (comp->hacks & GST_OMX_HACK_QCOM_7x30_FILL_THIS_BUFFER_RACE)
        g_usleep (G_USEC_PER_SEC / 10);

      /* Enqueue all buffers for the component to fill */
      while ((buf = g_queue_pop_head (port->pending_buffers))) {
        g_assert (!buf->used);

        err = OMX_FillThisBuffer (comp->handle, buf->omx_buf);
        if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (comp->parent,
              "Failed to pass buffer %p to port %u: %s (0x%08x)", buf,
              port->index, gst_omx_error_to_string (err), err);
          goto error;
        }
      }
    }
  }

done:
  GST_DEBUG_OBJECT (comp->parent, "Port %u is %s%s: %s (0x%08x)", port->index,
      (err == OMX_ErrorNone ? "" : "not "),
      (enabled ? "enabled" : "disabled"), gst_omx_error_to_string (err), err);

  return err;

error:
  {
    /* Need to unlock the port lock here because
     * set_last_error() needs all port locks.
     * This is safe here because we're just going
     * to error out anyway */
    g_mutex_unlock (port->port_lock);
    gst_omx_component_set_last_error (comp, err);
    g_mutex_lock (port->port_lock);
    goto done;
  }
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
  GstOMXComponent *comp;
  gboolean enabled;

  g_return_val_if_fail (port != NULL, FALSE);

  comp = port->comp;

  g_mutex_lock (port->port_lock);
  gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);
  enabled = port->port_def.bEnabled;
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (comp->parent, "Port %u is enabled: %d", port->index,
      enabled);

  return enabled;
}

OMX_ERRORTYPE
gst_omx_port_reconfigure (GstOMXPort * port)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Reconfiguring port %u", port->index);

  g_mutex_lock (port->port_lock);

  if (!port->settings_changed)
    goto done;

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone)
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

  port->configured_settings_cookie = port->settings_cookie;

  /* If this is an output port, notify all input ports
   * that might wait for us to reconfigure in
   * acquire_buffer()
   */
  if (port->port_def.eDir == OMX_DirOutput) {
    GList *l;

    g_mutex_lock (comp->state_lock);
    for (l = comp->pending_reconfigure_outports; l; l = l->next) {
      if (l->data == (gpointer) port) {
        comp->pending_reconfigure_outports =
            g_list_delete_link (comp->pending_reconfigure_outports, l);
        break;
      }
    }
    if (!comp->pending_reconfigure_outports) {
      g_atomic_int_set (&comp->have_pending_reconfigure_outports, 0);
      g_cond_broadcast (comp->state_cond);
    }
    g_mutex_unlock (comp->state_lock);
  }

done:
  GST_DEBUG_OBJECT (comp->parent, "Reconfigured port %u: %s (0x%08x)",
      port->index, gst_omx_error_to_string (err), err);

  g_mutex_unlock (port->port_lock);
  return err;
}

OMX_ERRORTYPE
gst_omx_port_manual_reconfigure (GstOMXPort * port, gboolean start)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Manual reconfigure of port %u %s",
      port->index, (start ? "start" : "stsop"));

  g_mutex_lock (port->port_lock);

  if ((err = gst_omx_component_get_last_error (comp)) != OMX_ErrorNone)
    goto done;

  if (start)
    port->settings_cookie++;
  else
    port->configured_settings_cookie = port->settings_cookie;

  if (port->port_def.eDir == OMX_DirOutput) {
    GList *l;

    if (start) {
      g_mutex_lock (comp->state_lock);
      for (l = comp->pending_reconfigure_outports; l; l = l->next) {
        if (l->data == (gpointer) port)
          break;
      }

      if (!l) {
        comp->pending_reconfigure_outports =
            g_list_prepend (comp->pending_reconfigure_outports, port);
        g_atomic_int_set (&comp->have_pending_reconfigure_outports, 1);
      }
      g_mutex_unlock (comp->state_lock);
    } else {
      g_mutex_lock (comp->state_lock);
      for (l = comp->pending_reconfigure_outports; l; l = l->next) {
        if (l->data == (gpointer) port) {
          comp->pending_reconfigure_outports =
              g_list_delete_link (comp->pending_reconfigure_outports, l);
          break;
        }
      }
      if (!comp->pending_reconfigure_outports) {
        g_atomic_int_set (&comp->have_pending_reconfigure_outports, 0);
        g_cond_broadcast (comp->state_cond);
      }
      g_mutex_unlock (comp->state_lock);
    }
  }


done:
  g_mutex_unlock (port->port_lock);

  GST_DEBUG_OBJECT (comp->parent, "Manual reconfigure of port %u: %s (0x%08x)",
      port->index, gst_omx_error_to_string (err), err);

  return err;
}

GQuark gst_omx_element_name_quark = 0;

static GType (*types[]) (void) = {
gst_omx_mpeg4_video_dec_get_type, gst_omx_h264_dec_get_type,
      gst_omx_h263_dec_get_type, gst_omx_wmv_dec_get_type,
      gst_omx_mpeg4_video_enc_get_type, gst_omx_h264_enc_get_type,
      gst_omx_h263_enc_get_type};

static GKeyFile *config = NULL;
GKeyFile *
gst_omx_get_configuration (void)
{
  return config;
}

const gchar *
gst_omx_error_to_string (OMX_ERRORTYPE err)
{
  switch (err) {
    case OMX_ErrorNone:
      return "None";
    case OMX_ErrorInsufficientResources:
      return "Insufficient resources";
    case OMX_ErrorUndefined:
      return "Undefined";
    case OMX_ErrorInvalidComponentName:
      return "Invalid component name";
    case OMX_ErrorComponentNotFound:
      return "Component not found";
    case OMX_ErrorInvalidComponent:
      return "Invalid component";
    case OMX_ErrorBadParameter:
      return "Bad parameter";
    case OMX_ErrorNotImplemented:
      return "Not implemented";
    case OMX_ErrorUnderflow:
      return "Underflow";
    case OMX_ErrorOverflow:
      return "Overflow";
    case OMX_ErrorHardware:
      return "Hardware";
    case OMX_ErrorInvalidState:
      return "Invalid state";
    case OMX_ErrorStreamCorrupt:
      return "Stream corrupt";
    case OMX_ErrorPortsNotCompatible:
      return "Ports not compatible";
    case OMX_ErrorResourcesLost:
      return "Resources lost";
    case OMX_ErrorNoMore:
      return "No more";
    case OMX_ErrorVersionMismatch:
      return "Version mismatch";
    case OMX_ErrorNotReady:
      return "Not ready";
    case OMX_ErrorTimeout:
      return "Timeout";
    case OMX_ErrorSameState:
      return "Same state";
    case OMX_ErrorResourcesPreempted:
      return "Resources preempted";
    case OMX_ErrorPortUnresponsiveDuringAllocation:
      return "Port unresponsive during allocation";
    case OMX_ErrorPortUnresponsiveDuringDeallocation:
      return "Port unresponsive during deallocation";
    case OMX_ErrorPortUnresponsiveDuringStop:
      return "Port unresponsive during stop";
    case OMX_ErrorIncorrectStateTransition:
      return "Incorrect state transition";
    case OMX_ErrorIncorrectStateOperation:
      return "Incorrect state operation";
    case OMX_ErrorUnsupportedSetting:
      return "Unsupported setting";
    case OMX_ErrorUnsupportedIndex:
      return "Unsupported index";
    case OMX_ErrorBadPortIndex:
      return "Bad port index";
    case OMX_ErrorPortUnpopulated:
      return "Port unpopulated";
    case OMX_ErrorComponentSuspended:
      return "Component suspended";
    case OMX_ErrorDynamicResourcesUnavailable:
      return "Dynamic resources unavailable";
    case OMX_ErrorMbErrorsInFrame:
      return "Macroblock errors in frame";
    case OMX_ErrorFormatNotDetected:
      return "Format not detected";
    case OMX_ErrorContentPipeOpenFailed:
      return "Content pipe open failed";
    case OMX_ErrorContentPipeCreationFailed:
      return "Content pipe creation failed";
    case OMX_ErrorSeperateTablesUsed:
      return "Seperate tables used";
    case OMX_ErrorTunnelingUnsupported:
      return "Tunneling unsupported";
    default:
      if (err >= (guint32) OMX_ErrorKhronosExtensions
          && err < (guint32) OMX_ErrorVendorStartUnused) {
        return "Khronos extension error";
      } else if (err >= (guint32) OMX_ErrorVendorStartUnused
          && err < (guint32) OMX_ErrorMax) {
        return "Vendor specific error";
      } else {
        return "Unknown error";
      }
  }
}

guint64
gst_omx_parse_hacks (gchar ** hacks)
{
  guint64 hacks_flags = 0;

  if (!hacks)
    return 0;

  while (*hacks) {
    if (g_str_equal (*hacks,
            "event-port-settings-changed-ndata-parameter-swap"))
      hacks_flags |=
          GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP;
    else if (g_str_equal (*hacks, "event-port-settings-changed-port-0-to-1"))
      hacks_flags |= GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1;
    else if (g_str_equal (*hacks, "video-framerate-integer"))
      hacks_flags |= GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER;
    else if (g_str_equal (*hacks, "syncframe-flag-not-used"))
      hacks_flags |= GST_OMX_HACK_SYNCFRAME_FLAG_NOT_USED;
    else if (g_str_equal (*hacks, "qcom-7x30-fill-this-buffer-race"))
      hacks_flags |= GST_OMX_HACK_QCOM_7x30_FILL_THIS_BUFFER_RACE;
    else
      GST_WARNING ("Unknown hack: %s", *hacks);
    hacks++;
  }

  return hacks_flags;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;
  GError *err = NULL;
  gchar **config_dirs;
  gchar **elements;
  gchar *env_config_dir;
  const gchar *user_config_dir;
  const gchar *const *system_config_dirs;
  gint i, j;
  gsize n_elements;
  static const gchar *config_name[] = { "gstomx.conf", NULL };
  static const gchar *env_config_name[] = { "GST_OMX_CONFIG_DIR", NULL };

  GST_DEBUG_CATEGORY_INIT (gstomx_debug, "omx", 0, "gst-omx");

  gst_omx_element_name_quark =
      g_quark_from_static_string ("gst-omx-element-name");

  /* Read configuration file gstomx.conf from the preferred
   * configuration directories */
  env_config_dir = g_strdup (g_getenv (*env_config_name));
  user_config_dir = g_get_user_config_dir ();
  system_config_dirs = g_get_system_config_dirs ();
  config_dirs =
      g_new (gchar *, g_strv_length ((gchar **) system_config_dirs) + 3);

  i = 0;
  j = 0;
  if (env_config_dir)
    config_dirs[i++] = (gchar *) env_config_dir;
  config_dirs[i++] = (gchar *) user_config_dir;
  while (system_config_dirs[j])
    config_dirs[i++] = (gchar *) system_config_dirs[j++];
  config_dirs[i++] = NULL;

  gst_plugin_add_dependency (plugin, env_config_name,
      (const gchar **) (config_dirs + (env_config_dir ? 1 : 0)), config_name,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  config = g_key_file_new ();
  if (!g_key_file_load_from_dirs (config, *config_name,
          (const gchar **) config_dirs, NULL, G_KEY_FILE_NONE, &err)) {
    GST_ERROR ("Failed to load configuration file: %s", err->message);
    g_error_free (err);
    goto done;
  }

  /* Initialize all types */
  for (i = 0; i < G_N_ELEMENTS (types); i++)
    types[i] ();

  elements = g_key_file_get_groups (config, &n_elements);
  for (i = 0; i < n_elements; i++) {
    GTypeQuery type_query;
    GTypeInfo type_info = { 0, };
    GType type, subtype;
    gchar *type_name, *core_name, *component_name;
    gint rank;

    GST_DEBUG ("Registering element '%s'", elements[i]);

    err = NULL;
    if (!(type_name =
            g_key_file_get_string (config, elements[i], "type-name", &err))) {
      GST_ERROR
          ("Unable to read 'type-name' configuration for element '%s': %s",
          elements[i], err->message);
      g_error_free (err);
      continue;
    }

    type = g_type_from_name (type_name);
    if (type == G_TYPE_INVALID) {
      GST_ERROR ("Invalid type name '%s' for element '%s'", type_name,
          elements[i]);
      g_free (type_name);
      continue;
    }
    if (!g_type_is_a (type, GST_TYPE_ELEMENT)) {
      GST_ERROR ("Type '%s' is no GstElement subtype for element '%s'",
          type_name, elements[i]);
      g_free (type_name);
      continue;
    }
    g_free (type_name);

    /* And now some sanity checking */
    err = NULL;
    if (!(core_name =
            g_key_file_get_string (config, elements[i], "core-name", &err))) {
      GST_ERROR
          ("Unable to read 'core-name' configuration for element '%s': %s",
          elements[i], err->message);
      g_error_free (err);
      continue;
    }
    if (!g_file_test (core_name, G_FILE_TEST_IS_REGULAR)) {
      GST_ERROR ("Core '%s' does not exist for element '%s'", core_name,
          elements[i]);
      g_free (core_name);
      continue;
    }
    g_free (core_name);

    err = NULL;
    if (!(component_name =
            g_key_file_get_string (config, elements[i], "component-name",
                &err))) {
      GST_ERROR
          ("Unable to read 'component-name' configuration for element '%s': %s",
          elements[i], err->message);
      g_error_free (err);
      continue;
    }
    g_free (component_name);

    err = NULL;
    rank = g_key_file_get_integer (config, elements[i], "rank", &err);
    if (err != NULL) {
      GST_ERROR ("No rank set for element '%s': %s", elements[i], err->message);
      g_error_free (err);
      continue;
    }

    /* And now register the type, all other configuration will
     * be handled by the type itself */
    g_type_query (type, &type_query);
    memset (&type_info, 0, sizeof (type_info));
    type_info.class_size = type_query.class_size;
    type_info.instance_size = type_query.instance_size;
    type_name = g_strdup_printf ("%s-%s", g_type_name (type), elements[i]);
    if (g_type_from_name (type_name) != G_TYPE_INVALID) {
      GST_ERROR ("Type '%s' already exists for element '%s'", type_name,
          elements[i]);
      g_free (type_name);
      continue;
    }
    subtype = g_type_register_static (type, type_name, &type_info, 0);
    g_free (type_name);
    g_type_set_qdata (subtype, gst_omx_element_name_quark,
        g_strdup (elements[i]));
    ret |= gst_element_register (plugin, elements[i], rank, subtype);
  }
  g_strfreev (elements);

done:
  g_free (env_config_dir);
  g_free (config_dirs);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "openmax",
    "GStreamer OpenMAX Plug-ins",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
