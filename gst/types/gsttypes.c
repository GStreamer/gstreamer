/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

gint mp3_typefind(GstBuffer *buf,gpointer *private);
gint wav_typefind(GstBuffer *buf,gpointer *private);

GstTypeFactory _factories[] = {
  { "audio/raw", ".raw", NULL },
  { "audio/mpeg audio/mp3", ".mp2 .mp3 .mpa .mpega", mp3_typefind },
  { "audio/wav", ".wav", wav_typefind },
  { "audio/ac3", ".ac3", NULL },
  { "image/jpeg", ".jpg .jpeg", NULL },
  { "video/raw image/raw", ".raw", NULL },
  { "video/mpeg video/mpeg1 video/mpeg-system", ".mpg", NULL },
  { "video/x-msvideo video/msvideo video/avi", ".avi", NULL },
  { NULL, NULL, NULL },
};


/* check to see if a buffer indicates the presence of an mp3 frame
 * NOTE that this only checks for a potentially valid mp3 frame header
 * and doesn't guarantee that it's a fully valid mp3 audio stream */
gboolean mp3_typefind(GstBuffer *buf,gpointer *private) {
  gulong head = GULONG_FROM_BE(*((gulong *)GST_BUFFER_DATA(buf)));

  if ((head & 0xffe00000) != 0xffe00000)
    return FALSE;
  if (!((head >> 17) & 3))
    return FALSE;
  if (((head >> 12) & 0xf) == 0xf)
    return FALSE;
  if (!((head >> 12) & 0xf))
    return FALSE;
  if (((head >> 10) & 0x3) == 0x3)
    return FALSE;

  return TRUE;
}

gboolean wav_typefind(GstBuffer *buf,gpointer *private) {
  gulong *data = (gulong *)GST_BUFFER_DATA(buf);

  if (strncmp((char *)data[0], "RIFF", 4)) return FALSE;
  if (strncmp((char *)data[2], "WAVE", 4)) return FALSE;

  return TRUE;
}


GstPlugin *plugin_init(GModule *module) {
  GstPlugin *plugin;
  int i = 0;

  if (gst_plugin_find("gsttypes") != NULL)
    return NULL;

  plugin = gst_plugin_new("gsttypes");
  g_return_val_if_fail(plugin != NULL,NULL);

  while (_factories[i].mime) {
    gst_type_register(&_factories[i]);
//    DEBUG("added factory #%d '%s'\n",i,_factories[i].mime);
    i++;
  }

  gst_info("gsttypes: loaded %d standard types\n",i);

  return plugin;
}
