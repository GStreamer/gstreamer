/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <gst/allocators/gstdmabuf.h>
#include <string.h>

#include "gstomx.h"
#include "gstomxmjpegdec.h"
#include "gstomxmpeg2videodec.h"
#include "gstomxmpeg4videodec.h"
#include "gstomxh264dec.h"
#include "gstomxh263dec.h"
#include "gstomxh265dec.h"
#include "gstomxvp8dec.h"
#include "gstomxtheoradec.h"
#include "gstomxwmvdec.h"
#include "gstomxmpeg4videoenc.h"
#include "gstomxh264enc.h"
#include "gstomxh263enc.h"
#include "gstomxh265enc.h"
#include "gstomxaacdec.h"
#include "gstomxmp3dec.h"
#include "gstomxmp3enc.h"
#include "gstomxaacenc.h"
#include "gstomxamrdec.h"
#include "gstomxanalogaudiosink.h"
#include "gstomxhdmiaudiosink.h"

GST_DEBUG_CATEGORY (gstomx_debug);
#define GST_CAT_DEFAULT gstomx_debug

GST_DEBUG_CATEGORY_STATIC (OMX_API_TRACE);

/* Macros used to log result of OMX calls. Use the requested debug level if the
 * operation succeeded and GST_LEVEL_ERROR if not.
 * Don't consider OMX_ErrorNoMore as an error as it means we're done iterating. */
#define DEBUG_IF_OK(obj,err,...) \
  GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, (err == OMX_ErrorNone || err == OMX_ErrorNoMore) ? GST_LEVEL_DEBUG : GST_LEVEL_ERROR, obj, __VA_ARGS__)
#define INFO_IF_OK(obj,err,...) \
  GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, (err == OMX_ErrorNone || err == OMX_ErrorNoMore) ? GST_LEVEL_INFO : GST_LEVEL_ERROR, obj, __VA_ARGS__)

G_LOCK_DEFINE_STATIC (core_handles);
static GHashTable *core_handles;

/* Cache used by gst_omx_buffer_flags_to_string() */
G_LOCK_DEFINE_STATIC (buffer_flags_str);
static GHashTable *buffer_flags_str;

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
    g_mutex_init (&core->lock);
    core->user_count = 0;
    g_hash_table_insert (core_handles, g_strdup (filename), core);

    /* Hack for the Broadcom OpenMAX IL implementation */
#ifdef USE_OMX_TARGET_RPI
    {
#else
    if (g_str_has_suffix (filename, "vc/lib/libopenmaxil.so")) {
#endif
      gchar *bcm_host_filename;
      gchar *bcm_host_path;
      GModule *bcm_host_module;
      void (*bcm_host_init) (void);

      bcm_host_path = g_path_get_dirname (filename);
      bcm_host_filename =
          g_build_filename (bcm_host_path, "libbcm_host.so", NULL);

      bcm_host_module =
          g_module_open (bcm_host_filename,
          G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

      g_free (bcm_host_filename);
      g_free (bcm_host_path);

      if (!bcm_host_module) {
        /* Retry without an absolute path */
        bcm_host_module =
            g_module_open ("libbcm_host.so",
            G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
        if (!bcm_host_module) {
          GST_ERROR ("Failed to load libbcm_host.so");
          goto error;
        }
      }

      if (!g_module_symbol (bcm_host_module, "bcm_host_init",
              (gpointer *) & bcm_host_init)) {
        GST_ERROR ("Failed to load symbol 'bcm_host_init' from libbcm_host.so");
        goto error;
      }

      bcm_host_init ();
    }

    core->module =
        g_module_open (filename, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
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
    if (!g_module_symbol (core->module, "OMX_SetupTunnel",
            (gpointer *) & core->setup_tunnel))
      goto symbol_error;

    GST_DEBUG ("Successfully loaded core '%s'", filename);
  }

  g_mutex_lock (&core->lock);
  core->user_count++;
  if (core->user_count == 1) {
    OMX_ERRORTYPE err;

    err = core->init ();
    if (err != OMX_ErrorNone) {
      GST_ERROR ("Failed to initialize core '%s': 0x%08x", filename, err);
      g_mutex_unlock (&core->lock);
      goto error;
    }

    GST_DEBUG ("Successfully initialized core '%s'", filename);
  }

  g_mutex_unlock (&core->lock);
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
    g_mutex_clear (&core->lock);
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

  g_mutex_lock (&core->lock);

  GST_DEBUG ("Releasing core %p", core);

  core->user_count--;
  if (core->user_count == 0) {
    GST_DEBUG ("Deinit core %p", core);
    core->deinit ();

    G_LOCK (buffer_flags_str);
    g_clear_pointer (&buffer_flags_str, g_hash_table_unref);
    G_UNLOCK (buffer_flags_str);
  }

  g_mutex_unlock (&core->lock);

  G_UNLOCK (core_handles);
}

/* NOTE: comp->messages_lock will be used */
static void
gst_omx_component_flush_messages (GstOMXComponent * comp)
{
  GstOMXMessage *msg;

  g_mutex_lock (&comp->messages_lock);
  while ((msg = g_queue_pop_head (&comp->messages))) {
    g_slice_free (GstOMXMessage, msg);
  }
  g_mutex_unlock (&comp->messages_lock);
}

static void
gst_omx_buffer_reset (GstOMXBuffer * buf)
{
  buf->omx_buf->nFlags = 0;
  buf->omx_buf->nOffset = 0;
  buf->omx_buf->nFilledLen = 0;
  GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp, G_GUINT64_CONSTANT (0));
}

static void gst_omx_buffer_unmap (GstOMXBuffer * buffer);

/* NOTE: Call with comp->lock, comp->messages_lock will be used */
static void
gst_omx_component_handle_messages (GstOMXComponent * comp)
{
  GstOMXMessage *msg;

  g_mutex_lock (&comp->messages_lock);
  while ((msg = g_queue_pop_head (&comp->messages))) {
    g_mutex_unlock (&comp->messages_lock);

    switch (msg->type) {
      case GST_OMX_MESSAGE_STATE_SET:{
        GST_INFO_OBJECT (comp->parent, "%s state change to %s finished",
            comp->name, gst_omx_state_to_string (msg->content.state_set.state));
        comp->state = msg->content.state_set.state;
        if (comp->state == comp->pending_state)
          comp->pending_state = OMX_StateInvalid;
        break;
      }
      case GST_OMX_MESSAGE_FLUSH:{
        GstOMXPort *port = NULL;
        OMX_U32 index = msg->content.flush.port;

        port = gst_omx_component_get_port (comp, index);
        if (!port)
          break;

        GST_DEBUG_OBJECT (comp->parent, "%s port %u flushed", comp->name,
            port->index);

        if (port->flushing) {
          port->flushed = TRUE;
        } else {
          GST_ERROR_OBJECT (comp->parent, "%s port %u was not flushing",
              comp->name, port->index);
        }

        break;
      }
      case GST_OMX_MESSAGE_ERROR:{
        OMX_ERRORTYPE error = msg->content.error.error;

        if (error == OMX_ErrorNone)
          break;

        GST_ERROR_OBJECT (comp->parent, "%s got error: %s (0x%08x)", comp->name,
            gst_omx_error_to_string (error), error);

        /* We only set the first error ever from which
         * we can't recover anymore.
         */
        if (comp->last_error == OMX_ErrorNone)
          comp->last_error = error;
        g_cond_broadcast (&comp->messages_cond);

        break;
      }
      case GST_OMX_MESSAGE_PORT_ENABLE:{
        GstOMXPort *port = NULL;
        OMX_U32 index = msg->content.port_enable.port;
        OMX_BOOL enable = msg->content.port_enable.enable;

        port = gst_omx_component_get_port (comp, index);
        if (!port)
          break;

        GST_DEBUG_OBJECT (comp->parent, "%s port %u %s", comp->name,
            port->index, (enable ? "enabled" : "disabled"));

        if (enable)
          port->enabled_pending = FALSE;
        else
          port->disabled_pending = FALSE;
        break;
      }
      case GST_OMX_MESSAGE_PORT_SETTINGS_CHANGED:{
        gint i, n;
        OMX_U32 index = msg->content.port_settings_changed.port;
        GList *outports = NULL, *l, *k;

        GST_DEBUG_OBJECT (comp->parent, "%s settings changed (port %u)",
            comp->name, (guint) index);

        /* FIXME: This probably can be done better */

        /* Now update the ports' states */
        n = (comp->ports ? comp->ports->len : 0);
        for (i = 0; i < n; i++) {
          GstOMXPort *port = g_ptr_array_index (comp->ports, i);

          if (index == OMX_ALL || index == port->index) {
            port->settings_cookie++;
            gst_omx_port_update_port_definition (port, NULL);
            if (port->port_def.eDir == OMX_DirOutput && !port->tunneled)
              outports = g_list_prepend (outports, port);
          }
        }

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

        g_list_free (outports);

        break;
      }
      case GST_OMX_MESSAGE_BUFFER_FLAG:{
        GstOMXPort *port = NULL;
        OMX_U32 index = msg->content.buffer_flag.port;
        OMX_U32 flags = msg->content.buffer_flag.flags;

        port = gst_omx_component_get_port (comp, index);
        if (!port)
          break;

        GST_DEBUG_OBJECT (comp->parent,
            "%s port %u got buffer flags 0x%08x (%s)", comp->name, port->index,
            (guint) flags, gst_omx_buffer_flags_to_string (flags));
        if ((flags & OMX_BUFFERFLAG_EOS)
            && port->port_def.eDir == OMX_DirOutput && !port->eos) {
          GST_DEBUG_OBJECT (comp->parent, "%s port %u is EOS", comp->name,
              port->index);
          port->eos = TRUE;
        }

        break;
      }
      case GST_OMX_MESSAGE_BUFFER_DONE:{
        GstOMXBuffer *buf = msg->content.buffer_done.buffer->pAppPrivate;
        GstOMXPort *port;

        port = buf->port;

        buf->used = FALSE;

        if (msg->content.buffer_done.empty) {
          /* Input buffer is empty again and can be used to contain new input */
          GST_LOG_OBJECT (port->comp->parent,
              "%s port %u emptied buffer %p (%p)", port->comp->name,
              port->index, buf, buf->omx_buf->pBuffer);

          /* Reset all flags, some implementations don't
           * reset them themselves and the flags are not
           * valid anymore after the buffer was consumed
           */
          gst_omx_buffer_reset (buf);

          /* Release and unmap the parent buffer, if any */
          gst_omx_buffer_unmap (buf);
        } else {
          /* Output buffer contains output now or
           * the port was flushed */
          GST_LOG_OBJECT (port->comp->parent,
              "%s port %u filled buffer %p (%p)", port->comp->name, port->index,
              buf, buf->omx_buf->pBuffer);

          if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS)
              && port->port_def.eDir == OMX_DirOutput && !port->eos) {
            GST_DEBUG_OBJECT (comp->parent, "%s port %u is EOS", comp->name,
                port->index);
            port->eos = TRUE;
          }
        }

        /* If an input port is managed by a pool, the buffer will be ready to be
         * filled again once it's been released to the pool. */
        if (port->port_def.eDir == OMX_DirOutput || !port->using_pool) {
          g_queue_push_tail (&port->pending_buffers, buf);
        }

        break;
      }
      default:{
        g_assert_not_reached ();
        break;
      }
    }

    g_slice_free (GstOMXMessage, msg);

    g_mutex_lock (&comp->messages_lock);
  }

  g_mutex_unlock (&comp->messages_lock);
}

/* NOTE: comp->messages_lock will be used */
static void
gst_omx_component_send_message (GstOMXComponent * comp, GstOMXMessage * msg)
{
  g_mutex_lock (&comp->messages_lock);
  if (msg)
    g_queue_push_tail (&comp->messages, msg);
  g_cond_broadcast (&comp->messages_cond);
  g_mutex_unlock (&comp->messages_lock);
}

/* NOTE: Call with comp->lock, comp->messages_lock will be used */
static gboolean
gst_omx_component_wait_message (GstOMXComponent * comp, GstClockTime timeout)
{
  gboolean signalled;
  gint64 wait_until = -1;

  if (timeout != GST_CLOCK_TIME_NONE) {
    gint64 add = timeout / (GST_SECOND / G_TIME_SPAN_SECOND);

    if (add == 0)
      return FALSE;

    wait_until = g_get_monotonic_time () + add;
    GST_DEBUG_OBJECT (comp->parent, "%s waiting for %" G_GINT64_FORMAT "us",
        comp->name, add);
  } else {
    GST_DEBUG_OBJECT (comp->parent, "%s waiting for signal", comp->name);
  }

  g_mutex_lock (&comp->messages_lock);
  g_mutex_unlock (&comp->lock);

  if (!g_queue_is_empty (&comp->messages)) {
    signalled = TRUE;
  } else if (timeout == GST_CLOCK_TIME_NONE) {
    g_cond_wait (&comp->messages_cond, &comp->messages_lock);
    signalled = TRUE;
  } else {
    signalled =
        g_cond_wait_until (&comp->messages_cond, &comp->messages_lock,
        wait_until);
  }

  g_mutex_unlock (&comp->messages_lock);
  g_mutex_lock (&comp->lock);

  return signalled;
}

static const gchar *
omx_event_type_to_str (OMX_EVENTTYPE event)
{
  switch (event) {
    case OMX_EventCmdComplete:
      return "EventCmdComplete";
    case OMX_EventError:
      return "EventError";
    case OMX_EventMark:
      return "EventMark";
    case OMX_EventPortSettingsChanged:
      return "EventPortSettingsChanged";
    case OMX_EventBufferFlag:
      return "EventBufferFlag";
    case OMX_EventResourcesAcquired:
      return "EventResourcesAcquired";
    case OMX_EventComponentResumed:
      return "EventComponentResumed";
    case OMX_EventDynamicResourcesAvailable:
      return "EventDynamicResourcesAvailable";
    case OMX_EventPortFormatDetected:
      return "EventPortFormatDetected";
#ifdef OMX_EventIndexSettingChanged
    case OMX_EventIndexSettingChanged:
      return "EventIndexSettingChanged";
#endif
#ifdef OMX_EventPortNeedsDisable
    case OMX_EventPortNeedsDisable:
      return "EventPortNeedsDisable";
#endif
#ifdef OMX_EventPortNeedsFlush
    case OMX_EventPortNeedsFlush:
      return "EventPortNeedsFlush";
#endif
    case OMX_EventKhronosExtensions:
    case OMX_EventVendorStartUnused:
    case OMX_EventMax:
    default:
      break;
  }

  return NULL;
}

/* See "Table 3-11: Event Parameter Usage" */
static GstStructure *
omx_event_to_debug_struct (OMX_EVENTTYPE event,
    guint32 data1, guint32 data2, gpointer event_data)
{
  const gchar *name;

  name = omx_event_type_to_str (event);
  switch (event) {
    case OMX_EventCmdComplete:
    {
      const gchar *cmd = gst_omx_command_to_string (data1);

      if (!cmd)
        break;

      switch (data1) {
        case OMX_CommandStateSet:
          return gst_structure_new (name,
              "command", G_TYPE_STRING, cmd,
              "state-reached", G_TYPE_STRING, gst_omx_state_to_string (data2),
              NULL);
        case OMX_CommandFlush:
        case OMX_CommandPortDisable:
        case OMX_CommandPortEnable:
        case OMX_CommandMarkBuffer:
          return gst_structure_new (name,
              "command", G_TYPE_STRING, cmd, "port", G_TYPE_UINT, data2,
              "error", G_TYPE_STRING,
              gst_omx_error_to_string (GPOINTER_TO_UINT (event_data)), NULL);
        case OMX_CommandKhronosExtensions:
        case OMX_CommandVendorStartUnused:
        case OMX_CommandMax:
          break;
      }
    }
      break;
    case OMX_EventError:
      return gst_structure_new (name, "error", G_TYPE_STRING,
          gst_omx_error_to_string (data1), "extra-info", G_TYPE_STRING,
          gst_omx_error_to_string (data2), NULL);
    case OMX_EventMark:
    case OMX_EventComponentResumed:
    case OMX_EventResourcesAcquired:
    case OMX_EventDynamicResourcesAvailable:
    case OMX_EventPortFormatDetected:
      return gst_structure_new_empty (name);
    case OMX_EventPortSettingsChanged:
#ifdef OMX_EventIndexSettingChanged
    case OMX_EventIndexSettingChanged:
#endif
#ifdef OMX_EventPortNeedsDisable
    case OMX_EventPortNeedsDisable:
#endif
#ifdef OMX_EventPortNeedsFlush
    case OMX_EventPortNeedsFlush:
#endif
      return gst_structure_new (name, "port", G_TYPE_UINT,
          data1, "param-config", G_TYPE_UINT, data2, NULL);
    case OMX_EventBufferFlag:
      return gst_structure_new (name, "port", G_TYPE_UINT,
          data1, "flags", G_TYPE_STRING, gst_omx_buffer_flags_to_string (data2),
          NULL);
    case OMX_EventKhronosExtensions:
    case OMX_EventVendorStartUnused:
    case OMX_EventMax:
    default:
      break;
  }

  return NULL;
}

static void
log_omx_api_trace_event (GstOMXComponent * comp, OMX_EVENTTYPE event,
    guint32 data1, guint32 data2, gpointer event_data)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstStructure *s;

  /* Don't bother creating useless structs if not needed */
  if (gst_debug_category_get_threshold (OMX_API_TRACE) < GST_LEVEL_DEBUG)
    return;

  s = omx_event_to_debug_struct (event, data1, data2, event_data);
  if (!s) {
    GST_CAT_WARNING_OBJECT (OMX_API_TRACE, comp->parent,
        "invalid event 0x%08x Data1 %u Data2 %u EventData %p", event, data1,
        data2, event_data);
    return;
  }

  GST_CAT_DEBUG_OBJECT (OMX_API_TRACE, comp->parent, "%" GST_PTR_FORMAT, s);

  gst_structure_free (s);
#endif /* GST_DISABLE_GST_DEBUG */
}

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
  GstOMXComponent *comp = (GstOMXComponent *) pAppData;

  log_omx_api_trace_event (comp, eEvent, nData1, nData2, pEventData);

  switch (eEvent) {
    case OMX_EventCmdComplete:
    {
      OMX_COMMANDTYPE cmd = (OMX_COMMANDTYPE) nData1;

      GST_DEBUG_OBJECT (comp->parent, "%s %s command complete (%d)",
          comp->name, gst_omx_command_to_string (cmd), cmd);

      switch (cmd) {
        case OMX_CommandStateSet:{
          GstOMXMessage *msg = g_slice_new (GstOMXMessage);

          msg->type = GST_OMX_MESSAGE_STATE_SET;
          msg->content.state_set.state = nData2;

          GST_DEBUG_OBJECT (comp->parent, "%s state change to %s finished",
              comp->name,
              gst_omx_state_to_string (msg->content.state_set.state));

          gst_omx_component_send_message (comp, msg);
          break;
        }
        case OMX_CommandFlush:{
          GstOMXMessage *msg = g_slice_new (GstOMXMessage);

          msg->type = GST_OMX_MESSAGE_FLUSH;
          msg->content.flush.port = nData2;
          GST_DEBUG_OBJECT (comp->parent, "%s port %u flushed", comp->name,
              (guint) msg->content.flush.port);

          gst_omx_component_send_message (comp, msg);
          break;
        }
        case OMX_CommandPortEnable:
        case OMX_CommandPortDisable:{
          GstOMXMessage *msg = g_slice_new (GstOMXMessage);

          msg->type = GST_OMX_MESSAGE_PORT_ENABLE;
          msg->content.port_enable.port = nData2;
          msg->content.port_enable.enable = (cmd == OMX_CommandPortEnable);
          GST_DEBUG_OBJECT (comp->parent, "%s port %u %s", comp->name,
              (guint) msg->content.port_enable.port,
              (msg->content.port_enable.enable ? "enabled" : "disabled"));

          gst_omx_component_send_message (comp, msg);
          break;
        }
        default:
          break;
      }
      break;
    }
    case OMX_EventError:
    {
      GstOMXMessage *msg;
      OMX_ERRORTYPE error_type = nData1;

      /* Yes, this really happens... */
      if (error_type == OMX_ErrorNone)
        break;

      /* Always ignore PortUnpopulated error. This error is informational
       * at best but it is useful for debugging some strange scenarios.
       */
      if (error_type == OMX_ErrorPortUnpopulated) {
        GST_DEBUG_OBJECT (comp->parent, "%s got error: %s (0x%08x)",
            comp->name, gst_omx_error_to_string (error_type), error_type);
        break;
      }

      msg = g_slice_new (GstOMXMessage);

      msg->type = GST_OMX_MESSAGE_ERROR;
      msg->content.error.error = error_type;
      GST_ERROR_OBJECT (comp->parent, "%s got error: %s (0x%08x)", comp->name,
          gst_omx_error_to_string (msg->content.error.error),
          msg->content.error.error);

      gst_omx_component_send_message (comp, msg);
      break;
    }
    case OMX_EventPortSettingsChanged:
    {
      GstOMXMessage *msg = g_slice_new (GstOMXMessage);
      OMX_U32 index;

      if (!(comp->hacks &
              GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_NDATA_PARAMETER_SWAP)) {
        index = nData1;
      } else {
        index = nData2;
      }


      if (index == 0
          && (comp->hacks &
              GST_OMX_HACK_EVENT_PORT_SETTINGS_CHANGED_PORT_0_TO_1))
        index = 1;


      msg->type = GST_OMX_MESSAGE_PORT_SETTINGS_CHANGED;
      msg->content.port_settings_changed.port = index;
      GST_DEBUG_OBJECT (comp->parent, "%s settings changed (port index: %u)",
          comp->name, (guint) msg->content.port_settings_changed.port);

      gst_omx_component_send_message (comp, msg);
      break;
    }
    case OMX_EventBufferFlag:{
      GstOMXMessage *msg;

      msg = g_slice_new (GstOMXMessage);

      msg->type = GST_OMX_MESSAGE_BUFFER_FLAG;
      msg->content.buffer_flag.port = nData1;
      msg->content.buffer_flag.flags = nData2;
      GST_DEBUG_OBJECT (comp->parent, "%s port %u got buffer flags 0x%08x (%s)",
          comp->name, (guint) msg->content.buffer_flag.port,
          (guint) msg->content.buffer_flag.flags,
          gst_omx_buffer_flags_to_string (msg->content.buffer_flag.flags));

      gst_omx_component_send_message (comp, msg);
      break;
    }
    case OMX_EventPortFormatDetected:
    default:
      GST_DEBUG_OBJECT (comp->parent, "%s unknown event 0x%08x", comp->name,
          eEvent);
      break;
  }

  return OMX_ErrorNone;
}

static void
gst_omx_buffer_unmap (GstOMXBuffer * buffer)
{
  g_return_if_fail (buffer != NULL);

  if (buffer->input_frame_mapped) {
    g_assert (!buffer->input_mem);
    g_assert (!buffer->input_buffer);
    g_assert (!buffer->input_buffer_mapped);
    gst_video_frame_unmap (&buffer->input_frame);
    buffer->input_frame_mapped = FALSE;
  } else if (buffer->input_mem) {
    g_assert (!buffer->input_buffer);
    g_assert (!buffer->input_buffer_mapped);
    gst_memory_unmap (buffer->input_mem, &buffer->map);
    g_clear_pointer (&buffer->input_mem, gst_memory_unref);
  } else if (buffer->input_buffer) {
    if (buffer->input_buffer_mapped)
      gst_buffer_unmap (buffer->input_buffer, &buffer->map);
    buffer->input_buffer_mapped = FALSE;
    g_clear_pointer (&buffer->input_buffer, gst_buffer_unref);
  }
}

static void
log_omx_api_trace_buffer (GstOMXComponent * comp, const gchar * event,
    GstOMXBuffer * buf)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstStructure *s;

  /* Don't bother creating useless structs if not needed */
  if (gst_debug_category_get_threshold (OMX_API_TRACE) < GST_LEVEL_TRACE)
    return;

  if (buf) {
    gchar *buf_str, *omx_buf_str, *pbuffer_str;

    /* GST_PTR_FORMAT won't serialize G_TYPE_POINTER fields so stringify pointers */
    buf_str = g_strdup_printf ("%p", buf);
    omx_buf_str = g_strdup_printf ("%p", buf->omx_buf);
    pbuffer_str = g_strdup_printf ("%p", buf->omx_buf->pBuffer);

    /* *INDENT-OFF* */
    s = gst_structure_new (event,
        "GstOMXBuffer", G_TYPE_STRING, buf_str,
        "OMX-buffer", G_TYPE_STRING, omx_buf_str,
        "pBuffer", G_TYPE_STRING, pbuffer_str,
        "TimeStamp", G_TYPE_UINT64, GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp),
        "AllocLen", G_TYPE_UINT, buf->omx_buf->nAllocLen,
        "FilledLen", G_TYPE_UINT, buf->omx_buf->nFilledLen,
        "flags", G_TYPE_UINT, buf->omx_buf->nFlags,
        "flags-str", G_TYPE_STRING, gst_omx_buffer_flags_to_string (buf->omx_buf->nFlags),
        NULL);
    /* *INDENT-ON* */

    g_free (buf_str);
    g_free (omx_buf_str);
    g_free (pbuffer_str);
  } else {
    s = gst_structure_new_empty (event);
  }

  GST_CAT_TRACE_OBJECT (OMX_API_TRACE, comp->parent, "%" GST_PTR_FORMAT, s);

  gst_structure_free (s);
#endif /* GST_DISABLE_GST_DEBUG */
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstOMXBuffer *buf;
  GstOMXComponent *comp;
  GstOMXMessage *msg;

  buf = pBuffer->pAppPrivate;
  if (!buf) {
    GST_ERROR ("Have unknown or deallocated buffer %p", pBuffer);
    return OMX_ErrorNone;
  }

  g_assert (buf->omx_buf == pBuffer);

  if (buf->port->tunneled) {
    GST_ERROR ("EmptyBufferDone on tunneled port");
    return OMX_ErrorBadParameter;
  }

  comp = buf->port->comp;

  msg = g_slice_new (GstOMXMessage);
  msg->type = GST_OMX_MESSAGE_BUFFER_DONE;
  msg->content.buffer_done.component = hComponent;
  msg->content.buffer_done.app_data = pAppData;
  msg->content.buffer_done.buffer = pBuffer;
  msg->content.buffer_done.empty = OMX_TRUE;

  log_omx_api_trace_buffer (comp, "EmptyBufferDone", buf);
  GST_LOG_OBJECT (comp->parent, "%s port %u emptied buffer %p (%p)",
      comp->name, buf->port->index, buf, buf->omx_buf->pBuffer);

  gst_omx_component_send_message (comp, msg);

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstOMXBuffer *buf;
  GstOMXComponent *comp;
  GstOMXMessage *msg;

  buf = pBuffer->pAppPrivate;
  if (!buf) {
    GST_ERROR ("Have unknown or deallocated buffer %p", pBuffer);
    return OMX_ErrorNone;
  }

  g_assert (buf->omx_buf == pBuffer);

  if (buf->port->tunneled) {
    GST_ERROR ("FillBufferDone on tunneled port");
    return OMX_ErrorBadParameter;
  }

  comp = buf->port->comp;

  msg = g_slice_new (GstOMXMessage);
  msg->type = GST_OMX_MESSAGE_BUFFER_DONE;
  msg->content.buffer_done.component = hComponent;
  msg->content.buffer_done.app_data = pAppData;
  msg->content.buffer_done.buffer = pBuffer;
  msg->content.buffer_done.empty = OMX_FALSE;

  log_omx_api_trace_buffer (comp, "FillBufferDone", buf);
  GST_LOG_OBJECT (comp->parent, "%s port %u filled buffer %p (%p)", comp->name,
      buf->port->index, buf, buf->omx_buf->pBuffer);

  gst_omx_component_send_message (comp, msg);

  return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

GST_DEFINE_MINI_OBJECT_TYPE (GstOMXComponent, gst_omx_component);

static void gst_omx_component_free (GstOMXComponent * comp);

/* NOTE: Uses comp->lock and comp->messages_lock */
GstOMXComponent *
gst_omx_component_new (GstObject * parent, const gchar * core_name,
    const gchar * component_name, const gchar * component_role, guint64 hacks)
{
  OMX_ERRORTYPE err;
  GstOMXCore *core;
  GstOMXComponent *comp;
  const gchar *dot;

  core = gst_omx_core_acquire (core_name);
  if (!core)
    return NULL;

  comp = g_slice_new0 (GstOMXComponent);
  comp->core = core;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (comp), 0,
      gst_omx_component_get_type (), NULL, NULL,
      (GstMiniObjectFreeFunction) gst_omx_component_free);

  if ((dot = g_strrstr (component_name, ".")))
    comp->name = g_strdup (dot + 1);
  else
    comp->name = g_strdup (component_name);

  err =
      core->get_handle (&comp->handle, (OMX_STRING) component_name, comp,
      &callbacks);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (parent,
        "Failed to get component handle '%s' from core '%s': 0x%08x",
        component_name, core_name, err);
    gst_omx_core_release (core);
    g_free (comp->name);
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

  g_mutex_init (&comp->lock);
  g_mutex_init (&comp->messages_lock);
  g_cond_init (&comp->messages_cond);

  g_queue_init (&comp->messages);
  comp->pending_state = OMX_StateInvalid;
  comp->last_error = OMX_ErrorNone;

  /* Set component role if any */
  if (component_role && !(hacks & GST_OMX_HACK_NO_COMPONENT_ROLE)) {
    OMX_PARAM_COMPONENTROLETYPE param;

    GST_OMX_INIT_STRUCT (&param);

    g_strlcpy ((gchar *) param.cRole, component_role, sizeof (param.cRole));
    err =
        gst_omx_component_set_parameter (comp,
        OMX_IndexParamStandardComponentRole, &param);

    DEBUG_IF_OK (comp->parent, err,
        "Setting component role to '%s': %s (0x%08x)", component_role,
        gst_omx_error_to_string (err), err);

    /* If setting the role failed this component is unusable */
    if (err != OMX_ErrorNone) {
      gst_omx_component_free (comp);
      return NULL;
    }
  }

  OMX_GetState (comp->handle, &comp->state);

  g_mutex_lock (&comp->lock);
  gst_omx_component_handle_messages (comp);
  g_mutex_unlock (&comp->lock);

  return comp;
}

/* NOTE: Uses comp->messages_lock */
static void
gst_omx_component_free (GstOMXComponent * comp)
{
  gint i, n;

  g_return_if_fail (comp != NULL);

  GST_INFO_OBJECT (comp->parent, "Unloading component %p %s", comp, comp->name);

  if (comp->ports) {
    n = comp->ports->len;
    for (i = 0; i < n; i++) {
      GstOMXPort *port = g_ptr_array_index (comp->ports, i);

      gst_omx_port_deallocate_buffers (port);
      g_assert (port->buffers == NULL);
      g_assert (g_queue_get_length (&port->pending_buffers) == 0);

      g_slice_free (GstOMXPort, port);
    }
    g_ptr_array_unref (comp->ports);
    comp->ports = NULL;
  }

  comp->core->free_handle (comp->handle);
  gst_omx_core_release (comp->core);

  gst_omx_component_flush_messages (comp);

  g_cond_clear (&comp->messages_cond);
  g_mutex_clear (&comp->messages_lock);
  g_mutex_clear (&comp->lock);

  gst_object_unref (comp->parent);

  g_free (comp->name);
  comp->name = NULL;

  g_slice_free (GstOMXComponent, comp);
}

GstOMXComponent *
gst_omx_component_ref (GstOMXComponent * comp)
{
  g_return_val_if_fail (comp, NULL);

  gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
  return comp;
}

void
gst_omx_component_unref (GstOMXComponent * comp)
{
  g_return_if_fail (comp);

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}

static GstStructure *
omx_command_to_debug_struct (OMX_COMMANDTYPE cmd,
    guint32 param, gpointer cmd_data)
{
  const gchar *cmd_str;

  cmd_str = gst_omx_command_to_string (cmd);

  switch (cmd) {
    case OMX_CommandStateSet:
      return gst_structure_new ("SendCommand",
          "command", G_TYPE_STRING, cmd_str,
          "state", G_TYPE_STRING, gst_omx_state_to_string (param), NULL);
    case OMX_CommandFlush:
    case OMX_CommandPortDisable:
    case OMX_CommandPortEnable:
      return gst_structure_new ("SendCommand",
          "command", G_TYPE_STRING, cmd_str, "port", G_TYPE_UINT, param, NULL);
    case OMX_CommandMarkBuffer:
      return gst_structure_new ("SendCommand",
          "command", G_TYPE_STRING, cmd_str,
          "mark-type", G_TYPE_POINTER, cmd_data, NULL);
    case OMX_CommandKhronosExtensions:
    case OMX_CommandVendorStartUnused:
    case OMX_CommandMax:
    default:
      break;
  }

  return NULL;
}

static void
log_omx_api_trace_send_command (GstOMXComponent * comp, OMX_COMMANDTYPE cmd,
    guint32 param, gpointer cmd_data)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstStructure *s;

  /* Don't bother creating useless structs if not needed */
  if (gst_debug_category_get_threshold (OMX_API_TRACE) < GST_LEVEL_DEBUG)
    return;

  s = omx_command_to_debug_struct (cmd, param, cmd_data);
  if (!s) {
    GST_CAT_WARNING_OBJECT (OMX_API_TRACE, comp->parent,
        "invalid command 0x%08x Param %u CmdData %p", cmd, param, cmd_data);
    return;
  }

  GST_CAT_DEBUG_OBJECT (OMX_API_TRACE, comp->parent, "%" GST_PTR_FORMAT, s);

  gst_structure_free (s);
#endif /* GST_DISABLE_GST_DEBUG */
}

static OMX_ERRORTYPE
gst_omx_component_send_command (GstOMXComponent * comp, OMX_COMMANDTYPE cmd,
    guint32 param, gpointer cmd_data)
{
  OMX_ERRORTYPE err;

  log_omx_api_trace_send_command (comp, cmd, param, cmd_data);
  err = OMX_SendCommand (comp->handle, cmd, param, cmd_data);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_component_set_state (GstOMXComponent * comp, OMX_STATETYPE state)
{
  OMX_STATETYPE old_state;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&comp->lock);

  gst_omx_component_handle_messages (comp);

  old_state = comp->state;
  GST_INFO_OBJECT (comp->parent, "Setting %s state from %s to %s", comp->name,
      gst_omx_state_to_string (old_state), gst_omx_state_to_string (state));

  if ((err = comp->last_error) != OMX_ErrorNone && state > old_state) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  if (old_state == state || comp->pending_state == state) {
    GST_DEBUG_OBJECT (comp->parent, "Component %s already in state %s",
        comp->name, gst_omx_state_to_string (state));
    goto done;
  }

  comp->pending_state = state;

  /* Reset some things */
  if ((old_state == OMX_StateExecuting || old_state == OMX_StatePause)
      && state < old_state) {
    g_list_free (comp->pending_reconfigure_outports);
    comp->pending_reconfigure_outports = NULL;
    /* Notify all inports that are still waiting */
    gst_omx_component_send_message (comp, NULL);
  }

  err = gst_omx_component_send_command (comp, OMX_CommandStateSet, state, NULL);
  /* No need to check if anything has changed here */

done:

  gst_omx_component_handle_messages (comp);

  if (err != OMX_ErrorNone && comp->last_error == OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Last operation returned an error. Setting last_error manually.");
    comp->last_error = err;
  }

  g_mutex_unlock (&comp->lock);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Error setting %s state from %s to %s: %s (0x%08x)", comp->name,
        gst_omx_state_to_string (old_state), gst_omx_state_to_string (state),
        gst_omx_error_to_string (err), err);
  }
  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_STATETYPE
gst_omx_component_get_state (GstOMXComponent * comp, GstClockTime timeout)
{
  OMX_STATETYPE ret;
  gboolean signalled = TRUE;

  g_return_val_if_fail (comp != NULL, OMX_StateInvalid);

  GST_DEBUG_OBJECT (comp->parent, "Getting state of %s", comp->name);

  g_mutex_lock (&comp->lock);

  gst_omx_component_handle_messages (comp);

  if (comp->last_error != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (comp->last_error),
        comp->last_error);
    ret = OMX_StateInvalid;
    goto done;
  }

  ret = comp->state;
  if (comp->pending_state == OMX_StateInvalid)
    goto done;

  while (signalled && comp->last_error == OMX_ErrorNone
      && comp->pending_state != OMX_StateInvalid) {

    signalled = gst_omx_component_wait_message (comp, timeout);
    if (signalled)
      gst_omx_component_handle_messages (comp);
  };

  if (signalled) {
    if (comp->last_error != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "%s got error while waiting for state change: %s (0x%08x)",
          comp->name, gst_omx_error_to_string (comp->last_error),
          comp->last_error);
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
    GST_WARNING_OBJECT (comp->parent, "%s timeout while waiting for state "
        "change", comp->name);
  }

done:
  g_mutex_unlock (&comp->lock);

  GST_DEBUG_OBJECT (comp->parent, "%s returning state %s", comp->name,
      gst_omx_state_to_string (ret));

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

  GST_DEBUG_OBJECT (comp->parent, "%s adding port %u", comp->name, index);

  GST_OMX_INIT_STRUCT (&port_def);
  port_def.nPortIndex = index;

  err = gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "%s failed to add port %u: %s (0x%08x)",
        comp->name, index, gst_omx_error_to_string (err), err);
    return NULL;
  }

  port = g_slice_new0 (GstOMXPort);
  port->comp = comp;
  port->index = index;

  port->tunneled = FALSE;

  port->port_def = port_def;

  g_queue_init (&port->pending_buffers);
  port->flushing = TRUE;
  port->flushed = FALSE;
  port->enabled_pending = FALSE;
  port->disabled_pending = FALSE;
  port->eos = FALSE;
  port->using_pool = FALSE;

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

  n = comp->ports->len;
  for (i = 0; i < n; i++) {
    GstOMXPort *tmp = g_ptr_array_index (comp->ports, i);

    if (tmp->index == index)
      return tmp;
  }
  return NULL;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_component_get_last_error (GstOMXComponent * comp)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&comp->lock);
  gst_omx_component_handle_messages (comp);
  err = comp->last_error;
  g_mutex_unlock (&comp->lock);

  GST_DEBUG_OBJECT (comp->parent, "Returning last %s error: %s (0x%08x)",
      comp->name, gst_omx_error_to_string (err), err);

  return err;
}

const gchar *
gst_omx_component_get_last_error_string (GstOMXComponent * comp)
{
  g_return_val_if_fail (comp != NULL, NULL);

  return gst_omx_error_to_string (gst_omx_component_get_last_error (comp));
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
omx_index_type_to_str (OMX_INDEXTYPE index)
{
  switch (index) {
    case OMX_IndexComponentStartUnused:
      return "OMX_IndexComponentStartUnused";
    case OMX_IndexParamPriorityMgmt:
      return "OMX_IndexParamPriorityMgmt";
    case OMX_IndexParamAudioInit:
      return "OMX_IndexParamAudioInit";
    case OMX_IndexParamImageInit:
      return "OMX_IndexParamImageInit";
    case OMX_IndexParamVideoInit:
      return "OMX_IndexParamVideoInit";
    case OMX_IndexParamOtherInit:
      return "OMX_IndexParamOtherInit";
    case OMX_IndexParamNumAvailableStreams:
      return "OMX_IndexParamNumAvailableStreams";
    case OMX_IndexParamActiveStream:
      return "OMX_IndexParamActiveStream";
    case OMX_IndexParamSuspensionPolicy:
      return "OMX_IndexParamSuspensionPolicy";
    case OMX_IndexParamComponentSuspended:
      return "OMX_IndexParamComponentSuspended";
    case OMX_IndexConfigCapturing:
      return "OMX_IndexConfigCapturing";
    case OMX_IndexConfigCaptureMode:
      return "OMX_IndexConfigCaptureMode";
    case OMX_IndexAutoPauseAfterCapture:
      return "OMX_IndexAutoPauseAfterCapture";
    case OMX_IndexParamContentURI:
      return "OMX_IndexParamContentURI";
    case OMX_IndexParamDisableResourceConcealment:
      return "OMX_IndexParamDisableResourceConcealment";
    case OMX_IndexConfigMetadataItemCount:
      return "OMX_IndexConfigMetadataItemCount";
    case OMX_IndexConfigContainerNodeCount:
      return "OMX_IndexConfigContainerNodeCount";
    case OMX_IndexConfigMetadataItem:
      return "OMX_IndexConfigMetadataItem";
    case OMX_IndexConfigCounterNodeID:
      return "OMX_IndexConfigCounterNodeID";
    case OMX_IndexParamMetadataFilterType:
      return "OMX_IndexParamMetadataFilterType";
    case OMX_IndexParamMetadataKeyFilter:
      return "OMX_IndexParamMetadataKeyFilter";
    case OMX_IndexConfigPriorityMgmt:
      return "OMX_IndexConfigPriorityMgmt";
    case OMX_IndexParamStandardComponentRole:
      return "OMX_IndexParamStandardComponentRole";
    case OMX_IndexPortStartUnused:
      return "OMX_IndexPortStartUnused";
    case OMX_IndexParamPortDefinition:
      return "OMX_IndexParamPortDefinition";
    case OMX_IndexParamCompBufferSupplier:
      return "OMX_IndexParamCompBufferSupplier";
    case OMX_IndexReservedStartUnused:
      return "OMX_IndexReservedStartUnused";
    case OMX_IndexAudioStartUnused:
      return "OMX_IndexAudioStartUnused";
    case OMX_IndexParamAudioPortFormat:
      return "OMX_IndexParamAudioPortFormat";
    case OMX_IndexParamAudioPcm:
      return "OMX_IndexParamAudioPcm";
    case OMX_IndexParamAudioAac:
      return "OMX_IndexParamAudioAac";
    case OMX_IndexParamAudioRa:
      return "OMX_IndexParamAudioRa";
    case OMX_IndexParamAudioMp3:
      return "OMX_IndexParamAudioMp3";
    case OMX_IndexParamAudioAdpcm:
      return "OMX_IndexParamAudioAdpcm";
    case OMX_IndexParamAudioG723:
      return "OMX_IndexParamAudioG723";
    case OMX_IndexParamAudioG729:
      return "OMX_IndexParamAudioG729";
    case OMX_IndexParamAudioAmr:
      return "OMX_IndexParamAudioAmr";
    case OMX_IndexParamAudioWma:
      return "OMX_IndexParamAudioWma";
    case OMX_IndexParamAudioSbc:
      return "OMX_IndexParamAudioSbc";
    case OMX_IndexParamAudioMidi:
      return "OMX_IndexParamAudioMidi";
    case OMX_IndexParamAudioGsm_FR:
      return "OMX_IndexParamAudioGsm_FR";
    case OMX_IndexParamAudioMidiLoadUserSound:
      return "OMX_IndexParamAudioMidiLoadUserSound";
    case OMX_IndexParamAudioG726:
      return "OMX_IndexParamAudioG726";
    case OMX_IndexParamAudioGsm_EFR:
      return "OMX_IndexParamAudioGsm_EFR";
    case OMX_IndexParamAudioGsm_HR:
      return "OMX_IndexParamAudioGsm_HR";
    case OMX_IndexParamAudioPdc_FR:
      return "OMX_IndexParamAudioPdc_FR";
    case OMX_IndexParamAudioPdc_EFR:
      return "OMX_IndexParamAudioPdc_EFR";
    case OMX_IndexParamAudioPdc_HR:
      return "OMX_IndexParamAudioPdc_HR";
    case OMX_IndexParamAudioTdma_FR:
      return "OMX_IndexParamAudioTdma_FR";
    case OMX_IndexParamAudioTdma_EFR:
      return "OMX_IndexParamAudioTdma_EFR";
    case OMX_IndexParamAudioQcelp8:
      return "OMX_IndexParamAudioQcelp8";
    case OMX_IndexParamAudioQcelp13:
      return "OMX_IndexParamAudioQcelp13";
    case OMX_IndexParamAudioEvrc:
      return "OMX_IndexParamAudioEvrc";
    case OMX_IndexParamAudioSmv:
      return "OMX_IndexParamAudioSmv";
    case OMX_IndexParamAudioVorbis:
      return "OMX_IndexParamAudioVorbis";
    case OMX_IndexConfigAudioMidiImmediateEvent:
      return "OMX_IndexConfigAudioMidiImmediateEvent";
    case OMX_IndexConfigAudioMidiControl:
      return "OMX_IndexConfigAudioMidiControl";
    case OMX_IndexConfigAudioMidiSoundBankProgram:
      return "OMX_IndexConfigAudioMidiSoundBankProgram";
    case OMX_IndexConfigAudioMidiStatus:
      return "OMX_IndexConfigAudioMidiStatus";
    case OMX_IndexConfigAudioMidiMetaEvent:
      return "OMX_IndexConfigAudioMidiMetaEvent";
    case OMX_IndexConfigAudioMidiMetaEventData:
      return "OMX_IndexConfigAudioMidiMetaEventData";
    case OMX_IndexConfigAudioVolume:
      return "OMX_IndexConfigAudioVolume";
    case OMX_IndexConfigAudioBalance:
      return "OMX_IndexConfigAudioBalance";
    case OMX_IndexConfigAudioChannelMute:
      return "OMX_IndexConfigAudioChannelMute";
    case OMX_IndexConfigAudioMute:
      return "OMX_IndexConfigAudioMute";
    case OMX_IndexConfigAudioLoudness:
      return "OMX_IndexConfigAudioLoudness";
    case OMX_IndexConfigAudioEchoCancelation:
      return "OMX_IndexConfigAudioEchoCancelation";
    case OMX_IndexConfigAudioNoiseReduction:
      return "OMX_IndexConfigAudioNoiseReduction";
    case OMX_IndexConfigAudioBass:
      return "OMX_IndexConfigAudioBass";
    case OMX_IndexConfigAudioTreble:
      return "OMX_IndexConfigAudioTreble";
    case OMX_IndexConfigAudioStereoWidening:
      return "OMX_IndexConfigAudioStereoWidening";
    case OMX_IndexConfigAudioChorus:
      return "OMX_IndexConfigAudioChorus";
    case OMX_IndexConfigAudioEqualizer:
      return "OMX_IndexConfigAudioEqualizer";
    case OMX_IndexConfigAudioReverberation:
      return "OMX_IndexConfigAudioReverberation";
    case OMX_IndexConfigAudioChannelVolume:
      return "OMX_IndexConfigAudioChannelVolume";
    case OMX_IndexImageStartUnused:
      return "OMX_IndexImageStartUnused";
    case OMX_IndexParamImagePortFormat:
      return "OMX_IndexParamImagePortFormat";
    case OMX_IndexParamFlashControl:
      return "OMX_IndexParamFlashControl";
    case OMX_IndexConfigFocusControl:
      return "OMX_IndexConfigFocusControl";
    case OMX_IndexParamQFactor:
      return "OMX_IndexParamQFactor";
    case OMX_IndexParamQuantizationTable:
      return "OMX_IndexParamQuantizationTable";
    case OMX_IndexParamHuffmanTable:
      return "OMX_IndexParamHuffmanTable";
    case OMX_IndexConfigFlashControl:
      return "OMX_IndexConfigFlashControl";
    case OMX_IndexVideoStartUnused:
      return "OMX_IndexVideoStartUnused";
    case OMX_IndexParamVideoPortFormat:
      return "OMX_IndexParamVideoPortFormat";
    case OMX_IndexParamVideoQuantization:
      return "OMX_IndexParamVideoQuantization";
    case OMX_IndexParamVideoFastUpdate:
      return "OMX_IndexParamVideoFastUpdate";
    case OMX_IndexParamVideoBitrate:
      return "OMX_IndexParamVideoBitrate";
    case OMX_IndexParamVideoMotionVector:
      return "OMX_IndexParamVideoMotionVector";
    case OMX_IndexParamVideoIntraRefresh:
      return "OMX_IndexParamVideoIntraRefresh";
    case OMX_IndexParamVideoErrorCorrection:
      return "OMX_IndexParamVideoErrorCorrection";
    case OMX_IndexParamVideoVBSMC:
      return "OMX_IndexParamVideoVBSMC";
    case OMX_IndexParamVideoMpeg2:
      return "OMX_IndexParamVideoMpeg2";
    case OMX_IndexParamVideoMpeg4:
      return "OMX_IndexParamVideoMpeg4";
    case OMX_IndexParamVideoWmv:
      return "OMX_IndexParamVideoWmv";
    case OMX_IndexParamVideoRv:
      return "OMX_IndexParamVideoRv";
    case OMX_IndexParamVideoAvc:
      return "OMX_IndexParamVideoAvc";
    case OMX_IndexParamVideoH263:
      return "OMX_IndexParamVideoH263";
    case OMX_IndexParamVideoProfileLevelQuerySupported:
      return "OMX_IndexParamVideoProfileLevelQuerySupported";
    case OMX_IndexParamVideoProfileLevelCurrent:
      return "OMX_IndexParamVideoProfileLevelCurrent";
    case OMX_IndexConfigVideoBitrate:
      return "OMX_IndexConfigVideoBitrate";
    case OMX_IndexConfigVideoFramerate:
      return "OMX_IndexConfigVideoFramerate";
    case OMX_IndexConfigVideoIntraVOPRefresh:
      return "OMX_IndexConfigVideoIntraVOPRefresh";
    case OMX_IndexConfigVideoIntraMBRefresh:
      return "OMX_IndexConfigVideoIntraMBRefresh";
    case OMX_IndexConfigVideoMBErrorReporting:
      return "OMX_IndexConfigVideoMBErrorReporting";
    case OMX_IndexParamVideoMacroblocksPerFrame:
      return "OMX_IndexParamVideoMacroblocksPerFrame";
    case OMX_IndexConfigVideoMacroBlockErrorMap:
      return "OMX_IndexConfigVideoMacroBlockErrorMap";
    case OMX_IndexParamVideoSliceFMO:
      return "OMX_IndexParamVideoSliceFMO";
    case OMX_IndexConfigVideoAVCIntraPeriod:
      return "OMX_IndexConfigVideoAVCIntraPeriod";
    case OMX_IndexConfigVideoNalSize:
      return "OMX_IndexConfigVideoNalSize";
    case OMX_IndexCommonStartUnused:
      return "OMX_IndexCommonStartUnused";
    case OMX_IndexParamCommonDeblocking:
      return "OMX_IndexParamCommonDeblocking";
    case OMX_IndexParamCommonSensorMode:
      return "OMX_IndexParamCommonSensorMode";
    case OMX_IndexParamCommonInterleave:
      return "OMX_IndexParamCommonInterleave";
    case OMX_IndexConfigCommonColorFormatConversion:
      return "OMX_IndexConfigCommonColorFormatConversion";
    case OMX_IndexConfigCommonScale:
      return "OMX_IndexConfigCommonScale";
    case OMX_IndexConfigCommonImageFilter:
      return "OMX_IndexConfigCommonImageFilter";
    case OMX_IndexConfigCommonColorEnhancement:
      return "OMX_IndexConfigCommonColorEnhancement";
    case OMX_IndexConfigCommonColorKey:
      return "OMX_IndexConfigCommonColorKey";
    case OMX_IndexConfigCommonColorBlend:
      return "OMX_IndexConfigCommonColorBlend";
    case OMX_IndexConfigCommonFrameStabilisation:
      return "OMX_IndexConfigCommonFrameStabilisation";
    case OMX_IndexConfigCommonRotate:
      return "OMX_IndexConfigCommonRotate";
    case OMX_IndexConfigCommonMirror:
      return "OMX_IndexConfigCommonMirror";
    case OMX_IndexConfigCommonOutputPosition:
      return "OMX_IndexConfigCommonOutputPosition";
    case OMX_IndexConfigCommonInputCrop:
      return "OMX_IndexConfigCommonInputCrop";
    case OMX_IndexConfigCommonOutputCrop:
      return "OMX_IndexConfigCommonOutputCrop";
    case OMX_IndexConfigCommonDigitalZoom:
      return "OMX_IndexConfigCommonDigitalZoom";
    case OMX_IndexConfigCommonOpticalZoom:
      return "OMX_IndexConfigCommonOpticalZoom";
    case OMX_IndexConfigCommonWhiteBalance:
      return "OMX_IndexConfigCommonWhiteBalance";
    case OMX_IndexConfigCommonExposure:
      return "OMX_IndexConfigCommonExposure";
    case OMX_IndexConfigCommonContrast:
      return "OMX_IndexConfigCommonContrast";
    case OMX_IndexConfigCommonBrightness:
      return "OMX_IndexConfigCommonBrightness";
    case OMX_IndexConfigCommonBacklight:
      return "OMX_IndexConfigCommonBacklight";
    case OMX_IndexConfigCommonGamma:
      return "OMX_IndexConfigCommonGamma";
    case OMX_IndexConfigCommonSaturation:
      return "OMX_IndexConfigCommonSaturation";
    case OMX_IndexConfigCommonLightness:
      return "OMX_IndexConfigCommonLightness";
    case OMX_IndexConfigCommonExclusionRect:
      return "OMX_IndexConfigCommonExclusionRect";
    case OMX_IndexConfigCommonDithering:
      return "OMX_IndexConfigCommonDithering";
    case OMX_IndexConfigCommonPlaneBlend:
      return "OMX_IndexConfigCommonPlaneBlend";
    case OMX_IndexConfigCommonExposureValue:
      return "OMX_IndexConfigCommonExposureValue";
    case OMX_IndexConfigCommonOutputSize:
      return "OMX_IndexConfigCommonOutputSize";
    case OMX_IndexParamCommonExtraQuantData:
      return "OMX_IndexParamCommonExtraQuantData";
    case OMX_IndexConfigCommonTransitionEffect:
      return "OMX_IndexConfigCommonTransitionEffect";
    case OMX_IndexOtherStartUnused:
      return "OMX_IndexOtherStartUnused";
    case OMX_IndexParamOtherPortFormat:
      return "OMX_IndexParamOtherPortFormat";
    case OMX_IndexConfigOtherPower:
      return "OMX_IndexConfigOtherPower";
    case OMX_IndexConfigOtherStats:
      return "OMX_IndexConfigOtherStats";
    case OMX_IndexTimeStartUnused:
      return "OMX_IndexTimeStartUnused";
    case OMX_IndexConfigTimeScale:
      return "OMX_IndexConfigTimeScale";
    case OMX_IndexConfigTimeClockState:
      return "OMX_IndexConfigTimeClockState";
    case OMX_IndexConfigTimeCurrentMediaTime:
      return "OMX_IndexConfigTimeCurrentMediaTime";
    case OMX_IndexConfigTimeCurrentWallTime:
      return "OMX_IndexConfigTimeCurrentWallTime";
    case OMX_IndexConfigTimeMediaTimeRequest:
      return "OMX_IndexConfigTimeMediaTimeRequest";
    case OMX_IndexConfigTimeClientStartTime:
      return "OMX_IndexConfigTimeClientStartTime";
    case OMX_IndexConfigTimePosition:
      return "OMX_IndexConfigTimePosition";
    case OMX_IndexConfigTimeSeekMode:
      return "OMX_IndexConfigTimeSeekMode";
    case OMX_IndexKhronosExtensions:
      return "OMX_IndexKhronosExtensions";
    case OMX_IndexVendorStartUnused:
      return "OMX_IndexVendorStartUnused";
    case OMX_IndexMax:
      return "OMX_IndexMax";
    default:
      break;
  }

#if OMX_VERSION_MINOR == 1
  switch (index) {
    case OMX_IndexParamCustomContentPipe:
      return "OMX_IndexParamCustomContentPipe";
    case OMX_IndexConfigCommonFocusRegion:
      return "OMX_IndexConfigCommonFocusRegion";
    case OMX_IndexConfigCommonFocusStatus:
      return "OMX_IndexConfigCommonFocusStatus";
    case OMX_IndexConfigTimeActiveRefClock:
      return "OMX_IndexConfigTimeActiveRefClock";
    case OMX_IndexConfigTimeCurrentAudioReference:
      return "OMX_IndexConfigTimeCurrentAudioReference";
    case OMX_IndexConfigTimeCurrentVideoReference:
      return "OMX_IndexConfigTimeCurrentVideoReference";
    default:
      break;
  }
#endif

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  switch ((OMX_ALG_INDEXTYPE) index) {
    case OMX_ALG_IndexVendorComponentStartUnused:
      return "OMX_ALG_IndexVendorComponentStartUnused";
    case OMX_ALG_IndexParamReportedLatency:
      return "OMX_ALG_IndexParamReportedLatency";
    case OMX_ALG_IndexParamPreallocation:
      return "OMX_ALG_IndexParamPreallocation";
    case OMX_ALG_IndexVendorPortStartUnused:
      return "OMX_ALG_IndexVendorPortStartUnused";
    case OMX_ALG_IndexPortParamBufferMode:
      return "OMX_ALG_IndexPortParamBufferMode";
    case OMX_ALG_IndexParamVendorVideoStartUnused:
      return "OMX_ALG_IndexParamVendorVideoStartUnused";
    case OMX_ALG_IndexParamVideoHevc:
      return "OMX_ALG_IndexParamVideoHevc";
    case OMX_ALG_IndexParamVideoVp9:
      return "OMX_ALG_IndexParamVideoVp9";
    case OMX_ALG_IndexParamVideoGopControl:
      return "OMX_ALG_IndexParamVideoGopControl";
    case OMX_ALG_IndexParamVideoSlices:
      return "OMX_ALG_IndexParamVideoSlices";
    case OMX_ALG_IndexParamVideoSceneChangeResilience:
      return "OMX_ALG_IndexParamVideoSceneChangeResilience";
    case OMX_ALG_IndexParamVideoPrefetchBuffer:
      return "OMX_ALG_IndexParamVideoPrefetchBuffer";
    case OMX_ALG_IndexParamVideoCodedPictureBuffer:
      return "OMX_ALG_IndexParamVideoCodedPictureBuffer";
    case OMX_ALG_IndexParamVideoQuantizationControl:
      return "OMX_ALG_IndexParamVideoQuantizationControl";
    case OMX_ALG_IndexParamVideoQuantizationExtension:
      return "OMX_ALG_IndexParamVideoQuantizationExtension";
    case OMX_ALG_IndexParamVideoScalingList:
      return "OMX_ALG_IndexParamVideoScalingList";
    case OMX_ALG_IndexParamVideoDecodedPictureBuffer:
      return "OMX_ALG_IndexParamVideoDecodedPictureBuffer";
    case OMX_ALG_IndexParamVideoInternalEntropyBuffers:
      return "OMX_ALG_IndexParamVideoInternalEntropyBuffers";
    case OMX_ALG_IndexParamVideoLowBandwidth:
      return "OMX_ALG_IndexParamVideoLowBandwidth";
    case OMX_ALG_IndexParamVideoAspectRatio:
      return "OMX_ALG_IndexParamVideoAspectRatio";
    case OMX_ALG_IndexParamVideoSubframe:
      return "OMX_ALG_IndexParamVideoSubframe";
    case OMX_ALG_IndexParamVideoInstantaneousDecodingRefresh:
      return "OMX_ALG_IndexParamVideoInstantaneousDecodingRefresh";
    case OMX_ALG_IndexParamVideoMaxBitrate:
      return "OMX_ALG_IndexParamVideoMaxBitrate";
    case OMX_ALG_IndexParamVideoFillerData:
      return "OMX_ALG_IndexParamVideoFillerData";
    case OMX_ALG_IndexParamVideoBufferMode:
      return "OMX_ALG_IndexParamVideoBufferMode";
    case OMX_ALG_IndexParamVideoInterlaceFormatCurrent:
      return "OMX_ALG_IndexParamVideoInterlaceFormatCurrent";
    case OMX_ALG_IndexParamVideoLongTerm:
      return "OMX_ALG_IndexParamVideoLongTerm";
    case OMX_ALG_IndexParamVideoLookAhead:
      return "OMX_ALG_IndexParamVideoLookAhead";
    case OMX_ALG_IndexConfigVendorVideoStartUnused:
      return "OMX_ALG_IndexConfigVendorVideoStartUnused";
    case OMX_ALG_IndexConfigVideoInsertInstantaneousDecodingRefresh:
      return "OMX_ALG_IndexConfigVideoInsertInstantaneousDecodingRefresh";
    case OMX_ALG_IndexConfigVideoGroupOfPictures:
      return "OMX_ALG_IndexConfigVideoGroupOfPictures";
    case OMX_ALG_IndexConfigVideoRegionOfInterest:
      return "OMX_ALG_IndexConfigVideoRegionOfInterest";
    case OMX_ALG_IndexConfigVideoNotifySceneChange:
      return "OMX_ALG_IndexConfigVideoNotifySceneChange";
    case OMX_ALG_IndexConfigVideoInsertLongTerm:
      return "OMX_ALG_IndexConfigVideoInsertLongTerm";
    case OMX_ALG_IndexConfigVideoUseLongTerm:
      return "OMX_ALG_IndexConfigVideoUseLongTerm";
    case OMX_ALG_IndexVendorCommonStartUnused:
      return "OMX_ALG_IndexVendorCommonStartUnused";
    case OMX_ALG_IndexParamCommonSequencePictureModeCurrent:
      return "OMX_ALG_IndexParamCommonSequencePictureModeCurrent";
    case OMX_ALG_IndexParamCommonSequencePictureModeQuerySupported:
      return "OMX_ALG_IndexParamCommonSequencePictureModeQuerySupported";
    case OMX_ALG_IndexParamVideoTwoPass:
      return "OMX_ALG_IndexParamVideoTwoPass";
    case OMX_ALG_IndexParamVideoColorPrimaries:
      return "OMX_ALG_IndexParamVideoColorPrimaries";
    case OMX_ALG_IndexParamVideoSkipFrame:
      return "OMX_ALG_IndexParamVideoSkipFrame";
    case OMX_ALG_IndexConfigVideoNotifyResolutionChange:
      return "OMX_ALG_IndexConfigVideoNotifyResolutionChange";
    case OMX_ALG_IndexConfigVideoInsertPrefixSEI:
      return "OMX_ALG_IndexConfigVideoInsertPrefixSEI";
    case OMX_ALG_IndexConfigVideoInsertSuffixSEI:
      return "OMX_ALG_IndexConfigVideoInsertSuffixSEI";
    case OMX_ALG_IndexConfigVideoQuantizationParameterTable:
      return "OMX_ALG_IndexConfigVideoQuantizationParameterTable";
    case OMX_ALG_IndexParamVideoInputParsed:
      return "OMX_ALG_IndexParamVideoInputParsed";
    case OMX_ALG_IndexParamVideoMaxPictureSize:
      return "OMX_ALG_IndexParamVideoMaxPictureSize";
    case OMX_ALG_IndexParamVideoMaxPictureSizes:
      return "OMX_ALG_IndexParamVideoMaxPictureSizes";
    case OMX_ALG_IndexConfigVideoLoopFilterBeta:
      return "OMX_ALG_IndexConfigVideoLoopFilterBeta";
    case OMX_ALG_IndexConfigVideoLoopFilterTc:
      return "OMX_ALG_IndexConfigVideoLoopFilterTc";
    case OMX_ALG_IndexParamVideoLoopFilterBeta:
      return "OMX_ALG_IndexParamVideoLoopFilterBeta";
    case OMX_ALG_IndexParamVideoLoopFilterTc:
      return "OMX_ALG_IndexParamVideoLoopFilterTc";
    case OMX_ALG_IndexPortParamEarlyCallback:
      return "OMX_ALG_IndexPortParamEarlyCallback";
    case OMX_ALG_IndexParamVideoTransferCharacteristics:
      return "OMX_ALG_IndexParamVideoTransferCharacteristics";
    case OMX_ALG_IndexParamVideoColorMatrix:
      return "OMX_ALG_IndexParamVideoColorMatrix";
    case OMX_ALG_IndexConfigVideoTransferCharacteristics:
      return "OMX_ALG_IndexConfigVideoTransferCharacteristics";
    case OMX_ALG_IndexConfigVideoColorMatrix:
      return "OMX_ALG_IndexConfigVideoColorMatrix";
    case OMX_ALG_IndexConfigVideoHighDynamicRangeSEI:
      return "OMX_ALG_IndexConfigVideoHighDynamicRangeSEI";
    case OMX_ALG_IndexConfigVideoMaxResolutionChange:
      return "OMX_ALG_IndexConfigVideoMaxResolutionChange";
    case OMX_ALG_IndexParamVideoQuantizationTable:
      return "OMX_ALG_IndexParamVideoQuantizationTable";
    case OMX_ALG_IndexParamVideoAccessUnitDelimiter:
      return "OMX_ALG_IndexParamVideoAccessUnitDelimiter";
    case OMX_ALG_IndexParamVideoBufferingPeriodSEI:
      return "OMX_ALG_IndexParamVideoBufferingPeriodSEI";
    case OMX_ALG_IndexParamVideoPictureTimingSEI:
      return "OMX_ALG_IndexParamVideoPictureTimingSEI";
    case OMX_ALG_IndexParamVideoRecoveryPointSEI:
      return "OMX_ALG_IndexParamVideoRecoveryPointSEI";
    case OMX_ALG_IndexParamVideoMasteringDisplayColourVolumeSEI:
      return "OMX_ALG_IndexParamVideoMasteringDisplayColourVolumeSEI";
    case OMX_ALG_IndexParamVideoContentLightLevelSEI:
      return "OMX_ALG_IndexParamVideoContentLightLevelSEI";
    case OMX_ALG_IndexConfigVideoRegionOfInterestByValue:
      return "OMX_ALG_IndexConfigVideoRegionOfInterestByValue";
    case OMX_ALG_IndexConfigVideoColorPrimaries:
      return "OMX_ALG_IndexConfigVideoColorPrimaries";
    case OMX_ALG_IndexMaxEnum:
      return "OMX_ALG_IndexMaxEnum";
  }
#endif

#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  /* Not part of the enum in OMX_IndexAlg.h */
  if (index == OMX_ALG_IndexParamVideoInterlaceFormatSupported)
    return "OMX_ALG_IndexParamVideoInterlaceFormatSupported";
#endif

  return NULL;
}
#endif /* GST_DISABLE_GST_DEBUG */

static void
log_omx_api_trace_call (GstOMXComponent * comp, const gchar * function,
    OMX_INDEXTYPE index, GstDebugLevel level)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstStructure *s;
  const gchar *index_name;

  /* Don't bother creating useless structs if not needed */
  if (gst_debug_category_get_threshold (OMX_API_TRACE) < level)
    return;

  index_name = omx_index_type_to_str (index);
  if (!index_name) {
    GST_CAT_WARNING_OBJECT (OMX_API_TRACE, comp->parent,
        "unknown call of %s with index 0x%08x", function, index);
    return;
  }

  s = gst_structure_new (function, "index", G_TYPE_STRING, index_name, NULL);
  GST_CAT_LEVEL_LOG (OMX_API_TRACE, level, comp->parent, "%" GST_PTR_FORMAT, s);
  gst_structure_free (s);
#endif /* GST_DISABLE_GST_DEBUG */
}

/* comp->lock must be unlocked while calling this */
OMX_ERRORTYPE
gst_omx_component_get_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (param != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Getting %s parameter at index 0x%08x",
      comp->name, index);
  log_omx_api_trace_call (comp, "GetParameter", index, GST_LEVEL_LOG);
  err = OMX_GetParameter (comp->handle, index, param);
  DEBUG_IF_OK (comp->parent, err, "Got %s parameter at index 0x%08x: %s "
      "(0x%08x)", comp->name, index, gst_omx_error_to_string (err), err);

  return err;
}

/* comp->lock must be unlocked while calling this */
OMX_ERRORTYPE
gst_omx_component_set_parameter (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (param != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Setting %s parameter at index 0x%08x",
      comp->name, index);

  log_omx_api_trace_call (comp, "SetParameter", index, GST_LEVEL_DEBUG);
  err = OMX_SetParameter (comp->handle, index, param);
  DEBUG_IF_OK (comp->parent, err, "Set %s parameter at index 0x%08x: %s "
      "(0x%08x)", comp->name, index, gst_omx_error_to_string (err), err);

  return err;
}

/* comp->lock must be unlocked while calling this */
OMX_ERRORTYPE
gst_omx_component_get_config (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer config)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (config != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Getting %s configuration at index 0x%08x",
      comp->name, index);
  log_omx_api_trace_call (comp, "GetConfig", index, GST_LEVEL_LOG);
  err = OMX_GetConfig (comp->handle, index, config);
  DEBUG_IF_OK (comp->parent, err, "Got %s parameter at index 0x%08x: %s "
      "(0x%08x)", comp->name, index, gst_omx_error_to_string (err), err);

  return err;
}

/* comp->lock must be unlocked while calling this */
OMX_ERRORTYPE
gst_omx_component_set_config (GstOMXComponent * comp, OMX_INDEXTYPE index,
    gpointer config)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (comp != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (config != NULL, OMX_ErrorUndefined);

  GST_DEBUG_OBJECT (comp->parent, "Setting %s configuration at index 0x%08x",
      comp->name, index);
  log_omx_api_trace_call (comp, "SetConfig", index, GST_LEVEL_DEBUG);
  err = OMX_SetConfig (comp->handle, index, config);
  DEBUG_IF_OK (comp->parent, err, "Set %s parameter at index 0x%08x: %s "
      "(0x%08x)", comp->name, index, gst_omx_error_to_string (err), err);

  return err;
}

OMX_ERRORTYPE
gst_omx_setup_tunnel (GstOMXPort * port1, GstOMXPort * port2)
{
  GstOMXComponent *comp1;
  GstOMXComponent *comp2;
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port1 != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (port1->port_def.eDir == OMX_DirOutput,
      OMX_ErrorUndefined);
  comp1 = port1->comp;

  g_return_val_if_fail (port2 != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (port2->port_def.eDir == OMX_DirInput,
      OMX_ErrorUndefined);
  comp2 = port2->comp;

  g_return_val_if_fail (comp1->core == comp2->core, OMX_ErrorUndefined);

  g_mutex_lock (&comp1->lock);
  g_mutex_lock (&comp2->lock);
  GST_DEBUG_OBJECT (comp1->parent,
      "Setup tunnel between %s port %u and %s port %u",
      comp1->name, port1->index, comp2->name, port2->index);

  err = comp1->core->setup_tunnel (comp1->handle, port1->index, comp2->handle,
      port2->index);

  if (err == OMX_ErrorNone) {
    port1->tunneled = TRUE;
    port2->tunneled = TRUE;
  }

  DEBUG_IF_OK (comp1->parent, err,
      "Setup tunnel between %s port %u and %s port %u: %s (0x%08x)",
      comp1->name, port1->index,
      comp2->name, port2->index, gst_omx_error_to_string (err), err);

  g_mutex_unlock (&comp2->lock);
  g_mutex_unlock (&comp1->lock);

  return err;
}

OMX_ERRORTYPE
gst_omx_close_tunnel (GstOMXPort * port1, GstOMXPort * port2)
{
  GstOMXComponent *comp1;
  GstOMXComponent *comp2;
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port1 != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (port1->port_def.eDir == OMX_DirOutput,
      OMX_ErrorUndefined);
  comp1 = port1->comp;

  g_return_val_if_fail (port2 != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (port2->port_def.eDir == OMX_DirInput,
      OMX_ErrorUndefined);
  comp2 = port2->comp;

  g_return_val_if_fail (comp1->core == comp2->core, OMX_ErrorUndefined);
  g_return_val_if_fail (port1->tunneled && port2->tunneled, OMX_ErrorUndefined);

  g_mutex_lock (&comp1->lock);
  g_mutex_lock (&comp2->lock);
  GST_DEBUG_OBJECT (comp1->parent,
      "Closing tunnel between %s port %u and %s port %u",
      comp1->name, port1->index, comp2->name, port2->index);

  err = comp1->core->setup_tunnel (comp1->handle, port1->index, 0, 0);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp1->parent,
        "Failed to close tunnel on output side %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }
  err = comp2->core->setup_tunnel (0, 0, comp2->handle, port2->index);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp2->parent,
        "Failed to close tunnel on input side %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  port1->tunneled = FALSE;
  port2->tunneled = FALSE;

  GST_DEBUG_OBJECT (comp1->parent,
      "Closed tunnel between %s port %u and %s port %u",
      comp1->name, port1->index, comp2->name, port2->index);

  g_mutex_unlock (&comp2->lock);
  g_mutex_unlock (&comp1->lock);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_get_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorBadParameter);

  comp = port->comp;

  GST_OMX_INIT_STRUCT (port_def);
  port_def->nPortIndex = port->index;

  err = gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      port_def);

  return err;
}

OMX_ERRORTYPE
gst_omx_port_update_port_definition (GstOMXPort * port,
    OMX_PARAM_PORTDEFINITIONTYPE * port_def)
{
  OMX_ERRORTYPE err_get, err_set = OMX_ErrorNone;
  GstOMXComponent *comp;

  g_return_val_if_fail (port != NULL, FALSE);

  comp = port->comp;

  if (port_def)
    err_set =
        gst_omx_component_set_parameter (comp, OMX_IndexParamPortDefinition,
        port_def);
  err_get = gst_omx_component_get_parameter (comp, OMX_IndexParamPortDefinition,
      &port->port_def);

  DEBUG_IF_OK (comp->parent, err_set,
      "Updated %s port %u definition: %s (0x%08x)", comp->name, port->index,
      gst_omx_error_to_string (err_set), err_set);

  if (err_set != OMX_ErrorNone)
    return err_set;
  else
    return err_get;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
GstOMXAcquireBufferReturn
gst_omx_port_acquire_buffer (GstOMXPort * port, GstOMXBuffer ** buf,
    GstOMXWait wait)
{
  GstOMXAcquireBufferReturn ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXComponent *comp;
  OMX_ERRORTYPE err;
  GstOMXBuffer *_buf = NULL;
  gint64 timeout = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (port != NULL, GST_OMX_ACQUIRE_BUFFER_ERROR);
  g_return_val_if_fail (!port->tunneled, GST_OMX_ACQUIRE_BUFFER_ERROR);
  g_return_val_if_fail (buf != NULL, GST_OMX_ACQUIRE_BUFFER_ERROR);

  *buf = NULL;

  comp = port->comp;

  g_mutex_lock (&comp->lock);
  GST_DEBUG_OBJECT (comp->parent, "Acquiring %s buffer from port %u",
      comp->name, port->index);

retry:
  gst_omx_component_handle_messages (comp);

  /* If we are in the case where we waited for a buffer after EOS,
   * make sure we don't do that again */
  if (timeout != -1)
    timeout = -2;

  /* Check if the component is in an error state */
  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s is in error state: %s",
        comp->name, gst_omx_error_to_string (err));
    ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
    goto done;
  }

  /* Check if the port is flushing */
  if (port->flushing) {
    GST_DEBUG_OBJECT (comp->parent, "Component %s port %d is flushing",
        comp->name, port->index);
    ret = GST_OMX_ACQUIRE_BUFFER_FLUSHING;
    goto done;
  }

  /* If this is an input port and at least one of the output ports
   * needs to be reconfigured, we wait until all output ports are
   * reconfigured. Afterwards this port is reconfigured if required
   * or buffers are returned to be filled as usual.
   */
  if (port->port_def.eDir == OMX_DirInput) {
    if (comp->pending_reconfigure_outports) {
      gst_omx_component_handle_messages (comp);
      while (comp->pending_reconfigure_outports &&
          (err = comp->last_error) == OMX_ErrorNone && !port->flushing) {
        GST_DEBUG_OBJECT (comp->parent,
            "Waiting for %s output ports to reconfigure", comp->name);
        gst_omx_component_wait_message (comp, GST_CLOCK_TIME_NONE);
        gst_omx_component_handle_messages (comp);
      }
      goto retry;
    }

    /* Only if this port needs to be reconfigured too notify
     * the caller about it */
    if (port->settings_cookie != port->configured_settings_cookie) {
      GST_DEBUG_OBJECT (comp->parent,
          "Component %s port %d needs reconfiguring", comp->name, port->index);
      ret = GST_OMX_ACQUIRE_BUFFER_RECONFIGURE;
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
    if (!g_queue_is_empty (&port->pending_buffers)) {
      GST_DEBUG_OBJECT (comp->parent,
          "%s output port %u needs reconfiguration but has buffers pending",
          comp->name, port->index);
      _buf = g_queue_pop_head (&port->pending_buffers);

      ret = GST_OMX_ACQUIRE_BUFFER_OK;
      goto done;
    }

    GST_DEBUG_OBJECT (comp->parent, "Component %s port %d needs reconfiguring",
        comp->name, port->index);
    ret = GST_OMX_ACQUIRE_BUFFER_RECONFIGURE;
    goto done;
  }

  if (port->port_def.eDir == OMX_DirOutput && port->eos) {
    if (!g_queue_is_empty (&port->pending_buffers)) {
      GST_DEBUG_OBJECT (comp->parent, "%s output port %u is EOS but has "
          "%d buffers pending", comp->name, port->index,
          g_queue_get_length (&port->pending_buffers));
      _buf = g_queue_pop_head (&port->pending_buffers);

      ret = GST_OMX_ACQUIRE_BUFFER_OK;
      goto done;
    }

    if (comp->hacks & GST_OMX_HACK_SIGNALS_PREMATURE_EOS && timeout != -2) {
      timeout = 33 * GST_MSECOND;

      GST_DEBUG_OBJECT (comp->parent, "%s output port %u is EOS but waiting "
          "in case it spits out more buffers", comp->name, port->index);
    } else {
      GST_DEBUG_OBJECT (comp->parent, "Component %s port %d signalled EOS",
          comp->name, port->index);
      ret = GST_OMX_ACQUIRE_BUFFER_EOS;
      port->eos = FALSE;
      goto done;
    }
  }

  /*
   * At this point we have no error or flushing/eos port
   * and a properly configured port.
   *
   */

  /* If the queue is empty we wait until a buffer
   * arrives, an error happens, the port is flushing
   * or the port needs to be reconfigured.
   */
  if (g_queue_is_empty (&port->pending_buffers)) {
    GST_DEBUG_OBJECT (comp->parent, "Queue of %s port %u is empty",
        comp->name, port->index);

    if (wait == GST_OMX_WAIT) {
      gst_omx_component_wait_message (comp,
          timeout == -2 ? GST_CLOCK_TIME_NONE : timeout);

      /* And now check everything again and maybe get a buffer */
      goto retry;
    } else {
      ret = GST_OMX_ACQUIRE_BUFFER_NO_AVAILABLE;
      goto done;
    }
  }

  GST_DEBUG_OBJECT (comp->parent, "%s port %u has pending buffers",
      comp->name, port->index);
  _buf = g_queue_pop_head (&port->pending_buffers);
  ret = GST_OMX_ACQUIRE_BUFFER_OK;

done:
  g_mutex_unlock (&comp->lock);

  if (_buf) {
    g_assert (_buf == _buf->omx_buf->pAppPrivate);
    *buf = _buf;
  }

  GST_DEBUG_OBJECT (comp->parent, "Acquired buffer %p (%p) from %s port %u: %d",
      _buf, (_buf ? _buf->omx_buf->pBuffer : NULL), comp->name, port->index,
      ret);

  return ret;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_release_buffer (GstOMXPort * port, GstOMXBuffer * buf)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (!port->tunneled, OMX_ErrorUndefined);
  g_return_val_if_fail (buf != NULL, OMX_ErrorUndefined);
  g_return_val_if_fail (buf->port == port, OMX_ErrorUndefined);

  comp = port->comp;

  g_mutex_lock (&comp->lock);

  GST_DEBUG_OBJECT (comp->parent, "Releasing buffer %p (%p) to %s port %u",
      buf, buf->omx_buf->pBuffer, comp->name, port->index);

  gst_omx_component_handle_messages (comp);

  if (port->port_def.eDir == OMX_DirOutput) {
    /* Reset all flags, some implementations don't
     * reset them themselves and the flags are not
     * valid anymore after the buffer was consumed
     */
    gst_omx_buffer_reset (buf);
  }

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s is in error state: %s "
        "(0x%08x)", comp->name, gst_omx_error_to_string (err), err);
    g_queue_push_tail (&port->pending_buffers, buf);
    gst_omx_component_send_message (comp, NULL);
    goto done;
  }

  if (port->flushing || port->disabled_pending || !port->port_def.bEnabled) {
    GST_DEBUG_OBJECT (comp->parent,
        "%s port %u is flushing or disabled, not releasing " "buffer",
        comp->name, port->index);
    g_queue_push_tail (&port->pending_buffers, buf);
    gst_omx_component_send_message (comp, NULL);
    goto done;
  }

  g_assert (buf == buf->omx_buf->pAppPrivate);

  /* FIXME: What if the settings cookies don't match? */

  buf->used = TRUE;

  if (port->port_def.eDir == OMX_DirInput) {
    log_omx_api_trace_buffer (comp, "EmptyThisBuffer", buf);
    err = OMX_EmptyThisBuffer (comp->handle, buf->omx_buf);
  } else {
    log_omx_api_trace_buffer (comp, "FillThisBuffer", buf);
    err = OMX_FillThisBuffer (comp->handle, buf->omx_buf);
  }
  DEBUG_IF_OK (comp->parent, err, "Released buffer %p to %s port %u: %s "
      "(0x%08x)", buf, comp->name, port->index, gst_omx_error_to_string (err),
      err);

done:
  gst_omx_component_handle_messages (comp);
  g_mutex_unlock (&comp->lock);

  return err;
}

/* NOTE: Must be called while holding comp->lock */
static gboolean
should_wait_until_flushed (GstOMXPort * port)
{
  if (!port->flushed)
    /* Flush command hasn't been completed yet by OMX */
    return TRUE;

  if (port->buffers) {
    guint i;

    /* Wait for all the buffers used by OMX to be released */
    for (i = 0; i < port->buffers->len; i++) {
      GstOMXBuffer *buf = g_ptr_array_index (port->buffers, i);

      if (buf->used)
        return TRUE;
    }
  }

  return FALSE;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_set_flushing (GstOMXPort * port, GstClockTime timeout,
    gboolean flush)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;

  g_mutex_lock (&comp->lock);

  GST_DEBUG_OBJECT (comp->parent, "Setting %s port %d to %sflushing",
      comp->name, port->index, (flush ? "" : "not "));

  gst_omx_component_handle_messages (comp);

  if (flush == port->flushing) {
    GST_DEBUG_OBJECT (comp->parent, "%s port %u was %sflushing already",
        comp->name, port->index, (flush ? "" : "not "));
    goto done;
  }

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s is in error state: %s "
        "(0x%08x)", comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  port->flushing = flush;
  if (flush) {
    gboolean signalled;
    OMX_ERRORTYPE last_error;

    gst_omx_component_send_message (comp, NULL);

    /* Now flush the port */
    port->flushed = FALSE;

    err =
        gst_omx_component_send_command (comp, OMX_CommandFlush, port->index,
        NULL);

    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Error sending flush command to %s port %u: %s (0x%08x)", comp->name,
          port->index, gst_omx_error_to_string (err), err);
      goto done;
    }

    if ((err = comp->last_error) != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Component %s is in error state: %s (0x%08x)", comp->name,
          gst_omx_error_to_string (err), err);
      goto done;
    }

    if (port->flushing != flush) {
      GST_ERROR_OBJECT (comp->parent, "%s: another flush happened in the "
          " meantime", comp->name);
      goto done;
    }

    if (timeout == 0) {
      if (should_wait_until_flushed (port))
        err = OMX_ErrorTimeout;
      goto done;
    }

    /* Retry until timeout or until an error happend or
     * until all buffers were released by the component and
     * the flush command completed */
    signalled = TRUE;
    last_error = OMX_ErrorNone;
    gst_omx_component_handle_messages (comp);
    while (should_wait_until_flushed (port)) {
      signalled = gst_omx_component_wait_message (comp, timeout);
      if (signalled)
        gst_omx_component_handle_messages (comp);

      last_error = comp->last_error;

      if (!signalled || last_error != OMX_ErrorNone)
        /* Something gone wrong or we timed out */
        break;
    }
    port->flushed = FALSE;

    GST_DEBUG_OBJECT (comp->parent, "%s port %d flushed", comp->name,
        port->index);
    if (last_error != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "Got error while flushing %s port %u: %s (0x%08x)", comp->name,
          port->index, gst_omx_error_to_string (last_error), last_error);
      err = last_error;
      goto done;
    } else if (!signalled) {
      GST_ERROR_OBJECT (comp->parent, "Timeout while flushing %s port %u",
          comp->name, port->index);
      err = OMX_ErrorTimeout;
      goto done;
    }
  }

  /* Reset EOS flag */
  port->eos = FALSE;

done:
  gst_omx_port_update_port_definition (port, NULL);

  DEBUG_IF_OK (comp->parent, err, "Set %s port %u to %sflushing: %s (0x%08x)",
      comp->name, port->index, (flush ? "" : "not "),
      gst_omx_error_to_string (err), err);
  gst_omx_component_handle_messages (comp);
  g_mutex_unlock (&comp->lock);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
gboolean
gst_omx_port_is_flushing (GstOMXPort * port)
{
  GstOMXComponent *comp;
  gboolean flushing;

  g_return_val_if_fail (port != NULL, FALSE);

  comp = port->comp;

  g_mutex_lock (&comp->lock);
  gst_omx_component_handle_messages (port->comp);
  flushing = port->flushing;
  g_mutex_unlock (&comp->lock);

  GST_DEBUG_OBJECT (comp->parent, "%s port %u is flushing: %d", comp->name,
      port->index, flushing);

  return flushing;
}

static OMX_ERRORTYPE gst_omx_port_deallocate_buffers_unlocked (GstOMXPort *
    port);

/* NOTE: Must be called while holding comp->lock, uses comp->messages_lock */
static OMX_ERRORTYPE
gst_omx_port_allocate_buffers_unlocked (GstOMXPort * port,
    const GList * buffers, const GList * images, guint n)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i;
  const GList *l;

  g_assert (!port->buffers || port->buffers->len == 0);

  g_return_val_if_fail (!port->tunneled, OMX_ErrorBadParameter);

  comp = port->comp;

  gst_omx_component_handle_messages (port->comp);
  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  /* Update the port definition to check if we need more
   * buffers after the port configuration was done and to
   * update the buffer size
   */
  gst_omx_port_update_port_definition (port, NULL);

  g_return_val_if_fail (n != -1 || (!buffers
          && !images), OMX_ErrorBadParameter);

  if (n == -1)
    n = port->port_def.nBufferCountActual;

  g_return_val_if_fail (n == port->port_def.nBufferCountActual,
      OMX_ErrorBadParameter);

  GST_INFO_OBJECT (comp->parent,
      "Allocating %d buffers of size %" G_GSIZE_FORMAT " for %s port %u", n,
      (size_t) port->port_def.nBufferSize, comp->name, (guint) port->index);

  if (!port->buffers)
    port->buffers = g_ptr_array_sized_new (n);

  l = (buffers ? buffers : images);
  for (i = 0; i < n; i++) {
    GstOMXBuffer *buf;

    buf = g_slice_new0 (GstOMXBuffer);
    buf->port = port;
    buf->used = FALSE;
    buf->settings_cookie = port->settings_cookie;
    g_ptr_array_add (port->buffers, buf);

    if (buffers) {
      err =
          OMX_UseBuffer (comp->handle, &buf->omx_buf, port->index, buf,
          port->port_def.nBufferSize, l->data);
      buf->eglimage = FALSE;
    } else if (images) {
      err =
          OMX_UseEGLImage (comp->handle, &buf->omx_buf, port->index, buf,
          l->data);
      buf->eglimage = TRUE;
    } else {
      err =
          OMX_AllocateBuffer (comp->handle, &buf->omx_buf, port->index, buf,
          port->port_def.nBufferSize);
      buf->eglimage = FALSE;
    }

    /* Let the caller decide to print an error when OMX_UseBuffer or
     * OMX_UseEGLImage fail. Indeed it can be part of a trial path. So
     * it is not necessary to warn the user if the fallback path succeeds.
     */
    if (err != OMX_ErrorNone) {
      GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, (buffers
              || images) ? GST_LEVEL_INFO : GST_LEVEL_ERROR, comp->parent,
          "Failed to allocate buffer for %s port %u: %s (0x%08x)", comp->name,
          port->index, gst_omx_error_to_string (err), err);
      gst_omx_port_deallocate_buffers_unlocked (port);
      goto done;
    }

    GST_DEBUG_OBJECT (comp->parent, "%s: allocated buffer %p (%p)",
        comp->name, buf, buf->omx_buf->pBuffer);

    g_assert (buf->omx_buf->pAppPrivate == buf);

    /* In the beginning all buffers are not owned by the component */
    g_queue_push_tail (&port->pending_buffers, buf);
    if (buffers || images)
      l = l->next;
  }

  gst_omx_component_handle_messages (comp);

done:
  gst_omx_port_update_port_definition (port, NULL);

  INFO_IF_OK (comp->parent, err, "Allocated buffers for %s port %u: %s "
      "(0x%08x)", comp->name, port->index, gst_omx_error_to_string (err), err);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_allocate_buffers (GstOMXPort * port)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_allocate_buffers_unlocked (port, NULL, NULL, -1);
  port->allocation = GST_OMX_BUFFER_ALLOCATION_ALLOCATE_BUFFER;
  g_mutex_unlock (&port->comp->lock);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_use_buffers (GstOMXPort * port, const GList * buffers)
{
  OMX_ERRORTYPE err;
  guint n;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  n = g_list_length ((GList *) buffers);
  err = gst_omx_port_allocate_buffers_unlocked (port, buffers, NULL, n);
  port->allocation = GST_OMX_BUFFER_ALLOCATION_USE_BUFFER;
  g_mutex_unlock (&port->comp->lock);

  return err;
}

gboolean
gst_omx_is_dynamic_allocation_supported (void)
{
  /* The Zynqultrascaleplus stack implements OMX 1.1.0 but supports the dynamic
   * allocation mode from 1.2.0 as an extension. */
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  return TRUE;
#endif

#if OMX_VERSION_MINOR == 2
  return TRUE;
#else
  return FALSE;
#endif
}

/* OMX 1.2.0 introduced a dynamic allocation mode where only buffer headers are
 * being allocated during component's initialization. The actual buffers are
 * allocated upstream and passed to OMX by setting the pBuffer dynamically
 * for each input buffer.
 *
 * This function takes care of allocating the buffer headers. Element should
 * then use one of the gst_omx_buffer_map_*() method to update buffer's pBuffer
 * pointers for each incoming buffer.
 *
 * NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_use_dynamic_buffers (GstOMXPort * port)
{
  OMX_ERRORTYPE err;
  GList *buffers = NULL;
  guint i, n;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  n = port->port_def.nBufferCountActual;
  for (i = 0; i < port->port_def.nBufferCountActual; i++)
    /* Pass NULL to UseBuffer() as the buffer is dynamic and so its payload
     * will be set each time before being passed to OMX. */
    buffers = g_list_prepend (buffers, GUINT_TO_POINTER (NULL));

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_allocate_buffers_unlocked (port, buffers, NULL, n);
  port->allocation = GST_OMX_BUFFER_ALLOCATION_USE_BUFFER_DYNAMIC;
  g_mutex_unlock (&port->comp->lock);

  g_list_free (buffers);

  return err;
}

/* gst_omx_buffer_map_* methods are used in dynamic buffer mode to map
 * a frame/memory/buffer and update @buffer so its pBuffer points to the
 * mapped data. It also ensures that the input will stay alive until
 * gst_omx_buffer_unmap() is called.
 * This is used in OMX 1.2.0 dynamic allocation mode so an OMX component can
 * safely process @buffer's content without having to copy it.
 * The input will be automatically unmapped when @buffer is released by OMX.
 */
gboolean
gst_omx_buffer_map_frame (GstOMXBuffer * buffer, GstBuffer * input,
    GstVideoInfo * info)
{
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (!buffer->input_frame_mapped, FALSE);
  g_return_val_if_fail (!buffer->input_mem, FALSE);
  g_return_val_if_fail (!buffer->input_buffer, FALSE);
  g_return_val_if_fail (!buffer->input_buffer_mapped, FALSE);

  if (!gst_video_frame_map (&buffer->input_frame, info, input, GST_MAP_READ))
    return FALSE;

  buffer->input_frame_mapped = TRUE;
  buffer->omx_buf->pBuffer =
      GST_VIDEO_FRAME_PLANE_DATA (&buffer->input_frame, 0);
  buffer->omx_buf->nAllocLen = gst_buffer_get_size (input);
  buffer->omx_buf->nFilledLen = buffer->omx_buf->nAllocLen;

  return TRUE;
}

gboolean
gst_omx_buffer_map_memory (GstOMXBuffer * buffer, GstMemory * mem)
{
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (!buffer->input_frame_mapped, FALSE);
  g_return_val_if_fail (!buffer->input_mem, FALSE);
  g_return_val_if_fail (!buffer->input_buffer, FALSE);
  g_return_val_if_fail (!buffer->input_buffer_mapped, FALSE);

  if (!gst_memory_map (mem, &buffer->map, GST_MAP_READ))
    return FALSE;

  buffer->input_mem = gst_memory_ref (mem);
  buffer->omx_buf->pBuffer = buffer->map.data;
  buffer->omx_buf->nAllocLen = buffer->map.size;
  buffer->omx_buf->nFilledLen = buffer->omx_buf->nAllocLen;

  return TRUE;
}

gboolean
gst_omx_buffer_import_fd (GstOMXBuffer * buffer, GstBuffer * input)
{
  gint fd;
  GstMemory *mem;

  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (input != NULL, FALSE);
  g_return_val_if_fail (!buffer->input_frame_mapped, FALSE);
  g_return_val_if_fail (!buffer->input_mem, FALSE);
  g_return_val_if_fail (!buffer->input_buffer, FALSE);
  g_return_val_if_fail (!buffer->input_buffer_mapped, FALSE);

  mem = gst_buffer_peek_memory (input, 0);
  g_return_val_if_fail (gst_is_dmabuf_memory (mem), FALSE);

  fd = gst_dmabuf_memory_get_fd (mem);

  buffer->input_buffer = gst_buffer_ref (input);
  buffer->omx_buf->pBuffer = GUINT_TO_POINTER (fd);
  buffer->omx_buf->nAllocLen = gst_memory_get_sizes (mem, NULL, NULL);
  buffer->omx_buf->nFilledLen = buffer->omx_buf->nAllocLen;

  return TRUE;
}

gboolean
gst_omx_buffer_map_buffer (GstOMXBuffer * buffer, GstBuffer * input)
{
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (input != NULL, FALSE);
  g_return_val_if_fail (!buffer->input_frame_mapped, FALSE);
  g_return_val_if_fail (!buffer->input_mem, FALSE);
  g_return_val_if_fail (!buffer->input_buffer, FALSE);
  g_return_val_if_fail (!buffer->input_buffer_mapped, FALSE);

  if (!gst_buffer_map (input, &buffer->map, GST_MAP_READ))
    return FALSE;

  buffer->input_buffer_mapped = TRUE;
  buffer->input_buffer = gst_buffer_ref (input);
  buffer->omx_buf->pBuffer = buffer->map.data;
  buffer->omx_buf->nAllocLen = buffer->map.size;
  buffer->omx_buf->nFilledLen = buffer->omx_buf->nAllocLen;

  return TRUE;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_use_eglimages (GstOMXPort * port, const GList * images)
{
  OMX_ERRORTYPE err;
  guint n;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  n = g_list_length ((GList *) images);
  err = gst_omx_port_allocate_buffers_unlocked (port, NULL, images, n);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

/* NOTE: Must be called while holding comp->lock, uses comp->messages_lock */
static OMX_ERRORTYPE
gst_omx_port_deallocate_buffers_unlocked (GstOMXPort * port)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gint i, n;

  g_return_val_if_fail (!port->tunneled, OMX_ErrorBadParameter);

  comp = port->comp;

  GST_INFO_OBJECT (comp->parent, "Deallocating buffers of %s port %u",
      comp->name, port->index);

  gst_omx_component_handle_messages (port->comp);

  if (!port->buffers) {
    GST_DEBUG_OBJECT (comp->parent, "No buffers allocated for %s port %u",
        comp->name, port->index);
    goto done;
  }

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
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

    if (buf->used) {
      GST_ERROR_OBJECT (comp->parent, "Trying to free used buffer %p of %s "
          "port %u", buf, comp->name, port->index);
    }

    /* omx_buf can be NULL if allocation failed earlier
     * and we're just shutting down
     *
     * errors do not cause exiting this loop because we want
     * to deallocate as much as possible.
     */
    if (buf->omx_buf) {
      g_assert (buf == buf->omx_buf->pAppPrivate);
      buf->omx_buf->pAppPrivate = NULL;
      GST_DEBUG_OBJECT (comp->parent, "%s: deallocating buffer %p (%p)",
          comp->name, buf, buf->omx_buf->pBuffer);

      tmp = OMX_FreeBuffer (comp->handle, port->index, buf->omx_buf);

      if (tmp != OMX_ErrorNone) {
        GST_ERROR_OBJECT (comp->parent,
            "Failed to deallocate buffer %d of %s port %u: %s (0x%08x)", i,
            comp->name, port->index, gst_omx_error_to_string (tmp), tmp);
        if (err == OMX_ErrorNone)
          err = tmp;
      }
    }
    g_slice_free (GstOMXBuffer, buf);
  }
  g_queue_clear (&port->pending_buffers);
  g_ptr_array_unref (port->buffers);
  port->buffers = NULL;

  gst_omx_component_handle_messages (comp);

done:
  gst_omx_port_update_port_definition (port, NULL);

  DEBUG_IF_OK (comp->parent, err, "Deallocated buffers of %s port %u: %s "
      "(0x%08x)", comp->name, port->index, gst_omx_error_to_string (err), err);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_deallocate_buffers (GstOMXPort * port)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_deallocate_buffers_unlocked (port);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

/* NOTE: Must be called while holding comp->lock, uses comp->messages_lock */
static OMX_ERRORTYPE
gst_omx_port_set_enabled_unlocked (GstOMXPort * port, gboolean enabled)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  comp = port->comp;

  gst_omx_component_handle_messages (comp);

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  if (port->enabled_pending || port->disabled_pending) {
    GST_ERROR_OBJECT (comp->parent, "%s port %d enabled/disabled pending "
        "already", comp->name, port->index);
#if OMX_VERSION_MINOR == 2
    err = OMX_ErrorBadParameter;
#else
    err = OMX_ErrorInvalidState;
#endif
    goto done;
  }

  GST_INFO_OBJECT (comp->parent, "Setting %s port %u to %s", comp->name,
      port->index, (enabled ? "enabled" : "disabled"));

  /* Check if the port is already enabled/disabled first */
  gst_omx_port_update_port_definition (port, NULL);
  if (! !port->port_def.bEnabled == ! !enabled)
    goto done;

  if (enabled)
    port->enabled_pending = TRUE;
  else
    port->disabled_pending = TRUE;

  if (enabled)
    err =
        gst_omx_component_send_command (comp, OMX_CommandPortEnable,
        port->index, NULL);
  else
    err =
        gst_omx_component_send_command (comp, OMX_CommandPortDisable,
        port->index, NULL);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Failed to send enable/disable command to %s port %u: %s (0x%08x)",
        comp->name, port->index, gst_omx_error_to_string (err), err);
    goto done;
  }

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

done:
  gst_omx_component_handle_messages (comp);

  gst_omx_port_update_port_definition (port, NULL);

  INFO_IF_OK (comp->parent, err, "Set %s port %u to %s%s: %s (0x%08x)",
      comp->name, port->index, (err == OMX_ErrorNone ? "" : "not "),
      (enabled ? "enabled" : "disabled"), gst_omx_error_to_string (err), err);

  return err;
}

static OMX_ERRORTYPE
gst_omx_port_wait_buffers_released_unlocked (GstOMXPort * port,
    GstClockTime timeout)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  OMX_ERRORTYPE last_error;
  gboolean signalled;

  comp = port->comp;

  gst_omx_component_handle_messages (comp);

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  GST_INFO_OBJECT (comp->parent, "Waiting for %s port %u to release all "
      "buffers", comp->name, port->index);

  if (timeout == 0) {
    if (!port->flushed || (port->buffers
            && port->buffers->len >
            g_queue_get_length (&port->pending_buffers)))
      err = OMX_ErrorTimeout;
    goto done;
  }

  /* Wait until all buffers are released by the port */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  gst_omx_component_handle_messages (comp);
  while (signalled && last_error == OMX_ErrorNone && (port->buffers
          && port->buffers->len >
          g_queue_get_length (&port->pending_buffers))) {
    signalled = gst_omx_component_wait_message (comp, timeout);
    if (signalled)
      gst_omx_component_handle_messages (comp);
    last_error = comp->last_error;
  }

  if (last_error != OMX_ErrorNone) {
    err = last_error;
    GST_ERROR_OBJECT (comp->parent,
        "Got error while waiting for %s port %u to release all buffers: %s "
        "(0x%08x)", comp->name, port->index, gst_omx_error_to_string (err),
        err);
    goto done;
  } else if (!signalled) {
    GST_ERROR_OBJECT (comp->parent, "Timeout waiting for %s port %u to "
        "release all buffers", comp->name, port->index);
    err = OMX_ErrorTimeout;
    goto done;
  }

done:
  gst_omx_component_handle_messages (comp);

  gst_omx_port_update_port_definition (port, NULL);

  DEBUG_IF_OK (comp->parent, err,
      "Waited for %s port %u to release all buffers: %s (0x%08x)", comp->name,
      port->index, gst_omx_error_to_string (err), err);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_wait_buffers_released (GstOMXPort * port, GstClockTime timeout)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_wait_buffers_released_unlocked (port, timeout);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

void
gst_omx_port_requeue_buffer (GstOMXPort * port, GstOMXBuffer * buf)
{
  g_mutex_lock (&port->comp->lock);
  g_queue_push_tail (&port->pending_buffers, buf);
  g_mutex_unlock (&port->comp->lock);

  /* awake gst_omx_port_acquire_buffer() */
  gst_omx_component_send_message (port->comp, NULL);
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_set_enabled (GstOMXPort * port, gboolean enabled)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_set_enabled_unlocked (port, enabled);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

static OMX_ERRORTYPE
gst_omx_port_populate_unlocked (GstOMXPort * port)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  GstOMXBuffer *buf;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;

  GST_DEBUG_OBJECT (comp->parent, "Populating %s port %d", comp->name,
      port->index);

  gst_omx_component_handle_messages (comp);

  if (port->flushing || port->disabled_pending || !port->port_def.bEnabled) {
    GST_DEBUG_OBJECT (comp->parent, "%s port %u is flushing or disabled",
        comp->name, port->index);
    err = OMX_ErrorIncorrectStateOperation;
    goto done;
  }

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s is in error state: %s"
        "(0x%08x)", comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  if (port->port_def.eDir == OMX_DirOutput && port->buffers && !port->tunneled) {
    /* Enqueue all buffers for the component to fill */
    while ((buf = g_queue_pop_head (&port->pending_buffers))) {
      g_assert (!buf->used);

      /* Reset all flags, some implementations don't
       * reset them themselves and the flags are not
       * valid anymore after the buffer was consumed.
       * Also reset nFilledLen as FillThisBuffer() expects an empty buffer.
       */
      gst_omx_buffer_reset (buf);

      log_omx_api_trace_buffer (comp, "FillThisBuffer", buf);
      err = OMX_FillThisBuffer (comp->handle, buf->omx_buf);

      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (comp->parent,
            "Failed to pass buffer %p (%p) to %s port %u: %s (0x%08x)", buf,
            buf->omx_buf->pBuffer, comp->name, port->index,
            gst_omx_error_to_string (err), err);
        goto done;
      }
      GST_DEBUG_OBJECT (comp->parent, "Passed buffer %p (%p) to component %s",
          buf, buf->omx_buf->pBuffer, comp->name);
    }
  }

done:
  gst_omx_port_update_port_definition (port, NULL);

  DEBUG_IF_OK (comp->parent, err, "Populated %s port %u: %s (0x%08x)",
      comp->name, port->index, gst_omx_error_to_string (err), err);
  gst_omx_component_handle_messages (comp);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_populate (GstOMXPort * port)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_populate_unlocked (port);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

/* NOTE: Must be called while holding comp->lock, uses comp->messages_lock */
static OMX_ERRORTYPE
gst_omx_port_wait_enabled_unlocked (GstOMXPort * port, GstClockTime timeout)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  gboolean signalled;
  OMX_ERRORTYPE last_error;
  gboolean enabled;

  comp = port->comp;

  /* Check the current port status */
  gst_omx_port_update_port_definition (port, NULL);

  if (port->enabled_pending)
    enabled = TRUE;
  else if (port->disabled_pending)
    enabled = FALSE;
  else
    enabled = port->port_def.bEnabled;

  gst_omx_component_handle_messages (comp);

  if ((err = comp->last_error) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "Component %s in error state: %s (0x%08x)",
        comp->name, gst_omx_error_to_string (err), err);
    goto done;
  }

  GST_INFO_OBJECT (comp->parent, "Waiting for %s port %u to be %s",
      comp->name, port->index, (enabled ? "enabled" : "disabled"));

  if (timeout == 0) {
    if (port->enabled_pending || port->disabled_pending)
      err = OMX_ErrorTimeout;
    goto done;
  }

  /* And now wait until the enable/disable command is finished */
  signalled = TRUE;
  last_error = OMX_ErrorNone;
  gst_omx_port_update_port_definition (port, NULL);
  gst_omx_component_handle_messages (comp);
  while (signalled && last_error == OMX_ErrorNone &&
      (! !port->port_def.bEnabled != ! !enabled || port->enabled_pending
          || port->disabled_pending)) {
    signalled = gst_omx_component_wait_message (comp, timeout);
    if (signalled)
      gst_omx_component_handle_messages (comp);
    last_error = comp->last_error;
    gst_omx_port_update_port_definition (port, NULL);
  }
  port->enabled_pending = FALSE;
  port->disabled_pending = FALSE;

  if (!signalled) {
    GST_ERROR_OBJECT (comp->parent,
        "Timeout waiting for %s port %u to be %s", comp->name, port->index,
        (enabled ? "enabled" : "disabled"));
    err = OMX_ErrorTimeout;
    goto done;
  } else if (last_error != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "Got error while waiting for %s port %u to be %s: %s (0x%08x)",
        comp->name, port->index, (enabled ? "enabled" : "disabled"),
        gst_omx_error_to_string (err), err);
    err = last_error;
  } else {
    if (enabled) {
      /* Reset EOS flag */
      port->eos = FALSE;
    }
  }

  gst_omx_component_handle_messages (comp);

done:
  gst_omx_port_update_port_definition (port, NULL);

  GST_INFO_OBJECT (comp->parent, "%s port %u is %s%s: %s (0x%08x)", comp->name,
      port->index, (err == OMX_ErrorNone ? "" : "not "),
      (enabled ? "enabled" : "disabled"), gst_omx_error_to_string (err), err);

  return err;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_wait_enabled (GstOMXPort * port, GstClockTime timeout)
{
  OMX_ERRORTYPE err;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  g_mutex_lock (&port->comp->lock);
  err = gst_omx_port_wait_enabled_unlocked (port, timeout);
  g_mutex_unlock (&port->comp->lock);

  return err;
}

gboolean
gst_omx_port_is_enabled (GstOMXPort * port)
{
  gboolean enabled;

  g_return_val_if_fail (port != NULL, FALSE);

  gst_omx_port_update_port_definition (port, NULL);
  enabled = ! !port->port_def.bEnabled;

  GST_DEBUG_OBJECT (port->comp->parent, "%s port %u is enabled: %d",
      port->comp->name, port->index, enabled);

  return enabled;
}

/* NOTE: Uses comp->lock and comp->messages_lock */
OMX_ERRORTYPE
gst_omx_port_mark_reconfigured (GstOMXPort * port)
{
  GstOMXComponent *comp;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  g_return_val_if_fail (port != NULL, OMX_ErrorUndefined);

  comp = port->comp;

  g_mutex_lock (&comp->lock);
  GST_INFO_OBJECT (comp->parent, "Marking %s port %u is reconfigured",
      comp->name, port->index);

  gst_omx_component_handle_messages (comp);

  if ((err = comp->last_error) != OMX_ErrorNone)
    goto done;

  port->configured_settings_cookie = port->settings_cookie;

  if (port->port_def.eDir == OMX_DirOutput) {
    GList *l;

    for (l = comp->pending_reconfigure_outports; l; l = l->next) {
      if (l->data == (gpointer) port) {
        comp->pending_reconfigure_outports =
            g_list_delete_link (comp->pending_reconfigure_outports, l);
        break;
      }
    }
    if (!comp->pending_reconfigure_outports)
      gst_omx_component_send_message (comp, NULL);
  }

done:
  gst_omx_port_update_port_definition (port, NULL);

  INFO_IF_OK (comp->parent, err, "Marked %s port %u as reconfigured: %s "
      "(0x%08x)", comp->name, port->index, gst_omx_error_to_string (err), err);

  g_mutex_unlock (&comp->lock);

  return err;
}

/* The OMX specs states that the nBufferCountActual of a port has to default
 * to its nBufferCountMin. If we don't change nBufferCountActual we purely rely
 * on this default. But in some cases, OMX may change nBufferCountMin before we
 * allocate buffers. Like for example when configuring the input ports with the
 * actual format, it may decrease the number of minimal buffers required.
 * This method checks this and update nBufferCountActual if needed so we'll use
 * less buffers than the worst case in such scenarios.
 */
gboolean
gst_omx_port_ensure_buffer_count_actual (GstOMXPort * port, guint extra)
{
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  guint nb;

  gst_omx_port_get_port_definition (port, &port_def);

  nb = port_def.nBufferCountMin + extra;
  if (port_def.nBufferCountActual != nb) {
    port_def.nBufferCountActual = nb;

    GST_DEBUG_OBJECT (port->comp->parent,
        "set port %d nBufferCountActual to %d", (guint) port->index, nb);

    if (gst_omx_port_update_port_definition (port, &port_def) != OMX_ErrorNone)
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_omx_port_update_buffer_count_actual (GstOMXPort * port, guint nb)
{
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  gst_omx_port_get_port_definition (port, &port_def);

  if (nb < port_def.nBufferCountMin) {
    GST_DEBUG_OBJECT (port->comp->parent,
        "Requested to use %d buffers on port %d but it's minimum is %d", nb,
        (guint) port->index, (guint) port_def.nBufferCountMin);

    nb = port_def.nBufferCountMin;
  }

  if (port_def.nBufferCountActual != nb) {
    port_def.nBufferCountActual = nb;

    GST_DEBUG_OBJECT (port->comp->parent,
        "set port %d nBufferCountActual to %d", (guint) port->index, nb);

    if (gst_omx_port_update_port_definition (port, &port_def) != OMX_ErrorNone)
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_omx_port_set_dmabuf (GstOMXPort * port, gboolean dmabuf)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ALG_PORT_PARAM_BUFFER_MODE buffer_mode;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&buffer_mode);
  buffer_mode.nPortIndex = port->index;

  if (dmabuf)
    buffer_mode.eMode = OMX_ALG_BUF_DMA;
  else
    buffer_mode.eMode = OMX_ALG_BUF_NORMAL;

  err =
      gst_omx_component_set_parameter (port->comp,
      (OMX_INDEXTYPE) OMX_ALG_IndexPortParamBufferMode, &buffer_mode);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (port->comp->parent,
        "Failed to set port %d in %sdmabuf mode: %s (0x%08x)",
        port->index, dmabuf ? "" : "non-", gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
#else
  /* dmabuf not supported for this platform */
  return FALSE;
#endif
}

gboolean
gst_omx_port_set_subframe (GstOMXPort * port, gboolean enabled)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ALG_VIDEO_PARAM_SUBFRAME subframe_mode;
  OMX_ERRORTYPE err;
  GST_DEBUG_OBJECT (port->comp->parent, "%s subframe mode for Zynq",
      enabled ? "Enable" : "Disable");
  GST_OMX_INIT_STRUCT (&subframe_mode);
  subframe_mode.nPortIndex = port->index;

  subframe_mode.bEnableSubframe = enabled;

  err = gst_omx_component_set_parameter (port->comp,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSubframe, &subframe_mode);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (port->comp->parent,
        "Failed to %s subframe mode on port %d: %s (0x%08x)",
        enabled ? "enable" : "disable", port->index,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
#else
  /* subframe mode is not supported on this platform */
  return FALSE;
#endif
}

gboolean
gst_omx_port_get_subframe (GstOMXPort * port)
{
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  OMX_ALG_VIDEO_PARAM_SUBFRAME subframe_mode;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&subframe_mode);
  subframe_mode.nPortIndex = port->index;

  err = gst_omx_component_get_parameter (port->comp,
      (OMX_INDEXTYPE) OMX_ALG_IndexParamVideoSubframe, &subframe_mode);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (port->comp->parent,
        "Failed to get subframe mode on port %d: %s (0x%08x)",
        port->index, gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return subframe_mode.bEnableSubframe;
#else
  /* subframe mode is not supported on this platform */
  return FALSE;
#endif
}

typedef GType (*GGetTypeFunction) (void);

static const GGetTypeFunction types[] = {
  gst_omx_analog_audio_sink_get_type, gst_omx_hdmi_audio_sink_get_type,
  gst_omx_mpeg2_video_dec_get_type, gst_omx_mpeg4_video_dec_get_type,
  gst_omx_h264_dec_get_type, gst_omx_h263_dec_get_type,
  gst_omx_wmv_dec_get_type, gst_omx_mpeg4_video_enc_get_type,
  gst_omx_h264_enc_get_type, gst_omx_h263_enc_get_type,
  gst_omx_aac_enc_get_type, gst_omx_mjpeg_dec_get_type,
  gst_omx_aac_dec_get_type, gst_omx_mp3_dec_get_type,
  gst_omx_aac_dec_get_type, gst_omx_mp3_enc_get_type,
  gst_omx_amr_dec_get_type
#ifdef HAVE_VP8
      , gst_omx_vp8_dec_get_type
#endif
#ifdef HAVE_THEORA
      , gst_omx_theora_dec_get_type
#endif
#ifdef HAVE_HEVC
      , gst_omx_h265_enc_get_type, gst_omx_h265_dec_get_type
#endif
};

struct TypeOffest
{
  GType (*get_type) (void);
  glong offset;
};

static const struct TypeOffest base_types[] = {
  {gst_omx_audio_sink_get_type, G_STRUCT_OFFSET (GstOMXAudioSinkClass, cdata)},
  {gst_omx_video_dec_get_type, G_STRUCT_OFFSET (GstOMXVideoDecClass, cdata)},
  {gst_omx_video_enc_get_type, G_STRUCT_OFFSET (GstOMXVideoEncClass, cdata)},
  {gst_omx_audio_dec_get_type, G_STRUCT_OFFSET (GstOMXAudioDecClass, cdata)},
  {gst_omx_audio_enc_get_type, G_STRUCT_OFFSET (GstOMXAudioEncClass, cdata)},
};

static GKeyFile *config = NULL;
GKeyFile *
gst_omx_get_configuration (void)
{
  return config;
}

const gchar *
gst_omx_error_to_string (OMX_ERRORTYPE err)
{
  guint err_u = (guint) err;

  switch (err_u) {
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
    case OMX_ErrorSeperateTablesUsed:
      return "Separate tables used";
    case OMX_ErrorTunnelingUnsupported:
      return "Tunneling unsupported";
#if OMX_VERSION_MINOR == 1
    case OMX_ErrorInvalidComponent:
      return "Invalid component";
    case OMX_ErrorInvalidState:
      return "Invalid state";
    case OMX_ErrorPortUnresponsiveDuringAllocation:
      return "Port unresponsive during allocation";
    case OMX_ErrorPortUnresponsiveDuringDeallocation:
      return "Port unresponsive during deallocation";
    case OMX_ErrorPortUnresponsiveDuringStop:
      return "Port unresponsive during stop";
    case OMX_ErrorContentPipeOpenFailed:
      return "Content pipe open failed";
    case OMX_ErrorContentPipeCreationFailed:
      return "Content pipe creation failed";
#endif
    default:
      if (err_u >= (guint) OMX_ErrorKhronosExtensions
          && err_u < (guint) OMX_ErrorVendorStartUnused) {
        return "Khronos extension error";
      } else if (err_u >= (guint) OMX_ErrorVendorStartUnused
          && err_u < (guint) OMX_ErrorMax) {
        return "Vendor specific error";
      } else {
        return "Unknown error";
      }
  }
}

const gchar *
gst_omx_state_to_string (OMX_STATETYPE state)
{
  switch (state) {
    case OMX_StateInvalid:
      return "Invalid";
    case OMX_StateLoaded:
      return "Loaded";
    case OMX_StateIdle:
      return "Idle";
    case OMX_StateExecuting:
      return "Executing";
    case OMX_StatePause:
      return "Pause";
    case OMX_StateWaitForResources:
      return "WaitForResources";
    default:
      if (state >= OMX_StateKhronosExtensions
          && state < OMX_StateVendorStartUnused)
        return "KhronosExtensionState";
      else if (state >= OMX_StateVendorStartUnused && state < OMX_StateMax)
        return "CustomVendorState";
      break;
  }
  return "Unknown state";
}

const gchar *
gst_omx_command_to_string (OMX_COMMANDTYPE cmd)
{
  switch (cmd) {
    case OMX_CommandStateSet:
      return "SetState";
    case OMX_CommandFlush:
      return "Flush";
    case OMX_CommandPortDisable:
      return "DisablePort";
    case OMX_CommandPortEnable:
      return "EnablePort";
    case OMX_CommandMarkBuffer:
      return "MarkBuffer";
    default:
      if (cmd >= OMX_CommandKhronosExtensions
          && cmd < OMX_CommandVendorStartUnused)
        return "KhronosExtensionCommand";
      if (cmd >= OMX_CommandVendorStartUnused && cmd < OMX_CommandMax)
        return "VendorExtensionCommand";
      break;
  }
  return "Unknown command";
}

struct BufferFlagString
{
  guint32 flag;
  const gchar *str;
};

struct BufferFlagString buffer_flags_map[] = {
  {OMX_BUFFERFLAG_EOS, "eos"},
  {OMX_BUFFERFLAG_STARTTIME, "start-time"},
  {OMX_BUFFERFLAG_DECODEONLY, "decode-only"},
  {OMX_BUFFERFLAG_DATACORRUPT, "data-corrupt"},
  {OMX_BUFFERFLAG_ENDOFFRAME, "end-of-frame"},
  {OMX_BUFFERFLAG_SYNCFRAME, "sync-frame"},
  {OMX_BUFFERFLAG_EXTRADATA, "extra-data"},
  {OMX_BUFFERFLAG_CODECCONFIG, "codec-config"},
  /* Introduced in OMX 1.2.0 */
#ifdef OMX_BUFFERFLAG_TIMESTAMPINVALID
  {OMX_BUFFERFLAG_TIMESTAMPINVALID, "timestamp-invalid"},
#endif
#ifdef OMX_BUFFERFLAG_READONLY
  {OMX_BUFFERFLAG_READONLY, "read-only"},
#endif
#ifdef OMX_BUFFERFLAG_ENDOFSUBFRAME
  {OMX_BUFFERFLAG_ENDOFSUBFRAME, "end-of-subframe"},
#endif
#ifdef OMX_BUFFERFLAG_SKIPFRAME
  {OMX_BUFFERFLAG_SKIPFRAME, "skip-frame"},
#endif
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  {OMX_ALG_BUFFERFLAG_TOP_FIELD, "top-field"},
  {OMX_ALG_BUFFERFLAG_BOT_FIELD, "bottom-field"},
#endif
  {0, NULL},
};


const gchar *
gst_omx_buffer_flags_to_string (guint32 flags)
{
  GString *s = NULL;
  guint i;
  const gchar *str;

  if (flags == 0)
    return "";

  /* Keep a cache of the string representation of the flags so we don't allocate
   * and free strings for each buffer. In practice we should only have a handfull
   * of flags so the cache won't consume much memory. */
  if (!buffer_flags_str) {
    G_LOCK (buffer_flags_str);
    buffer_flags_str = g_hash_table_new_full (NULL, NULL, NULL, g_free);
    G_UNLOCK (buffer_flags_str);
  }

  str = g_hash_table_lookup (buffer_flags_str, GUINT_TO_POINTER (flags));
  if (str)
    return str;

  for (i = 0; buffer_flags_map[i].str != NULL; i++) {
    if ((flags & buffer_flags_map[i].flag) == 0)
      continue;

    if (!s)
      s = g_string_new (buffer_flags_map[i].str);
    else
      g_string_append_printf (s, ", %s", buffer_flags_map[i].str);
  }

  if (!s)
    return "<unknown>";

  str = g_string_free (s, FALSE);

  G_LOCK (buffer_flags_str);
  /* Transfer ownership of str to hash table */
  g_hash_table_insert (buffer_flags_str, GUINT_TO_POINTER (flags),
      (gchar *) str);
  G_UNLOCK (buffer_flags_str);

  return str;
}

#if defined(USE_OMX_TARGET_RPI)
#define DEFAULT_HACKS (GST_OMX_HACK_NO_COMPONENT_ROLE | GST_OMX_HACK_HEIGHT_MULTIPLE_16)
#else
#define DEFAULT_HACKS (0)
#endif

guint64
gst_omx_parse_hacks (gchar ** hacks)
{
  guint64 hacks_flags = DEFAULT_HACKS;

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
    else if (g_str_equal (*hacks, "no-component-reconfigure"))
      hacks_flags |= GST_OMX_HACK_NO_COMPONENT_RECONFIGURE;
    else if (g_str_equal (*hacks, "no-empty-eos-buffer"))
      hacks_flags |= GST_OMX_HACK_NO_EMPTY_EOS_BUFFER;
    else if (g_str_equal (*hacks, "drain-may-not-return"))
      hacks_flags |= GST_OMX_HACK_DRAIN_MAY_NOT_RETURN;
    else if (g_str_equal (*hacks, "no-component-role"))
      hacks_flags |= GST_OMX_HACK_NO_COMPONENT_ROLE;
    else if (g_str_equal (*hacks, "no-disable-outport"))
      hacks_flags |= GST_OMX_HACK_NO_DISABLE_OUTPORT;
    else if (g_str_equal (*hacks, "signals-premature-eos"))
      hacks_flags |= GST_OMX_HACK_SIGNALS_PREMATURE_EOS;
    else if (g_str_equal (*hacks, "height-multiple-16"))
      hacks_flags |= GST_OMX_HACK_HEIGHT_MULTIPLE_16;
    else if (g_str_equal (*hacks, "pass-profile-to-decoder"))
      hacks_flags |= GST_OMX_HACK_PASS_PROFILE_TO_DECODER;
    else if (g_str_equal (*hacks, "pass-color-format-to-decoder"))
      hacks_flags |= GST_OMX_HACK_PASS_COLOR_FORMAT_TO_DECODER;
    else if (g_str_equal (*hacks, "ensure-buffer-count-actual"))
      hacks_flags |= GST_OMX_HACK_ENSURE_BUFFER_COUNT_ACTUAL;
    else
      GST_WARNING ("Unknown hack: %s", *hacks);
    hacks++;
  }

  return hacks_flags;
}


void
gst_omx_set_default_role (GstOMXClassData * class_data,
    const gchar * default_role)
{
  if (!class_data->component_role)
    class_data->component_role = default_role;
}

static void
_class_init (gpointer g_class, gpointer data)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXClassData *class_data = NULL;
  GKeyFile *config;
  const gchar *element_name = data;
  GError *err;
  gchar *core_name, *component_name, *component_role;
  gint in_port_index, out_port_index;
  gchar *template_caps;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar **hacks;
  int i;

  if (!element_name)
    return;

  /* Find the GstOMXClassData for this class */
  for (i = 0; i < G_N_ELEMENTS (base_types); i++) {
    GType gtype = base_types[i].get_type ();

    if (G_TYPE_CHECK_CLASS_TYPE (g_class, gtype)) {
      class_data = (GstOMXClassData *)
          (((guint8 *) g_class) + base_types[i].offset);
      break;
    }
  }

  g_assert (class_data != NULL);

  config = gst_omx_get_configuration ();

  /* This will alwaxys succeed, see check in plugin_init */
  core_name = g_key_file_get_string (config, element_name, "core-name", NULL);
  g_assert (core_name != NULL);
  class_data->core_name = core_name;
  component_name =
      g_key_file_get_string (config, element_name, "component-name", NULL);
  g_assert (component_name != NULL);
  class_data->component_name = component_name;

  /* If this fails we simply don't set a role */
  if ((component_role =
          g_key_file_get_string (config, element_name, "component-role",
              NULL))) {
    GST_DEBUG ("Using component-role '%s' for element '%s'", component_role,
        element_name);
    class_data->component_role = component_role;
  }


  /* Now set the inport/outport indizes and assume sane defaults */
  err = NULL;
  in_port_index =
      g_key_file_get_integer (config, element_name, "in-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'in-port-index' set for element '%s', auto-detecting: %s",
        element_name, err->message);
    in_port_index = -1;
    g_error_free (err);
  }
  class_data->in_port_index = in_port_index;

  err = NULL;
  out_port_index =
      g_key_file_get_integer (config, element_name, "out-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'out-port-index' set for element '%s', auto-detecting: %s",
        element_name, err->message);
    out_port_index = -1;
    g_error_free (err);
  }
  class_data->out_port_index = out_port_index;

  /* Add pad templates */
  err = NULL;
  if (class_data->type != GST_OMX_COMPONENT_TYPE_SOURCE) {
    if (!(template_caps =
            g_key_file_get_string (config, element_name, "sink-template-caps",
                &err))) {
      GST_DEBUG
          ("No sink template caps specified for element '%s', using default '%s'",
          element_name, class_data->default_sink_template_caps);
      caps = gst_caps_from_string (class_data->default_sink_template_caps);
      g_assert (caps != NULL);
      g_error_free (err);
    } else {
      caps = gst_caps_from_string (template_caps);
      if (!caps) {
        GST_DEBUG
            ("Could not parse sink template caps '%s' for element '%s', using default '%s'",
            template_caps, element_name,
            class_data->default_sink_template_caps);
        caps = gst_caps_from_string (class_data->default_sink_template_caps);
        g_assert (caps != NULL);
      }
    }
    templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    g_free (template_caps);
    gst_element_class_add_pad_template (element_class, templ);
    gst_caps_unref (caps);
  }

  err = NULL;
  if (class_data->type != GST_OMX_COMPONENT_TYPE_SINK) {
    if (!(template_caps =
            g_key_file_get_string (config, element_name, "src-template-caps",
                &err))) {
      GST_DEBUG
          ("No src template caps specified for element '%s', using default '%s'",
          element_name, class_data->default_src_template_caps);
      caps = gst_caps_from_string (class_data->default_src_template_caps);
      g_assert (caps != NULL);
      g_error_free (err);
    } else {
      caps = gst_caps_from_string (template_caps);
      if (!caps) {
        GST_DEBUG
            ("Could not parse src template caps '%s' for element '%s', using default '%s'",
            template_caps, element_name, class_data->default_src_template_caps);
        caps = gst_caps_from_string (class_data->default_src_template_caps);
        g_assert (caps != NULL);
      }
    }
    templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    g_free (template_caps);
    gst_element_class_add_pad_template (element_class, templ);
    gst_caps_unref (caps);
  }

  if ((hacks =
          g_key_file_get_string_list (config, element_name, "hacks", NULL,
              NULL))) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar **walk = hacks;

    while (*walk) {
      GST_DEBUG ("Using hack: %s", *walk);
      walk++;
    }
#endif

    class_data->hacks = gst_omx_parse_hacks (hacks);
    g_strfreev (hacks);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
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
  static const gchar *gst_omx_config_dir = GST_OMX_CONFIG_DIR;

  GST_DEBUG_CATEGORY_INIT (gstomx_debug, "omx", 0, "gst-omx");
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_debug_category, "omxvideo", 0,
      "gst-omx-video");
  GST_DEBUG_CATEGORY_INIT (OMX_API_TRACE, "OMX_API_TRACE", 0,
      "gst-omx performace");

  /* Read configuration file gstomx.conf from the preferred
   * configuration directories */
  env_config_dir = g_strdup (g_getenv (*env_config_name));
  user_config_dir = g_get_user_config_dir ();
  system_config_dirs = g_get_system_config_dirs ();
  config_dirs =
      g_new (gchar *, g_strv_length ((gchar **) system_config_dirs) + 4);

  i = 0;
  j = 0;
  if (env_config_dir)
    config_dirs[i++] = (gchar *) env_config_dir;
  config_dirs[i++] = (gchar *) user_config_dir;
  while (system_config_dirs[j])
    config_dirs[i++] = (gchar *) system_config_dirs[j++];
  config_dirs[i++] = (gchar *) gst_omx_config_dir;
  config_dirs[i++] = NULL;

  gst_plugin_add_dependency (plugin, env_config_name,
      (const gchar **) (config_dirs + (env_config_dir ? 1 : 0)), config_name,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  config = g_key_file_new ();
  if (!g_key_file_load_from_dirs (config, *config_name,
          (const gchar **) config_dirs, NULL, G_KEY_FILE_NONE, &err)) {
#ifdef USE_OMX_TARGET_GENERIC
    GST_INFO ("No configuration file found; "
        "ignore as gst-omx has been built with the generic target used only for testing");
#else
    {
      gchar *paths;

      paths = g_strjoinv (":", config_dirs);
      GST_ERROR
          ("Failed to load configuration file: %s (searched in: %s as per "
          "GST_OMX_CONFIG_DIR environment variable, the xdg user config "
          "directory (or XDG_CONFIG_HOME) and the system config directory "
          "(or XDG_CONFIG_DIRS)", err->message, paths);
      g_free (paths);
    }
#endif /* USE_OMX_TARGET_GENERIC */

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
    type_info.class_init = _class_init;
    type_info.class_data = g_strdup (elements[i]);
    type_name = g_strdup_printf ("%s-%s", g_type_name (type), elements[i]);
    if (g_type_from_name (type_name) != G_TYPE_INVALID) {
      GST_ERROR ("Type '%s' already exists for element '%s'", type_name,
          elements[i]);
      g_free (type_name);
      continue;
    }
    subtype = g_type_register_static (type, type_name, &type_info, 0);
    g_free (type_name);
    gst_element_register (plugin, elements[i], rank, subtype);
  }
  g_strfreev (elements);

done:
  g_free (env_config_dir);
  g_free (config_dirs);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    omx,
    "GStreamer OpenMAX Plug-ins",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
