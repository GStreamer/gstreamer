/* bonobo-media-gstreamer-factry: Factory for GStreamer player using the
 * Bonobo:Media interfaces
 *
 * Copyright (C) 2001 Ali Abdin <aliabdin@aucegypt.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gnome.h>
#include <glib.h>
#include <bonobo.h>
#include <liboaf/liboaf.h>
#include "bonobo-media-gstreamer.h"

#include <gst/gst.h>
#include <config.h>

static BonoboObject *
gstreamer_factory (BonoboGenericFactory * factory, gpointer user_data)
{
  return BONOBO_OBJECT (bonobo_media_gstreamer_new ());
}

static void
init_bonobo (int argc, char **argv)
{
  CORBA_ORB orb;

  gnome_init_with_popt_table ("bonobo-media-gstreamer", VERSION,
      argc, argv, oaf_popt_options, 0, NULL);

  orb = oaf_init (argc, argv);

  if (bonobo_init (orb, NULL, NULL) == FALSE)
    g_error ("Could not initialize Bonobo");
}

static void
last_unref_cb (BonoboObject * bonobo_object, BonoboGenericFactory * factory)
{
  bonobo_object_unref (BONOBO_OBJECT (factory));
  gtk_main_quit ();
}

int
main (int argc, char **argv)
{
  BonoboGenericFactory *factory;

  gst_init (&argc, &argv);
  init_bonobo (argc, argv);

  factory = bonobo_generic_factory_new ("OAFIID:Bonobo_Media_GStreamer_Factory",
      gstreamer_factory, NULL);

  gtk_signal_connect (GTK_OBJECT (bonobo_context_running_get ()), "last_unref",
      GTK_SIGNAL_FUNC (last_unref_cb), factory);

  bonobo_main ();

  return 0;
}
