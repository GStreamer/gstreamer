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

#include <assert.h>
#include <string.h>
#include <libav/avformat.h>

#include <gst/gst.h>

static GHashTable *global_types = NULL;

extern GstCaps*	gst_ffmpegcodec_codec_context_to_caps (AVCodecContext *ctx, int id);

static GstCaps*
gst_ffmpegtypes_typefind (GstBuffer *buffer, gpointer priv)
{
  AVInputFormat *in_plugin;
  AVInputFormat *highest = NULL;
  gint max = 0;
  gint res = 0;
  
  in_plugin = first_iformat;

  while (in_plugin) {
    if (in_plugin->read_probe) {
      AVProbeData probe_data;

      probe_data.filename = "";
      probe_data.buf = GST_BUFFER_DATA (buffer);
      probe_data.buf_size = GST_BUFFER_SIZE (buffer);

      res = in_plugin->read_probe (&probe_data);
      if (res > max) {
	max = res;
	highest = in_plugin;
      }
    }
    in_plugin = in_plugin->next;
  }
  if (highest) {
    GstCaps *caps;
    caps = g_hash_table_lookup (global_types, highest->name);
    return caps;
  }
	
  return NULL;
}

#define ADD_TYPE(key,caps) g_hash_table_insert (global_types, (key), (caps))

static void
register_standard_formats (void)
{
  global_types = g_hash_table_new (g_str_hash, g_str_equal);

  ADD_TYPE ("avi",    	GST_CAPS_NEW ("ffmpeg_type_avi",  "video/avi", NULL));
  ADD_TYPE ("mpeg",   	GST_CAPS_NEW ("ffmpeg_type_mpeg", "video/mpeg", 
			  		"systemstream", GST_PROPS_BOOLEAN (TRUE)));
  ADD_TYPE ("mpegts", 	GST_CAPS_NEW ("ffmpeg_type_mpegts", "video/x-mpegts", 
			  		"systemstream", GST_PROPS_BOOLEAN (TRUE)));
  ADD_TYPE ("rm",    	GST_CAPS_NEW ("ffmpeg_type_rm",  "audio/x-pn-realaudio", NULL));
  ADD_TYPE ("asf",    	GST_CAPS_NEW ("ffmpeg_type_asf", "video/x-ms-asf", NULL));
  ADD_TYPE ("avi",    	GST_CAPS_NEW ("ffmpeg_type_avi", "video/avi", 
			  		"format", GST_PROPS_STRING ("AVI")));
  ADD_TYPE ("mov",    	GST_CAPS_NEW ("ffmpeg_type_mov", "video/quicktime", NULL));
  ADD_TYPE ("swf",    	GST_CAPS_NEW ("ffmpeg_type_swf", "application/x-shockwave-flash", NULL));
  ADD_TYPE ("au",    	GST_CAPS_NEW ("ffmpeg_type_au", "audio/basic", NULL));
  ADD_TYPE ("mov",    	GST_CAPS_NEW ("ffmpeg_type_mov", "video/quicktime", NULL));
}
	
gboolean
gst_ffmpegtypes_register (GstPlugin *plugin)
{
  AVInputFormat *in_plugin;
  GstTypeFactory *factory;
  GstTypeDefinition *definition;
  
  in_plugin = first_iformat;

  while (in_plugin) {
    gchar *type_name;
    gchar *p;

    if (!in_plugin->read_probe)
      goto next;
    
    /* construct the type */
    type_name = g_strdup_printf("fftype_%s", in_plugin->name);

    p = type_name;

    while (*p) {
      if (*p == '.') *p = '_';
      p++;
    }

    definition = g_new0 (GstTypeDefinition, 1);
    definition->name = type_name;
    definition->mime = type_name;
    definition->exts = g_strdup (in_plugin->extensions);
    definition->typefindfunc = gst_ffmpegtypes_typefind;

    factory = gst_type_factory_new (definition);

    /* The very last thing is to register the elementfactory with the plugin. */
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
next:
    in_plugin = in_plugin->next;
  }
  register_standard_formats ();

  return TRUE;
}
