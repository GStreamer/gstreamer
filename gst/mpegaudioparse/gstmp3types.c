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

//#define DEBUG_ENABLED
#include <gst/gst.h>
#include <string.h> /* memcmp */

static GstCaps* mp3_typefind(GstBuffer *buf, gpointer private);

static GstTypeDefinition mp3type_definitions[] = {
  { "mp3types_audio/mp3", "audio/mp3", ".mp3 .mp2 .mp1 .mpga", mp3_typefind },
  { NULL, NULL, NULL, NULL },
};

static GstCaps* 
mp3_typefind(GstBuffer *buf, gpointer private) 
{
  gchar *data;
  gulong head;
  
  data = GST_BUFFER_DATA(buf);
  GstCaps *caps;

  GST_DEBUG (0,"mp3typefind: typefind\n");
 
  /* check for ID3 Tag first and forward ID3 length */
  if (!memcmp (data, "ID3", 3))
  {
    guint32 skip;
    /* ignore next 3 bytes */
    data += 6;
    /* if you want that thing faster, do it */
    skip = GUINT32_FROM_BE(*((guint32 *)data));
    skip = (((skip & 0x7f000000) >> 3) |
	    ((skip & 0x007f0000) >> 2) |
	    ((skip & 0x00007f00) >> 1) |
	    ((skip & 0x0000007f) >> 0)) + 4 
    GST_DEBUG (0, "mp3typefind: detected ID3 Tag with %u bytes\n", skip + 6);
    /* return if buffer is not big enough */
    if (GST_BUFFER_SIZE (buf) < skip + 10)
    {
      GST_DEBUG (0, "mp3typefind: buffer too small to go on typefinding\n", skip + 6);
      return NULL;
    }
    data += skip;
  }
  
  /* now with the right postion, do typefinding */
  head = GULONG_FROM_BE(*((gulong *)data));
  if ((head & 0xffe00000) != 0xffe00000)
    return NULL;
  if (!((head >> 17) & 3))
    return NULL;
  if (((head >> 12) & 0xf) == 0xf)
    return NULL;
  if (!((head >> 12) & 0xf))
    return NULL;
  if (((head >> 10) & 0x3) == 0x3)
    return NULL;

  caps = gst_caps_new ("mp3_typefind", "audio/mp3", NULL);
//  gst_caps_set(caps,"layer",GST_PROPS_INT(4-((head>>17)&0x3)));

  return caps;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gint i=0;

  while (mp3type_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_typefactory_new (&mp3type_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }

//  gst_info("gsttypes: loaded %d mp3 types\n",i);

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp3types",
  plugin_init
};
