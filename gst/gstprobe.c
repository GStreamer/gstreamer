/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstprobe.h: Header for GstProbe object
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include "gst_private.h"
#include "gstprobe.h"

/**
 * gst_probe_new:
 * @single_shot: TRUE if a single shot probe is required
 * @callback: the function to call when the probe is triggered
 * @user_data: data passed to the callback function
 *
 * Create a new probe with the specified parameters
 *
 * Returns: a new #GstProbe.
 */
GstProbe *
gst_probe_new (gboolean single_shot,
    GstProbeCallback callback, gpointer user_data)
{
  GstProbe *probe;

  g_return_val_if_fail (callback, NULL);

  probe = g_new0 (GstProbe, 1);

  probe->single_shot = single_shot;
  probe->callback = callback;
  probe->user_data = user_data;

  return probe;
}

/**
 * gst_probe_destroy:
 * @probe: The probe to destroy
 *
 * Free the memeory associated with the probe.
 */
void
gst_probe_destroy (GstProbe * probe)
{
  g_return_if_fail (probe);

#ifdef USE_POISONING
  memset (probe, 0xff, sizeof (*probe));
#endif

  g_free (probe);
}

/**
 * gst_probe_perform:
 * @probe: The probe to trigger
 * @data: the GstData that triggered the probe.
 *
 * Perform the callback associated with the given probe.
 *
 * Returns: the result of the probe callback function.
 */
gboolean
gst_probe_perform (GstProbe * probe, GstData ** data)
{
  gboolean res = TRUE;

  g_return_val_if_fail (probe, res);

  if (probe->callback)
    res = probe->callback (probe, data, probe->user_data);

  return res;
}

/**
 * gst_probe_dispatcher_new:
 *
 * Create a new probe dispatcher
 *
 * Returns: a new probe dispatcher.
 */
GstProbeDispatcher *
gst_probe_dispatcher_new (void)
{
  GstProbeDispatcher *disp;

  disp = g_new0 (GstProbeDispatcher, 1);

  gst_probe_dispatcher_init (disp);

  return disp;
}

/**
 * gst_probe_dispatcher_destroy:
 * @disp: the dispatcher to destroy
 *
 * Free the memory allocated by the probe dispatcher. All pending
 * probes are removed first.
 */
void
gst_probe_dispatcher_destroy (GstProbeDispatcher * disp)
{
  g_return_if_fail (disp);

#ifdef USE_POISONING
  memset (disp, 0xff, sizeof (*disp));
#endif

  /* FIXME, free pending probes */
  g_free (disp);
}

/**
 * gst_probe_dispatcher_init:
 * @disp: the dispatcher to initialize
 *
 * Initialize the dispatcher. Useful for statically allocated probe
 * dispatchers.
 */
void
gst_probe_dispatcher_init (GstProbeDispatcher * disp)
{
  g_return_if_fail (disp);

  disp->active = TRUE;
  disp->probes = NULL;
}

/**
 * gst_probe_dispatcher_set_active:
 * @disp: the dispatcher to activate
 * @active: boolean to indicate activation or deactivation
 *
 * Activate or deactivate the given dispatcher
 * dispatchers.
 */
void
gst_probe_dispatcher_set_active (GstProbeDispatcher * disp, gboolean active)
{
  g_return_if_fail (disp);

  disp->active = active;
}

/**
 * gst_probe_dispatcher_add_probe:
 * @disp: the dispatcher to add the probe to
 * @probe: the probe to add to the dispatcher
 *
 * Adds the given probe to the dispatcher.
 */
void
gst_probe_dispatcher_add_probe (GstProbeDispatcher * disp, GstProbe * probe)
{
  g_return_if_fail (disp);
  g_return_if_fail (probe);

  disp->probes = g_slist_prepend (disp->probes, probe);
}

/**
 * gst_probe_dispatcher_remove_probe:
 * @disp: the dispatcher to remove the probe from
 * @probe: the probe to remove from the dispatcher
 *
 * Removes the given probe from the dispatcher.
 */
void
gst_probe_dispatcher_remove_probe (GstProbeDispatcher * disp, GstProbe * probe)
{
  g_return_if_fail (disp);
  g_return_if_fail (probe);

  disp->probes = g_slist_remove (disp->probes, probe);
}

/**
 * gst_probe_dispatcher_dispatch:
 * @disp: the dispatcher to dispatch
 * @data: the data that triggered the dispatch
 *
 * Trigger all registered probes on the given dispatcher.
 *
 * Returns: TRUE if all callbacks returned TRUE.
 */
gboolean
gst_probe_dispatcher_dispatch (GstProbeDispatcher * disp, GstData ** data)
{
  GSList *walk;
  gboolean res = TRUE;

  g_return_val_if_fail (disp, res);

  walk = disp->probes;
  while (walk) {
    GstProbe *probe = (GstProbe *) walk->data;

    walk = g_slist_next (walk);

    res &= gst_probe_perform (probe, data);
    if (probe->single_shot) {
      disp->probes = g_slist_remove (disp->probes, probe);

      gst_probe_destroy (probe);
    }
  }

  return res;
}
