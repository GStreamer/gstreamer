/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstpad.h: Header for GstPad object
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


#include "gstlog.h"
#include "gstprobe.h"


GstProbe*
gst_probe_new (gboolean single_shot, 
	       GstProbeCallback callback, 
	       gpointer user_data)
{
  GstProbe *probe;

  g_return_val_if_fail (callback, NULL);

  probe = g_new0 (GstProbe, 1);

  probe->single_shot = single_shot;
  probe->callback    = callback;
  probe->user_data   = user_data;
  
  return probe;
}

void
gst_probe_destroy (GstProbe *probe)
{
  g_return_if_fail (probe);

  g_free (probe);
}

gboolean
gst_probe_perform (GstProbe *probe, GstData *data)
{
  gboolean res = TRUE;

  g_return_val_if_fail (probe, res);
  
  if (probe->callback)
    res = probe->callback (probe, data, probe->user_data);
  
  return res;
}

GstProbeDispatcher*
gst_probe_dispatcher_new (void)
{
  GstProbeDispatcher *disp;
  
  disp = g_new0 (GstProbeDispatcher, 1);

  gst_probe_dispatcher_init (disp);
  
  return disp;
}

void		
gst_probe_dispatcher_destroy (GstProbeDispatcher *disp)
{
}

void
gst_probe_dispatcher_init (GstProbeDispatcher *disp)
{
  g_return_if_fail (disp);

  disp->active = TRUE;
  disp->probes = NULL;
}

void		
gst_probe_dispatcher_set_active (GstProbeDispatcher *disp, gboolean active)
{
  g_return_if_fail (disp);

  disp->active = active;
}

void
gst_probe_dispatcher_add_probe (GstProbeDispatcher *disp, GstProbe *probe)
{
  g_return_if_fail (disp);
  g_return_if_fail (probe);
  
  disp->probes = g_slist_prepend (disp->probes, probe);
}

void
gst_probe_dispatcher_remove_probe (GstProbeDispatcher *disp, GstProbe *probe)
{
  g_return_if_fail (disp);
  g_return_if_fail (probe);
  
  disp->probes = g_slist_remove (disp->probes, probe);
}

gboolean	
gst_probe_dispatcher_dispatch (GstProbeDispatcher *disp, GstData *data)
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
