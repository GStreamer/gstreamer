/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2000> Daniel Fischer <dan@f3c.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <string.h>
#include "gstdv1394src.h"

#define N_BUFFERS_IN_POOL 3

#define PAL_FRAMESIZE 144000
#define NTSC_FRAMESIZE 120000

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CONSECUTIVE,
  ARG_SKIP,
  ARG_DROP_INCOMPLETE,
};

static GstElementDetails gst_dv1394src_details = GST_ELEMENT_DETAILS (
  "Firewire (1394) DV Source",
  "Source/Video",
  "Source for DV video data from firewire port",
  "Erik Walthinsen <omega@temple-baptist.com>\n"
  "Daniel Fischer <dan@f3c.com>"
);

#if 0
static GstPadTemplate*
gst_dv1394src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (
        "dv1394src",
        "video/dv",
/*
	gst_props_new (
          "format", GST_PROPS_LIST (
	  		G_TYPE_STRING ("NTSC"),
	  		G_TYPE_STRING ("PAL")
			), 
	  NULL)
	),
*/  
      NULL);
  }
  return template;
}
#endif

static void		gst_dv1394src_base_init		(gpointer g_class);
static void		gst_dv1394src_class_init		(GstDV1394SrcClass *klass);
static void		gst_dv1394src_init		(GstDV1394Src *filter);

static void		gst_dv1394src_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_dv1394src_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_dv1394src_change_state	(GstElement *element);

static GstData *	gst_dv1394src_get			(GstPad *pad);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_dv1394src_get_type(void) {
  static GType gst_dv1394src_type = 0;

  if (!gst_dv1394src_type) {
    static const GTypeInfo gst_dv1394src_info = {
      sizeof(GstDV1394Src), 
      gst_dv1394src_base_init,
      NULL,
      (GClassInitFunc)gst_dv1394src_class_init,
      NULL,
      NULL,
      sizeof(GstDV1394Src),
      0,
      (GInstanceInitFunc)gst_dv1394src_init,
    };
    gst_dv1394src_type = g_type_register_static(GST_TYPE_ELEMENT, "DV1394Src", &gst_dv1394src_info, 0);
  }
  return gst_dv1394src_type;
}

static void
gst_dv1394src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_dv1394src_details);
}

static void
gst_dv1394src_class_init (GstDV1394SrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property( G_OBJECT_CLASS(klass), ARG_CONSECUTIVE,
  	g_param_spec_int("consecutive","consecutive frames","send n consecutive frames after skipping",
			1, G_MAXINT,1,G_PARAM_READWRITE));
  g_object_class_install_property( G_OBJECT_CLASS(klass), ARG_SKIP,
  	g_param_spec_int("skip","skip frames","skip n frames",
			0, G_MAXINT,1,G_PARAM_READWRITE));
  g_object_class_install_property( G_OBJECT_CLASS(klass), ARG_DROP_INCOMPLETE,
  	g_param_spec_boolean("drop_incomplete","drop_incomplete","drop incomplete frames",
			TRUE, G_PARAM_READWRITE));

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_dv1394src_set_property;
  gobject_class->get_property = gst_dv1394src_get_property;

  gstelement_class->change_state = gst_dv1394src_change_state;
}

static void
gst_dv1394src_init (GstDV1394Src *dv1394src)
{
  dv1394src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (dv1394src->srcpad, gst_dv1394src_get);
  gst_element_add_pad (GST_ELEMENT (dv1394src), dv1394src->srcpad);

  dv1394src->card = 0;
  dv1394src->port = 0;
  dv1394src->channel = 63;
  
  dv1394src->consecutive = 1;
  dv1394src->skip = 0;
  dv1394src->drop_incomplete = TRUE;
  
  /* initialized when first header received */
  dv1394src->frameSize=0; 
 
  dv1394src->buf = NULL;
  dv1394src->frame = NULL;
  dv1394src->frameSequence = 0;
}

static void
gst_dv1394src_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDV1394Src *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DV1394SRC(object));
  filter = GST_DV1394SRC(object);

  switch (prop_id) {
    case ARG_SKIP:
    	filter->skip = g_value_get_int(value);
	break;
    case ARG_CONSECUTIVE:
    	filter->consecutive = g_value_get_int(value);
	break;
    case ARG_DROP_INCOMPLETE:
    	filter->drop_incomplete = g_value_get_boolean(value);
	break;
    default:
      break;
  }
}

static void
gst_dv1394src_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDV1394Src *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DV1394SRC(object));
  filter = GST_DV1394SRC(object);

  switch (prop_id) {
    case ARG_SKIP:
    	g_value_set_int( value, filter->skip );
        break;
    case ARG_CONSECUTIVE:
    	g_value_set_int( value, filter->consecutive );
        break;
    case ARG_DROP_INCOMPLETE:
        g_value_set_boolean( value, filter->drop_incomplete );
	break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static
int gst_dv1394src_iso_receive(raw1394handle_t handle,int channel,size_t len,quadlet_t *data) {
  GstDV1394Src *dv1394src = GST_DV1394SRC (raw1394_get_userdata(handle));

  if (len > 16) {
  	/*
		 the following code taken from kino-0.51 (Dan Dennedy/Charles Yates)
  	*/
        unsigned char *p = (unsigned char*) & data[3];
        int section_type = p[0] >> 5;           /* section type is in bits 5 - 7 */
        int dif_sequence = p[1] >> 4;           /* dif sequence number is in bits 4 - 7 */
        int dif_block = p[2];

        /* if we are at the beginning of a frame, 
          we set buf=frame, and alloc a new buffer for frame
        */

        if (section_type == 0 && dif_sequence == 0) {	// dif header
	
          if( !dv1394src->negotiated) {
            // figure format (NTSC/PAL)
            if( p[3] & 0x80 ) {
              // PAL
              dv1394src->frameSize = PAL_FRAMESIZE;
              GST_DEBUG ("PAL data");
              if (gst_pad_try_set_caps (dv1394src->srcpad, 
                    gst_caps_new_simple ("video/dv",
                      "format", G_TYPE_STRING, "PAL", NULL)) <= 0) {
		gst_element_error (dv1394src, CORE, NEGOTIATION, NULL, ("Could not set source caps for PAL"));
                return 0;
              }
            } else {
              // NTSC (untested)
              dv1394src->frameSize = NTSC_FRAMESIZE;
              GST_DEBUG ("NTSC data [untested] - please report success/failure to <dan@f3c.com>");
              if (gst_pad_try_set_caps (dv1394src->srcpad, 
                    gst_caps_new_simple ("video/dv",
                      "format", G_TYPE_STRING, "NTSC", NULL)) <= 0) {
                gst_element_error (dv1394src, CORE, NEGOTIATION, NULL, ("Could not set source caps for NTSC"));
                return 0;
              }
            }
            dv1394src->negotiated = TRUE;
          }
  
          // drop last frame when not complete
          if( !dv1394src->drop_incomplete || dv1394src->bytesInFrame == dv1394src->frameSize ) { 
            dv1394src->buf = dv1394src->frame;
          } else {
            GST_INFO_OBJECT (GST_ELEMENT(dv1394src), "incomplete frame dropped"); 
          }
          dv1394src->frame = NULL;

          dv1394src->frameSequence++;

          if( dv1394src->frameSequence % (dv1394src->skip+dv1394src->consecutive) < dv1394src->consecutive ) {
            dv1394src->frame = gst_buffer_new_and_alloc (dv1394src->frameSize);
            }
            dv1394src->bytesInFrame = 0;
          }

          if (dv1394src->frame != NULL) {
            void *data = GST_BUFFER_DATA( dv1394src->frame );


            switch (section_type) {
            case 0: /* 1 Header block */
                /* p[3] |= 0x80; // hack to force PAL data */
                memcpy(data + dif_sequence * 150 * 80, p, 480);
                break;

            case 1: /* 2 Subcode blocks */
                memcpy(data + dif_sequence * 150 * 80 + (1 + dif_block) * 80, p, 480);
                break;

            case 2: /* 3 VAUX blocks */
                memcpy(data + dif_sequence * 150 * 80 + (3 + dif_block) * 80, p, 480);
                break;

            case 3: /* 9 Audio blocks interleaved with video */
                memcpy(data + dif_sequence * 150 * 80 + (6 + dif_block * 16) * 80, p, 480);
                break;

            case 4: /* 135 Video blocks interleaved with audio */
                memcpy(data + dif_sequence * 150 * 80 + (7 + (dif_block / 15) + dif_block) * 80, p, 480);
                break;

            default: /* we can´t handle any other data */
                break;
            }
            dv1394src->bytesInFrame += 480;
        }
  }

  return 0;
}

static
int gst_dv1394src_bus_reset(raw1394handle_t handle,
		            unsigned int generation) {
  GST_INFO_OBJECT (GST_DV1394SRC(raw1394_get_userdata(handle)),"have bus reset");
  return 0;
}

static GstData *
gst_dv1394src_get (GstPad *pad)
{
  GstDV1394Src *dv1394src = GST_DV1394SRC (GST_PAD_PARENT(pad));

  dv1394src->buf = NULL;
  while (dv1394src->buf == NULL)
    raw1394_loop_iterate(dv1394src->handle);

  return GST_DATA(dv1394src->buf);
}  

static GstElementStateReturn
gst_dv1394src_change_state (GstElement *element)
{
  GstDV1394Src *dv1394src;

  g_return_val_if_fail (GST_IS_DV1394SRC (element), GST_STATE_FAILURE);
  dv1394src = GST_DV1394SRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if ((dv1394src->handle = raw1394_new_handle()) == NULL) {
        GST_INFO_OBJECT (dv1394src,"can't get raw1394 handle");
        return GST_STATE_FAILURE;
      }
      raw1394_set_userdata(dv1394src->handle,dv1394src);
      dv1394src->numcards = raw1394_get_port_info(dv1394src->handle,dv1394src->pinfo,16);
      if (dv1394src->numcards == 0) {
        GST_INFO_OBJECT (dv1394src,"no cards available for raw1394");
        return GST_STATE_FAILURE;
      }
      if (dv1394src->pinfo[dv1394src->card].nodes <= 1) {
        GST_INFO_OBJECT (dv1394src,"there are no nodes on the 1394 bus");
        return GST_STATE_FAILURE;
      }
      if (raw1394_set_port(dv1394src->handle,dv1394src->port) < 0) {
        GST_INFO_OBJECT (dv1394src,"can't set 1394 port %d",dv1394src->port);
        return GST_STATE_FAILURE;
      }
      raw1394_set_iso_handler(dv1394src->handle,dv1394src->channel,gst_dv1394src_iso_receive);
      raw1394_set_bus_reset_handler(dv1394src->handle,gst_dv1394src_bus_reset);
      dv1394src->started = FALSE;
      GST_DEBUG ("successfully opened up 1394 connection");
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (raw1394_start_iso_rcv(dv1394src->handle,dv1394src->channel) < 0) {
        GST_INFO_OBJECT (dv1394src,"can't start 1394 iso receive");
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      raw1394_stop_iso_rcv(dv1394src->handle, dv1394src->channel);
      break;
    case GST_STATE_READY_TO_NULL:
      raw1394_destroy_handle(dv1394src->handle);
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */  
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
