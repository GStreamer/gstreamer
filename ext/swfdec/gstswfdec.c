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

#include "gstswfdec.h"
#include <string.h>

/* elementfactory information */
static GstElementDetails gst_swfdec_details = {
  "SWF video decoder",
  "Codec/Video/Decoder",
  "LGPL",
  "Uses libswfdec to decode Flash video streams",
  VERSION,
  "David Schleef <ds@schleef.org>",
  "(C) 2002",
};

/* Swfdec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FRAME_RATE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (video_template_factory,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "swfdec_src",
    "video/raw",
      "format",    GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
      "width",     GST_PROPS_INT_RANGE (16, 4096),
      "height",    GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_PAD_TEMPLATE_FACTORY (audio_template_factory,
  "audio_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "swfdec_audiosrc",
    "audio/raw",
      "format",		GST_PROPS_STRING("int"),
      "law",		GST_PROPS_INT(0),
      "endianness",	GST_PROPS_INT(G_BYTE_ORDER),
      "signed",		GST_PROPS_BOOLEAN(TRUE),
      "width",		GST_PROPS_INT(16),
      "depth",		GST_PROPS_INT(16),
      "rate",		GST_PROPS_INT(44100),
      "channels",	GST_PROPS_INT(1)
  )
);

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "swfdec_sink",
    "application/x-shockwave-flash",
      "format",		GST_PROPS_STRING("SWF")
  )
);

static void	gst_swfdec_class_init		(GstSwfdecClass *klass);
static void	gst_swfdec_init		(GstSwfdec *swfdec);

static void	gst_swfdec_dispose		(GObject *object);

static void	gst_swfdec_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_swfdec_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);
#if 0
static GstPad *
gst_swfdec_request_new_pad (GstElement *element, GstPadTemplate *templ,
	const gchar *template);
#endif

#if 0
static gboolean gst_swfdec_src_event       	(GstPad *pad, GstEvent *event);
static gboolean gst_swfdec_src_query 		(GstPad *pad, GstPadQueryType type,
		       				 GstFormat *format, gint64 *value);

static gboolean gst_swfdec_convert_sink 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		         			 GstFormat *dest_format, gint64 *dest_value);
static gboolean gst_swfdec_convert_src 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		        	 		 GstFormat *dest_format, gint64 *dest_value);
#endif

static GstElementStateReturn
		gst_swfdec_change_state	(GstElement *element);

#if 0
static void	gst_swfdec_chain		(GstPad *pad, GstBuffer *buffer);
#endif

static GstElementClass *parent_class = NULL;
/*static guint gst_swfdec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_swfdec_get_type (void)
{
  static GType swfdec_type = 0;

  if (!swfdec_type) {
    static const GTypeInfo swfdec_info = {
      sizeof(GstSwfdecClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_swfdec_class_init,
      NULL,
      NULL,
      sizeof(GstSwfdec),
      0,
      (GInstanceInitFunc)gst_swfdec_init,
    };
    swfdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSwfdec", &swfdec_info, 0);
  }
  return swfdec_type;
}

static void
gst_swfdec_class_init(GstSwfdecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_RATE,
    g_param_spec_float ("frame_rate","frame_rate","frame_rate",
                        0.0, 1000.0, 0.0, G_PARAM_READABLE)); 
  
  gobject_class->set_property 	= gst_swfdec_set_property;
  gobject_class->get_property 	= gst_swfdec_get_property;
  gobject_class->dispose 	= gst_swfdec_dispose;

  gstelement_class->change_state = gst_swfdec_change_state;
  //gstelement_class->request_new_pad = gst_swfdec_request_new_pad;
}

#if 0
static void
gst_swfdec_vo_frame_draw (vo_frame_t * frame)
{
  gst_swfdec_vo_instance_t *_instance;
  gst_swfdec_vo_frame_t *_frame;
  GstSwfdec *swfdec;
  gint64 pts = -1;

  g_return_if_fail (frame != NULL);
  g_return_if_fail (((gst_swfdec_vo_frame_t *)frame)->buffer != NULL);

  _frame = (gst_swfdec_vo_frame_t *)frame;
  _instance = (gst_swfdec_vo_instance_t *)frame->instance;

  swfdec = GST_SWFDEC (_instance->swfdec);


  /* we have to be carefull here. we do swf_close in the READY state
   * but it can send a few frames still. We have to make sure we are playing
   * when we send frames. we do have to free those last frames though */
  if (GST_STATE (GST_ELEMENT (swfdec)) != GST_STATE_PLAYING) {
    gst_buffer_unref (_frame->buffer);
    /* pretend we have sent the frame */
    _frame->sent = TRUE;
    return;
  }

  if (swfdec->frame_rate_code != swfdec->decoder->frame_rate_code)
  {
    swfdec->frame_rate_code = swfdec->decoder->frame_rate_code;

    g_object_notify (G_OBJECT (swfdec), "frame_rate");
  }

  pts = swfdec->next_time - 3 * (GST_SECOND / video_rates[swfdec->decoder->frame_rate_code]);

  GST_BUFFER_TIMESTAMP (_frame->buffer) = pts;

  GST_DEBUG (0, "out: %lld %d %lld", GST_BUFFER_TIMESTAMP (_frame->buffer),
		  swfdec->decoder->frame_rate_code,
                  (long long)(GST_SECOND / video_rates[swfdec->decoder->frame_rate_code]));

  swfdec->next_time += (GST_SECOND / video_rates[swfdec->decoder->frame_rate_code]) + swfdec->adjust;

  GST_BUFFER_FLAG_SET (_frame->buffer, GST_BUFFER_READONLY);
  swfdec->frames_per_PTS++;
  swfdec->first = FALSE;
  _frame->sent = TRUE;
  swfdec->total_frames++;
  gst_pad_push (swfdec->videopad, _frame->buffer);
}
#endif

#if 0
static int
gst_swfdec_setup (GstSwfdec *swfdec, int width, int height)
{
  g_return_val_if_fail (swfdec != NULL, -1);

  GST_INFO (GST_CAT_PLUGIN_INFO, "VO: setup w=%d h=%d", width, height);

  swfdec->width = width;
  swfdec->height = height;
  swfdec->total_frames = 0;

  gst_pad_try_set_caps (swfdec->videopad, 
		    gst_caps_new (
		      "swfdec_caps",
		      "video/raw",
		      gst_props_new (
			"format",   GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
			  "width",  GST_PROPS_INT (width),
			  "height", GST_PROPS_INT (height),
			  NULL)));


  return 0;
}
#endif


#if 0
static void
gst_swfdec_close (GstSwfdec *swfdec)
{
  GST_INFO (GST_CAT_PLUGIN_INFO, "VO: close");

  /* FIXME */
}
#endif

#if 0
static vo_frame_t *
gst_swfdec_vo_get_frame (vo_instance_t * instance, int flags)
{
  gst_swfdec_vo_instance_t * _instance;
  gst_swfdec_vo_frame_t *frame;
  size_t size0;
  uint8_t *data = NULL;
  GstSwfdec *swfdec;

  g_return_val_if_fail (instance != NULL, NULL);

  GST_INFO (GST_CAT_PLUGIN_INFO, "VO: get_frame");

  _instance = (gst_swfdec_vo_instance_t *)instance;

  swfdec = _instance->swfdec;
  
  if (flags & VO_PREDICTION_FLAG) {
    _instance->prediction_index ^= 1;
    frame = &_instance->frames[_instance->prediction_index];
  } else {
    frame = &_instance->frames[2];
  }

  /* we are reusing this frame */
  if (frame->buffer != NULL) {
    /* if the frame wasn't sent, we have to unref twice */
    if (!frame->sent)
      gst_buffer_unref (frame->buffer);
    gst_buffer_unref (frame->buffer);
    frame->buffer = NULL;
  }

  size0 = swfdec->width * swfdec->height / 4;

  if (swfdec->pool) {
    frame->buffer = gst_buffer_new_from_pool (swfdec->pool, 0, 0);
  } else {
    size_t size = 6 * size0;
    size_t offset;
    GstBuffer *parent;

    parent = gst_buffer_new ();

    GST_BUFFER_SIZE(parent) = size + 0x10;
    GST_BUFFER_DATA(parent) = data = g_new(uint8_t, size + 0x10);

    offset = 0x10 - (((unsigned long)data) & 0xf);
    frame->buffer = gst_buffer_create_sub(parent, offset, size);

    gst_buffer_unref(parent);
  }
  data = GST_BUFFER_DATA(frame->buffer);

  /* need ref=2						*/
  /* 1 - unref when reusing this frame			*/
  /* 2 - unref when other elements done with buffer	*/
  gst_buffer_ref (frame->buffer);

  frame->vo.base[0] = data;
  frame->vo.base[1] = data + 4 * size0;
  frame->vo.base[2] = data + 5 * size0;
  /*printf("base[0]=%p\n", frame->vo.base[0]); */
  frame->sent = FALSE;

  return (vo_frame_t *)frame;
}
#endif

#if 0
static void
gst_swfdec_vo_open (GstSwfdec *swfdec)
{
  gst_swfdec_vo_instance_t * instance;
  gint i,j;

  GST_INFO (GST_CAT_PLUGIN_INFO, "VO: open");

  instance = g_new (gst_swfdec_vo_instance_t, 1);
  
  instance->vo.setup = gst_swfdec_vo_setup;
  instance->vo.close = gst_swfdec_vo_close;
  instance->vo.get_frame = gst_swfdec_vo_get_frame;
  instance->swfdec = swfdec;

  for (i=0; i<NUM_FRAMES; i++) {
    for (j=0; j<3; j++) {
      instance->frames[j].vo.base[j] = NULL;
    }
    instance->frames[i].vo.copy = NULL;
    instance->frames[i].vo.field = NULL;
    instance->frames[i].vo.draw = gst_swfdec_vo_frame_draw;
    instance->frames[i].vo.instance = (vo_instance_t *)instance;
    instance->frames[i].buffer = NULL;
  }

  swfdec->vo = (vo_instance_t *) instance;
}
#endif

#if 0
static void
gst_swfdec_vo_destroy (GstSwfdec *swfdec)
{
  gst_swfdec_vo_instance_t * instance;
  gint i;

  GST_INFO (GST_CAT_PLUGIN_INFO, "VO: destroy");

  instance = (gst_swfdec_vo_instance_t *) swfdec->vo;
  
  for (i=0; i<NUM_FRAMES; i++) {
    if (instance->frames[i].buffer) {
      if (!instance->frames[i].sent) {
        gst_buffer_unref (instance->frames[i].buffer);
      }
      gst_buffer_unref (instance->frames[i].buffer);
    } 
  }

  g_free (instance);
  swfdec->vo = NULL;
}
#endif

#if 0
static GstPadConnectReturn
gst_swfdec_connect(GstPad *pad, GstCaps *caps)
{
	return GST_PAD_CONNECT_DELAYED;
}
#endif

#if 0
static void
src_disconnected(GstPad *srcpad, GstPad *sinkpad, GstSwfdec *plugin)
{
	GST_DEBUG(GST_CAT_PADS, "removing pad %s:%s",
		GST_DEBUG_PAD_NAME(srcpad));
	
	gst_element_remove_pad(GST_ELEMENT(plugin), srcpad);

	if(plugin->videopad == srcpad) plugin->videopad = NULL;
	if(plugin->audiopad == srcpad) plugin->audiopad = NULL;
}
#endif

#if 0
static GstPad *
gst_swfdec_request_new_pad (GstElement *element, GstPadTemplate *templ,
	const gchar *template)
{
	gchar *name;
	GstPad *srcpad;
	GstSwfdec *plugin;

	plugin = GST_SWFDEC(element);

	g_return_val_if_fail(plugin != NULL, NULL);
	g_return_val_if_fail(GST_IS_SWFDEC(plugin), NULL);

	if(templ->direction != GST_PAD_SRC){
		g_warning("swfdec: request new pad that is not SRC pad.\n");
		return NULL;
	}

//printf("requesting pad %s %d\n",template,templ->name);
#if 0
	if(strcmp("audio", template) == 0){
		g_print("swfdec adding pad audio_00\n");

		srcpad = gst_pad_new_from_template(templ, "audio_00");
		gst_element_add_pad(GST_ELEMENT(plugin), srcpad);

		g_signal_connect(G_OBJECT(srcpad), "disconnected",
			G_CALLBACK(src_disconnected), plugin);
		gst_pad_set_connect_function(srcpad, gst_swfdec_connect);
		plugin->audiopad = srcpad;
	}else if(strcmp("video", template) == 0){
#endif
	if(1){
		g_print("swfdec adding pad video_00\n");

		srcpad = gst_pad_new_from_template(templ, "video_00");
		gst_element_add_pad(GST_ELEMENT(plugin), srcpad);

		g_signal_connect(G_OBJECT(srcpad), "disconnected",
			G_CALLBACK(src_disconnected), plugin);
		gst_pad_set_connect_function(srcpad, gst_swfdec_connect);
		plugin->videopad = srcpad;
	}else{
		g_warning("swfdec: request new pad with bad template\n");
		return NULL;
	}

	return srcpad;
}
#endif



static void
gst_swfdec_loop(GstElement *element)
{
	GstSwfdec *swfdec;
	GstBuffer *buf = NULL;
	int ret;

	g_return_if_fail(element != NULL);
	g_return_if_fail(GST_IS_SWFDEC(element));

	swfdec = GST_SWFDEC(element);

	if(!swfdec->videopad){
printf("creating videopad\n");
		swfdec->videopad =
			gst_pad_new_from_template(
				GST_PAD_TEMPLATE_GET(video_template_factory),
				"video_00");
printf("videopad=%p\n",swfdec->videopad);
		swfdec->audiopad =
			gst_pad_new_from_template(
				GST_PAD_TEMPLATE_GET(audio_template_factory),
				"audio_00");

printf("setting caps\n");
#if 0
		gst_pad_try_set_caps(swfdec->videopad,
			gst_pad_get_pad_template_caps(swfdec->videopad));
#endif

  gst_pad_try_set_caps (swfdec->videopad, 
		    gst_caps_new (
		      "swfdec_caps",
		      "video/raw",
		      gst_props_new (
			"format",   GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
			  "width",  GST_PROPS_INT (640),
			  "height", GST_PROPS_INT (480),
			  NULL)));

  gst_pad_try_set_caps (swfdec->audiopad, 
		    gst_caps_new (
		      "swfdec_caps",
		      "audio/raw",
			gst_props_new(
			"format", GST_PROPS_STRING("int"),
			"law", GST_PROPS_INT(0),
			"endianness", GST_PROPS_INT(G_BYTE_ORDER),
			"signed", GST_PROPS_BOOLEAN(TRUE),
			"width", GST_PROPS_INT(16),
			"depth", GST_PROPS_INT(16),
		  	"rate", GST_PROPS_INT (44100),
			"channels", GST_PROPS_INT (1),
			NULL)));

printf("adding pad\n");
		gst_element_add_pad(element, swfdec->videopad);
		gst_element_add_pad(element, swfdec->audiopad);
	}

	ret = swf_parse(swfdec->state);
	if(ret==SWF_NEEDBITS){
		buf = gst_pad_pull(swfdec->sinkpad);
		if(GST_IS_EVENT(buf)){
			switch (GST_EVENT_TYPE (buf)) {
			case GST_EVENT_EOS:
				printf("got eos\n");
				break;
			default:
				printf("got event\n");
				break;
			}

		}else{
			if(!GST_BUFFER_DATA(buf)){
				printf("expected non-null buffer\n");
			}
			ret = swf_addbits(swfdec->state,GST_BUFFER_DATA(buf),
				GST_BUFFER_SIZE(buf));
		}
	}

	if(ret==SWF_CHANGE){
		swfdec->width = swfdec->state->width;
		swfdec->height = swfdec->state->height;
		swfdec->interval = GST_SECOND / swfdec->state->rate;
#if G_BYTE_ORDER == 4321
#define RED_MASK 0xff0000
#define GREEN_MASK 0x00ff00
#define BLUE_MASK 0x0000ff
#else
#define RED_MASK 0x0000ff
#define GREEN_MASK 0x00ff00
#define BLUE_MASK 0xff0000
#endif
#if 1
		gst_pad_try_set_caps(swfdec->videopad,
			gst_caps_new(
				"swfdec_caps",
				"video/raw",
				gst_props_new(
				"format", GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
				"width", GST_PROPS_INT(swfdec->width),
				"height", GST_PROPS_INT(swfdec->height),
			  	"bpp", GST_PROPS_INT (24),
				  "depth", GST_PROPS_INT (24),
				  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
				  "red_mask", GST_PROPS_INT (RED_MASK),
				  "green_mask", GST_PROPS_INT (GREEN_MASK),
				  "blue_mask", GST_PROPS_INT (BLUE_MASK),
				NULL)));
#else
		gst_pad_try_set_caps(swfdec->videopad,
			gst_caps_new(
				"swfdec_caps",
				"video/raw",
				gst_props_new(
				"format", GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
				"width", GST_PROPS_INT(swfdec->width),
				"height", GST_PROPS_INT(swfdec->height),
			  	"bpp", GST_PROPS_INT (16),
				  "depth", GST_PROPS_INT (16),
				  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
				NULL)));
#endif
		gst_pad_try_set_caps(swfdec->audiopad,
			gst_caps_new(
				"swfdec_caps",
				"audio/raw",
				gst_props_new(
				"format", GST_PROPS_STRING("int"),
				"law", GST_PROPS_INT(0),
				"endianness", GST_PROPS_INT(G_BYTE_ORDER),
				"signed", GST_PROPS_BOOLEAN(TRUE),
				"width", GST_PROPS_INT(16),
				"depth", GST_PROPS_INT(16),
			  	"rate", GST_PROPS_INT (44100),
				"channels", GST_PROPS_INT (1),
				NULL)));
		return;
	}

	if(ret==SWF_IMAGE){
		GstBuffer *newbuf = NULL;
		int newsize = swfdec->state->width * swfdec->state->height * 3;

		/* video stuff */
		if(swfdec->pool){
			newbuf = gst_buffer_new_from_pool(swfdec->pool, 0, 0);
		}
		if(!newbuf){
			newbuf = gst_buffer_new();
			GST_BUFFER_SIZE(newbuf) = newsize;
			GST_BUFFER_DATA(newbuf) = g_malloc(newsize);
		}
		g_return_if_fail(GST_BUFFER_DATA(newbuf) != NULL);

		memcpy(GST_BUFFER_DATA(newbuf),swfdec->state->buffer,newsize);

		swfdec->timestamp += swfdec->interval;
		GST_BUFFER_TIMESTAMP(newbuf) = swfdec->timestamp;

		gst_pad_push(swfdec->videopad, newbuf);

		/* audio stuff */
		newbuf = gst_buffer_new();
		newsize = 2*44100.0/swfdec->state->rate;
		GST_BUFFER_SIZE(newbuf) = newsize;
		GST_BUFFER_DATA(newbuf) = g_malloc(newsize);
		memcpy(GST_BUFFER_DATA(newbuf),swfdec->state->sound_buffer,
				newsize);
		GST_BUFFER_TIMESTAMP(newbuf) = swfdec->timestamp;

		gst_pad_push(swfdec->audiopad, newbuf);
	}

	if(ret==SWF_EOF){
		gst_pad_push(swfdec->videopad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
		gst_pad_push(swfdec->audiopad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
	}
}

static void
gst_swfdec_init (GstSwfdec *swfdec)
{
  /* create the sink and src pads */
  swfdec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->sinkpad);
  //gst_pad_set_chain_function (swfdec->sinkpad, gst_swfdec_chain);
  //gst_pad_set_convert_function (swfdec->sinkpad, gst_swfdec_convert_sink);

#if 0
  swfdec->videopad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (video_template_factory), "video_00");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->videopad);
#endif
  //gst_pad_set_event_function (swfdec->videopad, GST_DEBUG_FUNCPTR (gst_swfdec_src_event));
  //gst_pad_set_query_function (swfdec->videopad, GST_DEBUG_FUNCPTR (gst_swfdec_src_query));
  //gst_pad_set_convert_function (swfdec->videopad, gst_swfdec_convert_src);
  
#if 0
  swfdec->audiopad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (audio_template_factory), "audio_00");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->audiopad);
#endif
  
  gst_element_set_loop_function(GST_ELEMENT(swfdec), gst_swfdec_loop);

  /* initialize the swfdec decoder state */
  swfdec->state = swf_init();
  g_return_if_fail(swfdec->state != NULL);

  swfdec->state->colorspace = SWF_COLORSPACE_RGB888;

  GST_FLAG_SET (GST_ELEMENT (swfdec), GST_ELEMENT_EVENT_AWARE);
}

static void
gst_swfdec_dispose (GObject *object)
{
  //GstSwfdec *swfdec = GST_SWFDEC (object);

  /* FIXME */
  //swf_state_free(swfdec->state);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#if 0
static void
gst_swfdec_chain (GstPad *pad, GstBuffer *buf)
{
  GstSwfdec *swfdec = GST_SWFDEC (gst_pad_get_parent (pad));
  guint32 size;
  guchar *data;
  gint ret;
  gint64 pts;

  GST_DEBUG (0, "SWFDEC: chain called");

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
	//gint64 value = GST_EVENT_DISCONT_OFFSET (event, 0).value;
	//swfdec->decoder->is_sequence_needed = 1;
	GST_DEBUG (GST_CAT_EVENT, "swfdec: discont\n"); 
        swfdec->first = TRUE;
        swfdec->timestamp = 0;
        gst_pad_event_default (pad, event);
	return;
      }
      case GST_EVENT_EOS:
        if (!swfdec->closed) {
          /* close flushes the last few frames */
          //swf_close (swfdec->state); 
	  swfdec->closed = TRUE;
        }
      default:
        gst_pad_event_default (pad, event);
	return;
    }
  }

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  pts = GST_BUFFER_TIMESTAMP (buf);

  GST_DEBUG (GST_CAT_CLOCK, "swfdec: pts %llu\n", pts);

  swfdec->timestamp += swfdec->interval;

/* fprintf(stderr, "SWFDEC: in timestamp=%llu\n",GST_BUFFER_TIMESTAMP(buf)); */
/* fprintf(stderr, "SWFDEC: have buffer of %d bytes\n",size);		*/
  ret = swf_addbits(swfdec->state, data, size);

  if(ret==SWF_IMAGE){

  }

/*fprintf(stderr, "SWFDEC: decoded %d frames\n", num_frames);*/

  gst_buffer_unref(buf);
}
#endif

#if 0
static gboolean
gst_swfdec_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value,
		           GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
	      
  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}
#endif

#if 0
static gboolean
gst_swfdec_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value,
		          GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
	      
  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * 6 * (swfdec->width * swfdec->height >> 2) *  
		  video_rates[swfdec->decoder->frame_rate_code] / GST_SECOND;
	  break;
        case GST_FORMAT_UNITS:
	  *dest_value = src_value * video_rates[swfdec->decoder->frame_rate_code] / GST_SECOND;
	  break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  if (video_rates[swfdec->decoder->frame_rate_code] != 0.0) {
	    *dest_value = src_value * GST_SECOND /
	      video_rates[swfdec->decoder->frame_rate_code];
	  }
	  else
	    res = FALSE;
	  break;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * 6 * (swfdec->width * swfdec->height >> 2);
	  break;
        case GST_FORMAT_UNITS:
	  *dest_value = src_value;
	  break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}
#endif

#if 0
static gboolean 
gst_swfdec_src_query (GstPad *pad, GstPadQueryType type,
		        GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
  static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };
#define MAX_SEEK_FORMATS 1 /* we can only do time seeking for now */
  gint i;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
        case GST_FORMAT_BYTES:
        case GST_FORMAT_UNITS:
	{
          res = FALSE;

          for (i = 0; i < MAX_SEEK_FORMATS && !res; i++) {
	    GstFormat peer_format;
	    gint64 peer_value;
		  
	    peer_format = formats[i];
	  
            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (swfdec->sinkpad), GST_PAD_QUERY_TOTAL,
			       &peer_format, &peer_value)) 
	    {
              GstFormat conv_format;

              /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
              res = gst_pad_convert (swfdec->sinkpad,
                              peer_format, peer_value,
                              &conv_format, value);
              /* and to final format */
              res &= gst_pad_convert (pad,
                         GST_FORMAT_TIME, *value,
                         format, value);
            }
	  }
          break;
	}
        default:
	  res = FALSE;
          break;
      }
      break;
    }
    case GST_PAD_QUERY_POSITION:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        default:
          res = gst_pad_convert (pad,
                          GST_FORMAT_TIME, swfdec->next_time,
                          format, value);
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}
#endif

#if 0
static gboolean 
gst_swfdec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;
  static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };
#define MAX_SEEK_FORMATS 1 /* we can only do time seeking for now */
  gint i;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gint64 src_offset;
      gboolean flush;
      GstFormat format;
			                
      format = GST_FORMAT_TIME;

      /* first bring the src_format to TIME */
      if (!gst_pad_convert (pad,
                GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
                &format, &src_offset))
      {
        /* didn't work, probably unsupported seek format then */
        res = FALSE;
        break;
      }

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

      /* assume the worst */
      res = FALSE;

      /* while we did not exhaust our seek formats without result */
      for (i = 0; i < MAX_SEEK_FORMATS && !res; i++) {
        gint64 desired_offset;

        format = formats[i];

        /* try to convert requested format to one we can seek with on the sinkpad */
        if (gst_pad_convert (swfdec->sinkpad, GST_FORMAT_TIME, src_offset, &format, &desired_offset))
        {
          GstEvent *seek_event;

          /* conversion succeeded, create the seek */
          seek_event = gst_event_new_seek (formats[i] | GST_SEEK_METHOD_SET | flush, desired_offset);
          /* do the seekk */
          if (gst_pad_send_event (GST_PAD_PEER (swfdec->sinkpad), seek_event)) {
            /* seek worked, we're done, loop will exit */
            res = TRUE;
          }
        }
        /* at this point, either the seek worked or res == FALSE */
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  return res;
}
#endif

static GstElementStateReturn
gst_swfdec_change_state (GstElement *element)
{
  GstSwfdec *swfdec = GST_SWFDEC (element);

  switch (GST_STATE_TRANSITION (element)) { 
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      //gst_swfdec_vo_open (swfdec);
      //swf_init (swfdec->decoder, swfdec->accel, swfdec->vo);

      //swfdec->decoder->is_sequence_needed = 1;
      //swfdec->decoder->frame_rate_code = 0;
      swfdec->timestamp = 0;
      swfdec->pool = NULL;
      swfdec->closed = FALSE;

      /* reset the initial video state */
      swfdec->format = -1;
      swfdec->width = -1;
      swfdec->height = -1;
      swfdec->first = TRUE;
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      /* try to get a bufferpool */
#if 0
      swfdec->pool = gst_pad_get_bufferpool (swfdec->videopad);
      if (swfdec->pool)
        GST_INFO (GST_CAT_PLUGIN_INFO, "got pool %p", swfdec->pool);
#endif
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* need to clear things we get from other plugins, since we could be reconnected */
      if (swfdec->pool) {
	gst_buffer_pool_unref (swfdec->pool);
	swfdec->pool = NULL;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* if we are not closed by an EOS event do so now, this cen send a few frames but
       * we are prepared to not really send them (see above) */
      if (!swfdec->closed) {
        /*swf_close (swfdec->decoder); */
	swfdec->closed = TRUE;
      }
      //gst_swfdec_vo_destroy (swfdec);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_swfdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSwfdec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  src = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_swfdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSwfdec *swfdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  swfdec = GST_SWFDEC (object);

  switch (prop_id) {
    case ARG_FRAME_RATE:
      g_value_set_float (value, swfdec->frame_rate);
      break;
    default:
      break;
  }
}

static GstCaps *
swf_type_find(GstBuffer *buf, gpointer private)
{
	gchar *data = GST_BUFFER_DATA(buf);

	if((data[0] != 'F' && data[0] != 'C') ||
	    data[1] != 'W' || data[2] != 'S')return NULL;

	return gst_caps_new("swf_type_find","application/x-shockwave-flash",
		NULL);
}

static GstTypeDefinition swftype_definition = 
	{ "swfdecode/x-shockwave-flash", "application/x-shockwave-flash",
		".swf .swfl", swf_type_find };

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the swfdec element */
  factory = gst_element_factory_new("swfdec",GST_TYPE_SWFDEC,
                                   &gst_swfdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (video_template_factory));
  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (audio_template_factory));
  gst_element_factory_add_pad_template (factory,
		  GST_PAD_TEMPLATE_GET (sink_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_type_factory_new(&swftype_definition);
  gst_plugin_add_feature(plugin, GST_PLUGIN_FEATURE(type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "swfdec",
  plugin_init
};
