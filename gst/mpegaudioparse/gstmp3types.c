/* GStreamer
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

/*#define DEBUG_ENABLED */
#include <gst/gst.h>
#include <string.h> /* memcmp */

static GstCaps* mp3_type_find(GstBuffer *buf, gpointer private);

static GstTypeDefinition mp3type_definitions[] = {
  { "mp3types_audio/mp3", "audio/mp3", ".mp3 .mp2 .mp1 .mpga", mp3_type_find },
  { NULL, NULL, NULL, NULL },
};

static GstCaps* 
mp3_type_find(GstBuffer *buf, gpointer private) 
{
  guint8 *data;
  gint size;
  gulong head;
  GstCaps *caps;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG (0,"mp3typefind: typefind");

  /* gracefully ripped from libid3 */
  if (size >= 3 &&
      data[0] == 'T' && data[1] == 'A' && data[2] == 'G') {
    /* ID V1 tags */
    data += 128;

    GST_DEBUG (0, "mp3typefind: detected ID3 Tag V1");
  }
  else {
    if (size >= 10 &&
        (data[0] == 'I' && data[1] == 'D' && data[2] == '3') &&
        data[3] < 0xff && data[4] < 0xff &&
        data[6] < 0x80 && data[7] < 0x80 && data[8] < 0x80 && data[9] < 0x80)
    {
      guint32 skip = 0;

      skip = (skip << 7) | (data[6] & 0x7f);
      skip = (skip << 7) | (data[7] & 0x7f);
      skip = (skip << 7) | (data[8] & 0x7f);
      skip = (skip << 7) | (data[9] & 0x7f);

      if (data[0] == 'I') {
	/* ID3V2 */
	/* footer present? */
	if (data[5] & 0x10)
          skip += 10;

	skip += 10;
      }

      GST_DEBUG (0, "mp3typefind: detected ID3 Tag V2 with %u bytes", skip);

      /* we currently accept a valid ID3 tag as an mp3 as some ID3 tags have invalid
       * offsets so the next check might fail */
      goto done;
    }
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

done:
  caps = gst_caps_new ("mp3_type_find", "audio/mp3", NULL);

  return caps;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gint i=0;

  while (mp3type_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_type_factory_new (&mp3type_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }

  /* gst_info("gsttypes: loaded %d mp3 types\n",i); */

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp3types",
  plugin_init
};
