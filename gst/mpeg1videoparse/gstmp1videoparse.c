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

//#define GST_DEBUG_ENABLED
#include "gstmp1videoparse.h"

/* Start codes. */
#define SEQ_START_CODE 0x000001b3
#define GOP_START_CODE 0x000001b8
#define PICTURE_START_CODE 0x00000100
#define SLICE_MIN_START_CODE 0x00000101
#define SLICE_MAX_START_CODE 0x000001af
#define EXT_START_CODE 0x000001b5
#define USER_START_CODE 0x000001b2
#define SEQUENCE_ERROR_CODE 0x000001b4
#define SEQ_END_CODE 0x000001b7

/* elementfactory information */
static GstElementDetails mp1videoparse_details = {
  "MPEG 1 Video Parser",
  "Filter/Parser/Video",
  "Parses and frames MPEG 1 video streams, provides seek",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

static GstPadTemplate*
src_factory (void) 
{
  return
    gst_padtemplate_new (
  	"src",
  	GST_PAD_SRC,
  	GST_PAD_ALWAYS,
	gst_caps_new (
  	  "mp1videoparse_src",
    	  "video/mpeg",
	  gst_props_new (
    	    "mpegversion",   GST_PROPS_INT (1),
    	    "systemstream",  GST_PROPS_BOOLEAN (FALSE),
    	    "sliced",        GST_PROPS_BOOLEAN (TRUE),
	    NULL)),
	NULL);
}

static GstPadTemplate*
sink_factory (void)
{
  return
    gst_padtemplate_new (
  	"sink",
  	GST_PAD_SINK,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "mp1videoparse_sink",
    	  "video/mpeg",
	  gst_props_new (
    	    "mpegversion",   GST_PROPS_INT (1),
    	    "systemstream",  GST_PROPS_BOOLEAN (FALSE),
	    NULL)),
	NULL);
}

/* Mp1VideoParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_mp1videoparse_class_init	(Mp1VideoParseClass *klass);
static void	gst_mp1videoparse_init		(Mp1VideoParse *mp1videoparse);

static void	gst_mp1videoparse_chain		(GstPad *pad, GstBuffer *buf);
static void	gst_mp1videoparse_real_chain	(Mp1VideoParse *mp1videoparse, GstBuffer *buf, GstPad *outpad);

static void	gst_mp1videoparse_flush		(Mp1VideoParse *mp1videoparse);

static GstPadTemplate *src_template, *sink_template;

static GstElementClass *parent_class = NULL;
//static guint gst_mp1videoparse_signals[LAST_SIGNAL] = { 0 };

GType
mp1videoparse_get_type (void)
{
  static GType mp1videoparse_type = 0;

  if (!mp1videoparse_type) {
    static const GTypeInfo mp1videoparse_info = {
      sizeof(Mp1VideoParseClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mp1videoparse_class_init,
      NULL,
      NULL,
      sizeof(Mp1VideoParse),
      0,
      (GInstanceInitFunc)gst_mp1videoparse_init,
    };
    mp1videoparse_type = g_type_register_static(GST_TYPE_ELEMENT, "Mp1VideoParse", &mp1videoparse_info, 0);
  }
  return mp1videoparse_type;
}

static void
gst_mp1videoparse_class_init (Mp1VideoParseClass *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

}

static void
gst_mp1videoparse_init (Mp1VideoParse *mp1videoparse)
{
  mp1videoparse->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_pad_set_caps (mp1videoparse->sinkpad, gst_pad_get_padtemplate_caps (mp1videoparse->sinkpad));
  gst_element_add_pad(GST_ELEMENT(mp1videoparse),mp1videoparse->sinkpad);
  gst_pad_set_chain_function(mp1videoparse->sinkpad,gst_mp1videoparse_chain);

  mp1videoparse->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_pad_set_caps (mp1videoparse->srcpad, gst_pad_get_padtemplate_caps (mp1videoparse->srcpad));
  gst_element_add_pad(GST_ELEMENT(mp1videoparse),mp1videoparse->srcpad);

  mp1videoparse->partialbuf = NULL;
  mp1videoparse->need_resync = FALSE;
  mp1videoparse->last_pts = 0;
  mp1videoparse->picture_in_buffer = 0;
}

static gboolean
mp1videoparse_valid_sync (gulong head)
{
  if (head == SEQ_START_CODE)
    return TRUE;
  if (head == GOP_START_CODE)
    return TRUE;
  if (head == PICTURE_START_CODE)
    return TRUE;
  if (head >= SLICE_MIN_START_CODE &&
      head <= SLICE_MAX_START_CODE)
    return TRUE;
  if (head == USER_START_CODE)
    return TRUE;
  if (head == EXT_START_CODE)
    return TRUE;

  return FALSE;
}

static gint
mp1videoparse_find_next_gop (Mp1VideoParse *mp1videoparse, GstBuffer *buf)
{
  guchar *data = GST_BUFFER_DATA(buf);
  gulong size = GST_BUFFER_SIZE(buf);
  gulong offset = 0;
  gint sync_zeros = 0;
  gboolean have_sync = FALSE;

  while (offset < size) {
    guchar byte = *(data+offset);
    offset++;
    if (byte == 0) {
      sync_zeros++;
    }
    else if (byte == 1 && sync_zeros >=2 ) {
      sync_zeros = 0;
      have_sync = TRUE;
    }
    else if (have_sync) {
      if (byte == (SEQ_START_CODE & 0xff) || byte == (GOP_START_CODE & 0xff)) return offset-4;
      else {
        sync_zeros = 0;
	have_sync = FALSE;
      }
    }
    else {
      sync_zeros = 0;
    }
  }

  return -1;
}

static void
gst_mp1videoparse_flush (Mp1VideoParse *mp1videoparse)
{
  GST_DEBUG (0,"mp1videoparse: flushing\n");
  if (mp1videoparse->partialbuf) {
    gst_buffer_unref(mp1videoparse->partialbuf);
    mp1videoparse->partialbuf= NULL;
  }
  mp1videoparse->need_resync = TRUE;
  mp1videoparse->in_flush = TRUE;
  mp1videoparse->picture_in_buffer = 0;
}

static void
gst_mp1videoparse_chain (GstPad *pad,GstBuffer *buf)
{
  Mp1VideoParse *mp1videoparse;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  mp1videoparse = GST_MP1VIDEOPARSE (GST_OBJECT_PARENT (pad));

  gst_mp1videoparse_real_chain(mp1videoparse, buf, mp1videoparse->srcpad);
}

static void
gst_mp1videoparse_real_chain (Mp1VideoParse *mp1videoparse, GstBuffer *buf, GstPad *outpad)
{
  guchar *data;
  gulong size, offset = 0;
  GstBuffer *outbuf;
  gint sync_state;
  gboolean have_sync;
  guchar sync_byte;
  gulong head;
  gint sync_pos;
  guint64 time_stamp;
  GstBuffer *temp;

//  g_return_if_fail(GST_IS_BUFFER(buf));


  time_stamp = GST_BUFFER_TIMESTAMP(buf);


  if (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLUSH)) {
    gst_mp1videoparse_flush(mp1videoparse);
  }

  if (mp1videoparse->partialbuf) {
    offset = GST_BUFFER_SIZE(mp1videoparse->partialbuf);
    mp1videoparse->partialbuf = gst_buffer_append(mp1videoparse->partialbuf, buf);
    gst_buffer_unref(buf);
  }
  else {
    mp1videoparse->partialbuf = buf;
    offset = 0;
  }

  data = GST_BUFFER_DATA(mp1videoparse->partialbuf);
  size = GST_BUFFER_SIZE(mp1videoparse->partialbuf);

  GST_DEBUG (0,"mp1videoparse: received buffer of %ld bytes %lld\n",size, GST_BUFFER_TIMESTAMP(buf));

  head = GULONG_FROM_BE(*((gulong *)data));

  GST_DEBUG (0,"mp1videoparse: head is %08lx\n", head);

  if (!mp1videoparse_valid_sync(head) || mp1videoparse->need_resync) {
    sync_pos = mp1videoparse_find_next_gop(mp1videoparse, mp1videoparse->partialbuf);
    if (sync_pos != -1) {
      mp1videoparse->need_resync = FALSE;
      GST_DEBUG (0,"mp1videoparse: found new gop at %d\n", sync_pos);

      if (sync_pos != 0) {
        temp = gst_buffer_create_sub(mp1videoparse->partialbuf, sync_pos, size-sync_pos);
	g_assert(temp != NULL);
	gst_buffer_unref(mp1videoparse->partialbuf);
	mp1videoparse->partialbuf = temp;
        data = GST_BUFFER_DATA(mp1videoparse->partialbuf);
        size = GST_BUFFER_SIZE(mp1videoparse->partialbuf);
	offset = 0;
      }
    }
    else {
      GST_DEBUG (0,"mp1videoparse: could not sync\n");
      gst_buffer_unref(mp1videoparse->partialbuf);
      mp1videoparse->partialbuf = NULL;
      return;
    }
  }

  if (mp1videoparse->picture_in_buffer == 1) {
    mp1videoparse->last_pts = time_stamp;
  }

  sync_state = 0;
  have_sync = FALSE;

  GST_DEBUG (0,"mp1videoparse: searching sync\n");

  while (offset < size-1) {
    sync_byte = *(data + offset);
    //printf(" %d %02x\n", offset, sync_byte);
    if (sync_byte == 0) {
      sync_state++;
    }
    else if ((sync_byte == 1) && (sync_state >=2)) {
      GST_DEBUG (0,"mp1videoparse: code 0x000001%02x\n",data[offset+1]);
      if (data[offset+1] == (PICTURE_START_CODE & 0xff)) {
	mp1videoparse->picture_in_buffer++;
	if (mp1videoparse->picture_in_buffer == 1) {
          mp1videoparse->last_pts = time_stamp;
	  sync_state = 0;
	}
	else if (mp1videoparse->picture_in_buffer == 2) {
          have_sync = TRUE;
          break;
	}
	else {
          GST_DEBUG (0,"mp1videoparse: %d in buffer\n", mp1videoparse->picture_in_buffer);
          g_assert_not_reached();
	}
      }
      else sync_state = 0;
    }
    // something else...
    else sync_state = 0;
    // go down the buffer
    offset++;
  }

  if (have_sync) {
    offset -= 2;
    GST_DEBUG (0,"mp1videoparse: synced at %ld code 0x000001%02x\n",offset,data[offset+3]);

    outbuf = gst_buffer_create_sub(mp1videoparse->partialbuf, 0, offset+4);
    g_assert(outbuf != NULL);
    GST_BUFFER_TIMESTAMP(outbuf) = mp1videoparse->last_pts;

    if (mp1videoparse->in_flush) {
      GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLUSH);
      mp1videoparse->in_flush = FALSE;
    }

    GST_DEBUG (0,"mp1videoparse: pushing  %d bytes %llu\n", GST_BUFFER_SIZE(outbuf), GST_BUFFER_TIMESTAMP(outbuf));
    gst_pad_push(outpad, outbuf);
    GST_DEBUG (0,"mp1videoparse: pushing  done\n");
    mp1videoparse->picture_in_buffer = 0;

    temp = gst_buffer_create_sub(mp1videoparse->partialbuf, offset, size-offset);
    gst_buffer_unref(mp1videoparse->partialbuf);
    mp1videoparse->partialbuf = temp;
  }
  else {
    mp1videoparse->last_pts = time_stamp;
  }
}

/* FIXME
static GstElementStateReturn 
gst_mp1videoparse_change_state (GstElement *element) 
{
  Mp1VideoParse *mp1videoparse;
  g_return_val_if_fail(GST_IS_MP1VIDEOPARSE(element),GST_STATE_FAILURE);

  mp1videoparse = GST_MP1VIDEOPARSE(element);
  GST_DEBUG (0,"mp1videoparse: state pending %d\n", GST_STATE_PENDING(element));

  // if going down into NULL state, clear out buffers
  if (GST_STATE_PENDING(element) == GST_STATE_READY) {
    gst_mp1videoparse_flush(mp1videoparse);
  }

  // if we haven't failed already, give the parent class a chance to ;-)
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}
*/

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the mp1videoparse element */
  factory = gst_elementfactory_new("mp1videoparse",GST_TYPE_MP1VIDEOPARSE,
                                   &mp1videoparse_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  src_template = src_factory ();
  gst_elementfactory_add_padtemplate (factory, src_template);

  sink_template = sink_factory ();
  gst_elementfactory_add_padtemplate (factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp1videoparse",
  plugin_init
};
