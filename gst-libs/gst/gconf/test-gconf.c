/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
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

#include "gconf.h"

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  printf ("Default video sink : %s\n",
      gst_gconf_get_string ("default/videosink"));
  printf ("Default audio sink : %s\n",
      gst_gconf_get_string ("default/audiosink"));
  printf ("Default video src : %s\n",
      gst_gconf_get_string ("default/videosrc"));
  printf ("Default audio src : %s\n",
      gst_gconf_get_string ("default/audiosrc"));
  return 0;
}
