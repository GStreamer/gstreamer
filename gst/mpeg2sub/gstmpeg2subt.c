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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstmpeg2subt.h>

static void	gst_mpeg2subt_class_init	(GstMpeg2SubtClass *klass);
static void	gst_mpeg2subt_base_init		(GstMpeg2SubtClass *klass);
static void	gst_mpeg2subt_init		(GstMpeg2Subt *mpeg2subt);

static void	gst_mpeg2subt_chain_video	(GstPad *pad,GstData *_data);
static void	gst_mpeg2subt_chain_subtitle	(GstPad *pad,GstData *_data);

static void	gst_mpeg2subt_merge_title	(GstMpeg2Subt *mpeg2subt, GstBuffer *buf);

static void	gst_mpeg2subt_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_mpeg2subt_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* elementfactory information */
static GstElementDetails mpeg2subt_details = {
  "MPEG2 subtitle Decoder",
  "Codec/Decoder/Video",
  "Decodes and merges MPEG2 subtitles into a video frame",
  "Wim Taymans <wim.taymans@chello.be>"
};

/* GstMpeg2Subt signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SKIP,
  /* FILL ME */
};

static guchar yuv_color[16] = {
  0x99,
  0x00,
  0xFF,
  0x00,
  0x40,
  0x50,
  0x60,
  0x70,
  0x80,
  0x90,
  0xA0,
  0xB0,
  0xC0,
  0xD0,
  0xE0,
  0xF0
};




static GstElementClass *parent_class = NULL;
/*static guint gst_mpeg2subt_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_mpeg2subt_get_type (void)
{
  static GType mpeg2subt_type = 0;

  if (!mpeg2subt_type) {
    static const GTypeInfo mpeg2subt_info = {
      sizeof(GstMpeg2SubtClass),
      (GBaseInitFunc)gst_mpeg2subt_base_init,
      NULL,
      (GClassInitFunc)gst_mpeg2subt_class_init,
      NULL,
      NULL,
      sizeof(GstMpeg2Subt),
      0,
      (GInstanceInitFunc)gst_mpeg2subt_init,
    };
    mpeg2subt_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMpeg2Subt", &mpeg2subt_info, 0);
  }
  return mpeg2subt_type;
}

static void
gst_mpeg2subt_base_init (GstMpeg2SubtClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &mpeg2subt_details);
}

static void
gst_mpeg2subt_class_init (GstMpeg2SubtClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SKIP,
    g_param_spec_int("skip","skip","skip",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mpeg2subt_set_property;
  gobject_class->get_property = gst_mpeg2subt_get_property;

}

static void
gst_mpeg2subt_init (GstMpeg2Subt *mpeg2subt)
{
  mpeg2subt->videopad = gst_pad_new("video",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(mpeg2subt),mpeg2subt->videopad);
  gst_pad_set_chain_function(mpeg2subt->videopad,gst_mpeg2subt_chain_video);

  mpeg2subt->subtitlepad = gst_pad_new("subtitle",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(mpeg2subt),mpeg2subt->subtitlepad);
  gst_pad_set_chain_function(mpeg2subt->subtitlepad,gst_mpeg2subt_chain_subtitle);

  mpeg2subt->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(mpeg2subt),mpeg2subt->srcpad);

  mpeg2subt->partialbuf = NULL;
  mpeg2subt->have_title = FALSE;
}

static void
gst_mpeg2subt_chain_video (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMpeg2Subt *mpeg2subt;
  guchar *data;
  glong size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  mpeg2subt = GST_MPEG2SUBT (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if (mpeg2subt->have_title && mpeg2subt->duration != 0) {
    gst_mpeg2subt_merge_title(mpeg2subt, buf);
    mpeg2subt->duration--;
  }

  gst_pad_push(mpeg2subt->srcpad, GST_DATA (buf));
}


static void
gst_mpeg2subt_parse_header (GstMpeg2Subt *mpeg2subt)
{
  guchar *buffer = GST_BUFFER_DATA(mpeg2subt->partialbuf);
  guchar dummy;
  guint i;

  i = mpeg2subt->data_size + 4;
  while (i < mpeg2subt->packet_size)
  {
    dummy = buffer [i];
    switch (dummy)
    {
      case 0x01: /* null packet ? */
        i++;
        break;
      case 0x02: /* 02 ff (ff) is the end of the packet */
        i = mpeg2subt->packet_size;
        break;
      case 0x03: /* palette */
        mpeg2subt->color[0] = yuv_color[buffer [i+1] >> 4];
        mpeg2subt->color[1] = yuv_color[buffer [i+1] & 0xf];
        mpeg2subt->color[2] = yuv_color[buffer [i+2] >> 4];
        mpeg2subt->color[3] = yuv_color[buffer [i+2] & 0xf];
        mpeg2subt->color[4] = yuv_color[0xf];
	GST_DEBUG ("mpeg2subt: colors %d %d %d %d", mpeg2subt->color[0],mpeg2subt->color[1],mpeg2subt->color[2],mpeg2subt->color[3]);
        i += 3;
        break;
      case 0x04: /* transparency palette */
        mpeg2subt->trans[3] = buffer [i+1] >> 4;
	mpeg2subt->trans[2] = buffer [i+1] & 0xf;
	mpeg2subt->trans[1] = buffer [i+2] >> 4;
	mpeg2subt->trans[0] = buffer [i+2] & 0xf;
	GST_DEBUG ("mpeg2subt: transparency %d %d %d %d", mpeg2subt->trans[0],mpeg2subt->trans[1],mpeg2subt->trans[2],mpeg2subt->trans[3]);
	i += 3;
	break;
      case 0x05: /* image coordinates */
        mpeg2subt->width = 1 + ( ((buffer[i+2] & 0x0f) << 8) + buffer[i+3] )
                  - ( (((unsigned int)buffer[i+1]) << 4) + (buffer[i+2] >> 4) );
        mpeg2subt->height = 1 + ( ((buffer[i+5] & 0x0f) << 8) + buffer[i+6] )
                   - ( (((unsigned int)buffer[i+4]) << 4) + (buffer[i+5] >> 4) );
        i += 7;
	break;
      case 0x06: /* image 1 / image 2 offsets */
        mpeg2subt->offset[0] = (((unsigned int)buffer[i+1]) << 8) + buffer[i+2];
        mpeg2subt->offset[1] = (((unsigned int)buffer[i+3]) << 8) + buffer[i+4];
	i += 5;
	break;
      case 0xff: /* "ff xx yy zz uu" with 'zz uu' == start of control packet
		  *  xx and yy are the end time in 90th/sec
	          */
	mpeg2subt->duration = (((buffer[i+1] << 8) + buffer[i+2]) * 25)/90;

	GST_DEBUG ("duration %d", mpeg2subt->duration);

	if ( (buffer[i+3] != buffer[mpeg2subt->data_size+2])
	     || (buffer[i+4] != buffer[mpeg2subt->data_size+3]) )
	{
	  g_print("mpeg2subt: invalid control header (%.2x%.2x != %.2x%.2x) !\n",
		buffer[i+3], buffer[i+4], buffer[mpeg2subt->data_size+2], buffer[mpeg2subt->data_size+3] );
/* FIXME */
/*          exit(1); */
	}
	i += 5;
	break;
      default:
	g_print("mpeg2subt: invalid sequence in control header (%.2x) !\n", dummy);
	break;
    }
  }
}

static int
get_nibble (guchar *buffer, gint *offset, gint id, gint *aligned)
{
  static int next;

  if (*aligned)
  {
    next = buffer[offset[id]];
    offset[id]++;

    *aligned = 0;
    return next >> 4;
  }
  else
  {
    *aligned = 1;
    return next & 0xf;
  }
}

static void
gst_mpeg2subt_merge_title (GstMpeg2Subt *mpeg2subt, GstBuffer *buf)
{
  gint x=0, y=0;
  gint width = mpeg2subt->width;
  gint height = mpeg2subt->height;
  guchar *buffer = GST_BUFFER_DATA(mpeg2subt->partialbuf);
  guchar *target = GST_BUFFER_DATA(buf);
  gint id=0, aligned=1;
  gint offset[2];

  offset[0] = mpeg2subt->offset[0];
  offset[1] = mpeg2subt->offset[1];
#define get_nibble() get_nibble (buffer, offset, id, &aligned)

  GST_DEBUG ("mpeg2subt: merging subtitle");

  while ((offset[1] < mpeg2subt->data_size + 2) && (y < height))
  {
    gint code;
    gint length, colorid;

    code = get_nibble();
    if (code >= 0x4)	/* 4 .. f */
    {
found_code:
      length = code >> 2;
      colorid = code & 3;
      while (length--)
        if (x++ < width) {
	  if (mpeg2subt->trans[colorid] != 0x0) {
            *target++ = mpeg2subt->color[colorid];
	  }
          else target++;
	}

      if (x >= width)
      {
        if (!aligned)
          get_nibble ();
        goto next_line;
      }
      continue;
    }

    code = (code << 4) + get_nibble();
    if (code >= 0x10)	/* 1x .. 3x */
      goto found_code;

    code = (code << 4) + get_nibble();
    if (code >= 0x40)	/* 04x .. 0fx */
      goto found_code;

    code = (code << 4) + get_nibble();
    if (code >= 0x100)	/* 01xx .. 03xx */
      goto found_code;

    /* 00xx - should only happen for 00 00 */
    if (!aligned)
      code = (code << 4) + get_nibble(); /* 0 0x xx */

    if (code)
    {
      g_print("mpeg2subt: got unknown code 00%x (offset %x side %x, x=%d, y=%d)\n", code, mpeg2subt->offset[id], id, x, y);
      goto next_line;
    }
next_line:
    /* aligned 00 00 */
    if (y < height) {
      target+=(width-x);
      x = 0;
      y++;
      id = 1 - id;
    }
  }
}

static void
gst_mpeg2subt_chain_subtitle (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMpeg2Subt *mpeg2subt;
  guchar *data;
  glong size = 0;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  mpeg2subt = GST_MPEG2SUBT (GST_OBJECT_PARENT (pad));

  if (mpeg2subt->have_title) {
    gst_buffer_unref(mpeg2subt->partialbuf);
    mpeg2subt->partialbuf = NULL;
    mpeg2subt->have_title = FALSE;
  }

  GST_DEBUG ("presentation time %" G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP(buf));

  /* deal with partial frame from previous buffer */
  if (mpeg2subt->partialbuf) {

    mpeg2subt->partialbuf = gst_buffer_merge(mpeg2subt->partialbuf, buf);;
    /* and the one we received.. */
    gst_buffer_unref(buf);
  }
  else {
    mpeg2subt->partialbuf = buf;
  }

  data = GST_BUFFER_DATA(mpeg2subt->partialbuf);
  size = GST_BUFFER_SIZE(mpeg2subt->partialbuf);

  mpeg2subt->packet_size = GUINT16_FROM_BE(*(guint16 *)data);

  if (mpeg2subt->packet_size == size) {

    GST_DEBUG ("mpeg2subt: subtitle packet size %d, current size %ld", mpeg2subt->packet_size, size);

    mpeg2subt->data_size = GUINT16_FROM_BE(*(guint16 *)(data+2));

    gst_mpeg2subt_parse_header(mpeg2subt);
    mpeg2subt->have_title = TRUE;
  }
}

static void
gst_mpeg2subt_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMpeg2Subt *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MPEG2SUBT(object));
  src = GST_MPEG2SUBT(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mpeg2subt_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMpeg2Subt *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MPEG2SUBT(object));
  src = GST_MPEG2SUBT(object);

  switch (prop_id) {
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register(plugin, "mpeg2subt",
			      GST_RANK_NONE, GST_TYPE_MPEG2SUBT);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpeg2sub",
  "MPEG-2 video subtitle parser",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
