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


#define PCM_BUFFER_SIZE		(1152*4)

/*#define DEBUG_ENABLED*/
#include <gstac3parse.h>

/* struct and table stolen from ac3dec by Aaron Holtzman */
struct frmsize_s {
  guint16 bit_rate;
  guint16 frm_size[3];
};

static struct frmsize_s frmsizecod_tbl[] = {
      { 32  ,{64   ,69   ,96   } },
      { 32  ,{64   ,70   ,96   } },
      { 40  ,{80   ,87   ,120  } },
      { 40  ,{80   ,88   ,120  } },
      { 48  ,{96   ,104  ,144  } },
      { 48  ,{96   ,105  ,144  } },
      { 56  ,{112  ,121  ,168  } },
      { 56  ,{112  ,122  ,168  } },
      { 64  ,{128  ,139  ,192  } },
      { 64  ,{128  ,140  ,192  } },
      { 80  ,{160  ,174  ,240  } },
      { 80  ,{160  ,175  ,240  } },
      { 96  ,{192  ,208  ,288  } },
      { 96  ,{192  ,209  ,288  } },
      { 112 ,{224  ,243  ,336  } },
      { 112 ,{224  ,244  ,336  } },
      { 128 ,{256  ,278  ,384  } },
      { 128 ,{256  ,279  ,384  } },
      { 160 ,{320  ,348  ,480  } },
      { 160 ,{320  ,349  ,480  } },
      { 192 ,{384  ,417  ,576  } },
      { 192 ,{384  ,418  ,576  } },
      { 224 ,{448  ,487  ,672  } },
      { 224 ,{448  ,488  ,672  } },
      { 256 ,{512  ,557  ,768  } },
      { 256 ,{512  ,558  ,768  } },
      { 320 ,{640  ,696  ,960  } },
      { 320 ,{640  ,697  ,960  } },
      { 384 ,{768  ,835  ,1152 } },
      { 384 ,{768  ,836  ,1152 } },
      { 448 ,{896  ,975  ,1344 } },
      { 448 ,{896  ,976  ,1344 } },
      { 512 ,{1024 ,1114 ,1536 } },
      { 512 ,{1024 ,1115 ,1536 } },
      { 576 ,{1152 ,1253 ,1728 } },
      { 576 ,{1152 ,1254 ,1728 } },
      { 640 ,{1280 ,1393 ,1920 } },
      { 640 ,{1280 ,1394 ,1920 } }};

/* elementfactory information */
static GstElementDetails ac3parse_details = {
  "AC3 Parser",
  "Codec/Parser",
  "Parses and frames AC3 audio streams, provides seek",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

/* GstAc3Parse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SKIP,
  /* FILL ME */
};

static GstPadTemplate*
src_factory (void) 
{
  return 
    gst_pad_template_new (
  	"src",
  	GST_PAD_SRC,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "ac3parse_src",
    	  "audio/ac3",
	  gst_props_new (
    	    "framed",   GST_PROPS_BOOLEAN (TRUE),
	    NULL)),
	NULL);
}

static GstPadTemplate*
sink_factory (void) 
{
  return 
    gst_pad_template_new (
  	"sink",
  	GST_PAD_SINK,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "ac3parse_sink",
    	  "audio/ac3",
	  gst_props_new (
    	    "framed",   GST_PROPS_BOOLEAN (FALSE),
	    NULL)),
	NULL);
}

static void	gst_ac3parse_class_init	(GstAc3ParseClass *klass);
static void	gst_ac3parse_init	(GstAc3Parse *ac3parse);

static void	gst_ac3parse_chain	(GstPad *pad,GstBuffer *buf);

static void	gst_ac3parse_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_ac3parse_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstPadTemplate *src_template, *sink_template;

static GstElementClass *parent_class = NULL;
/*static guint gst_ac3parse_signals[LAST_SIGNAL] = { 0 };*/

GType
ac3parse_get_type (void)
{
  static GType ac3parse_type = 0;

  if (!ac3parse_type) {
    static const GTypeInfo ac3parse_info = {
      sizeof(GstAc3ParseClass),      NULL,
      NULL,
      (GClassInitFunc)gst_ac3parse_class_init,
      NULL,
      NULL,
      sizeof(GstAc3Parse),
      0,
      (GInstanceInitFunc)gst_ac3parse_init,
    };
    ac3parse_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAc3Parse", &ac3parse_info, 0);
  }
  return ac3parse_type;
}

static void
gst_ac3parse_class_init (GstAc3ParseClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SKIP,
    g_param_spec_int("skip","skip","skip",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ac3parse_set_property;
  gobject_class->get_property = gst_ac3parse_get_property;

}

static void
gst_ac3parse_init (GstAc3Parse *ac3parse)
{
  ac3parse->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (ac3parse), ac3parse->sinkpad);
  gst_pad_set_chain_function (ac3parse->sinkpad, gst_ac3parse_chain);

  ac3parse->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad (GST_ELEMENT (ac3parse), ac3parse->srcpad);

  ac3parse->partialbuf = NULL;
  ac3parse->skip = 0;
}

static void
gst_ac3parse_chain (GstPad *pad, GstBuffer *buf)
{
  GstAc3Parse *ac3parse;
  guchar *data;
  glong size,offset = 0;
  unsigned short header;
  GstBuffer *outbuf = NULL;
  gint bpf;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  ac3parse = GST_AC3PARSE(GST_OBJECT_PARENT (pad));
  GST_DEBUG (0,"ac3parse: received buffer of %d bytes", GST_BUFFER_SIZE (buf));

  /* deal with partial frame from previous buffer */
  if (ac3parse->partialbuf) {

    ac3parse->partialbuf = gst_buffer_append(ac3parse->partialbuf, buf);;
    /* and the one we received.. */
    gst_buffer_unref(buf);
  }
  else {
    ac3parse->partialbuf = buf;
  }

  data = GST_BUFFER_DATA(ac3parse->partialbuf);
  size = GST_BUFFER_SIZE(ac3parse->partialbuf);

  /* while we still have bytes left -2 for the header */
  while (offset < size-2) {
    int skipped = 0;

    GST_DEBUG (0,"ac3parse: offset %ld, size %ld ",offset, size);

    /* search for a possible start byte */
    for (;((data[offset] != 0x0b) && (offset < size));offset++) skipped++ ;
    if (skipped) {
      fprintf(stderr, "ac3parse: **** now at %ld skipped %d bytes (FIXME?)\n",offset,skipped);
    }
    /* construct the header word */
    header = GUINT16_TO_BE(*((guint16 *)(data+offset)));
/*    g_print("AC3PARSE: sync word is 0x%02X\n",header); */
    /* if it's a valid header, go ahead and send off the frame */
    if (header == 0x0b77) {
      gint rate, fsize;
/*      g_print("AC3PARSE: found sync at %d\n",offset); */
      /* get the bits we're interested in */
      rate = (data[offset+4] >> 6) & 0x3;
      fsize = data[offset+4] & 0x3f;
      /* calculate the bpf of the frame */
      bpf = frmsizecod_tbl[fsize].frm_size[rate] * 2;
      /* if we don't have the whole frame... */
      if ((size - offset) < bpf) {
	GST_DEBUG (0,"ac3parse: partial buffer needed %ld < %d ",size-offset, bpf);
	break;
      } else {
	outbuf = gst_buffer_create_sub(ac3parse->partialbuf,offset,bpf);

	offset += bpf;
	if (ac3parse->skip == 0 && GST_PAD_IS_CONNECTED(ac3parse->srcpad)) {
	  GST_DEBUG (0,"ac3parse: pushing buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
          gst_pad_push(ac3parse->srcpad,outbuf);
	}
	else {
	  GST_DEBUG (0,"ac3parse: skipping buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
          gst_buffer_unref(outbuf);
	  ac3parse->skip--;
	}
      }
    } else {
      offset++;
      fprintf(stderr, "ac3parse: *** wrong header, skipping byte (FIXME?)\n");
    }
  }
  /* if we have processed this block and there are still */
  /* bytes left not in a partial block, copy them over. */
  if (size-offset > 0) {
    gint remainder = (size - offset);
    GST_DEBUG (0,"ac3parse: partial buffer needed %d for trailing bytes",remainder);

    outbuf = gst_buffer_create_sub(ac3parse->partialbuf,offset,remainder);
    gst_buffer_unref(ac3parse->partialbuf);
    ac3parse->partialbuf = outbuf;
  }
}

static void
gst_ac3parse_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAc3Parse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AC3PARSE(object));
  src = GST_AC3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_ac3parse_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAc3Parse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AC3PARSE(object));
  src = GST_AC3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the ac3parse element */
  factory = gst_element_factory_new("ac3parse",GST_TYPE_AC3PARSE,
                                   &ac3parse_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_SECONDARY);

  src_template = src_factory ();
  gst_element_factory_add_pad_template (factory, src_template);

  sink_template = sink_factory ();
  gst_element_factory_add_pad_template (factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ac3parse",
  plugin_init
};
