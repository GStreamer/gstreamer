/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstaudio.h: convenience macros for pad capabilities
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

/* for people that are looking at this source: the purpose of these defines is
 * to make GstCaps a bit easier, in that you don't have to know all of the
 * properties that need to be defined. you can just use these macros. currently
 * (8/01) the only plugins that use these are the passthrough, speed, volume,
 * and [de]interleave plugins. so. these are for convenience only, and do not
 * specify the 'limits' of gstreamer. you might also use these definitions as a
 * base for making your own caps, if need be.
 *
 * for example, to make a source pad that can output mono streams of either
 * float or int:

    template = gst_padtemplate_new 
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/raw",
                                    GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/raw",
                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);

    srcpad = gst_pad_new_from_template(template,"src");

 * Andy Wingo, 18 August 2001 */

#define GST_AUDIO_INT_PAD_TEMPLATE_PROPS \
        gst_props_new (\
          "format",             GST_PROPS_STRING ("int"),\
            "law",              GST_PROPS_INT (0),\
            "endianness",       GST_PROPS_INT (G_BYTE_ORDER),\
            "signed",           GST_PROPS_LIST (\
            					  GST_PROPS_BOOLEAN (TRUE),\
            					  GST_PROPS_BOOLEAN(FALSE)\
            					),\
            "width",            GST_PROPS_LIST (GST_PROPS_INT(8), GST_PROPS_INT(16)),\
            "depth",            GST_PROPS_LIST (GST_PROPS_INT(8), GST_PROPS_INT(16)),\
            "rate",             GST_PROPS_INT_RANGE (4000, 96000),\
            "channels",         GST_PROPS_INT_RANGE (1, G_MAXINT),\
          NULL)

#define GST_AUDIO_INT_MONO_PAD_TEMPLATE_PROPS \
        gst_props_new (\
          "format",             GST_PROPS_STRING ("int"),\
            "law",              GST_PROPS_INT (0),\
            "endianness",       GST_PROPS_INT (G_BYTE_ORDER),\
            "signed",           GST_PROPS_LIST (\
            					  GST_PROPS_BOOLEAN (TRUE),\
            					  GST_PROPS_BOOLEAN(FALSE)\
            					),\
            "width",            GST_PROPS_LIST (GST_PROPS_INT(8), GST_PROPS_INT(16)),\
            "depth",            GST_PROPS_LIST (GST_PROPS_INT(8), GST_PROPS_INT(16)),\
            "rate",             GST_PROPS_INT_RANGE (4000, 96000),\
            "channels",         GST_PROPS_INT (1),\
          NULL)

#define GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS \
        gst_props_new (\
          "format",             GST_PROPS_STRING ("float"),\
            "layout",           GST_PROPS_STRING ("gfloat"),\
            "intercept",        GST_PROPS_FLOAT (0.0),\
            "slope",            GST_PROPS_FLOAT (1.0),\
            "rate",             GST_PROPS_INT_RANGE (4000, 96000),\
            "channels",         GST_PROPS_INT (1),\
            NULL)
