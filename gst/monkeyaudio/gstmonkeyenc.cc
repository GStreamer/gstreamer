/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>

#include "gstmonkeyenc.h"

GstElementDetails gst_monkeyenc_details = {
  "MonkeyAudio encoder",
  "Codec/Audio/Encoder",
  "Free to use",
  "Encode to MonkeyAudio audio stream (.ape)",
   VERSION,
  "Jeremy SIMON <jsimon13@yahoo.fr>",
  "(C) 2002 Matthew T. Ashland",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};


static void	gst_monkeyenc_class_init	 (GstMonkeyEncClass *klass);
static void	gst_monkeyenc_init		     (GstMonkeyEnc *monkeyenc);
static void	gst_monkeyenc_chain		     (GstPad *pad, GstBuffer *buffer);
static void gst_monkeyenc_handle_event (GstPad *pad, GstBuffer *buffer);

static GstPadLinkReturn
    gst_monkeyenc_sinklink             (GstPad *pad, GstCaps *caps);
static GstElementStateReturn
		gst_monkeyenc_change_state         (GstElement *element);

static GstElementClass *parent_class = NULL;

GType gst_monkeyenc_get_type (void)
{
  static GType monkeyenc_type = 0;

  if (!monkeyenc_type) {
    static const GTypeInfo monkeyenc_info = {
      sizeof (GstMonkeyEncClass), NULL,
      NULL,
      (GClassInitFunc) gst_monkeyenc_class_init,
      NULL,
      NULL,
      sizeof (GstMonkeyEnc),
      0,
      (GInstanceInitFunc) gst_monkeyenc_init,
    };

    monkeyenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstMonkeyEnc", &monkeyenc_info, (GTypeFlags)0);
  }
  return monkeyenc_type;
}


static void
gst_monkeyenc_class_init (GstMonkeyEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = gst_monkeyenc_change_state;

  parent_class = GST_ELEMENT_CLASS( g_type_class_ref(GST_TYPE_ELEMENT));
}


static GstPadLinkReturn
gst_monkeyenc_sinklink (GstPad *pad, GstCaps *caps)
{
  GstMonkeyEnc *monkeyenc;

  monkeyenc = GST_MONKEYENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "channels", &monkeyenc->channels);
  gst_caps_get_int (caps, "rate", &monkeyenc->rate);
  gst_caps_get_int (caps, "depth", &monkeyenc->depth);

  return GST_PAD_LINK_OK;
}


static void
gst_monkeyenc_init (GstMonkeyEnc * monkeyenc)
{
  monkeyenc->sinkpad = gst_pad_new_from_template (monkeyenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (monkeyenc), monkeyenc->sinkpad);

  monkeyenc->srcpad = gst_pad_new_from_template (monkeyenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (monkeyenc), monkeyenc->srcpad);

  gst_pad_set_link_function (monkeyenc->sinkpad, gst_monkeyenc_sinklink);

  gst_pad_set_chain_function (monkeyenc->sinkpad, gst_monkeyenc_chain);
}


static void
gst_monkeyenc_handle_event (GstPad *pad, GstBuffer *buffer)
{
  GstEvent *event = GST_EVENT (buffer);

  switch (GST_EVENT_TYPE (event))
  {
    default:
      gst_pad_event_default (pad, event);
      break;
  }

  return;
}

static void
gst_monkeyenc_chain (GstPad *pad, GstBuffer *buffer)
{
  GstMonkeyEnc *monkeyenc;
  gint retval;

 
  monkeyenc = GST_MONKEYENC (gst_pad_get_parent (pad));
  g_return_if_fail (GST_IS_MONKEYENC (monkeyenc));

  /* handle events */
  if (GST_IS_EVENT (buffer))
  {
    gst_monkeyenc_handle_event (pad, buffer);
    return;
  }
  
  if (!GST_PAD_IS_USABLE (monkeyenc->srcpad))
    return;

  if (monkeyenc->init)
  {    
    monkeyenc->src_io = new srcpad_CIO;
    monkeyenc->src_io->Open("");
    monkeyenc->src_io->srcpad = monkeyenc->srcpad;

    FillWaveFormatEx(&monkeyenc->waveformatex, monkeyenc->rate, monkeyenc->depth, monkeyenc->channels);
  
    monkeyenc->compress_engine = CreateIAPECompress (&retval);

    if (monkeyenc->compress_engine == NULL)
    {
      gst_element_error(GST_ELEMENT (monkeyenc), "Failed to initiliaze MonkeyAudio encoder engine : %d", retval);
      return;
    }

    retval = monkeyenc->compress_engine->StartEx (monkeyenc->src_io, &(monkeyenc->waveformatex), MAX_AUDIO_BYTES_UNKNOWN,
                                         COMPRESSION_LEVEL_NORMAL, NULL, CREATE_WAV_HEADER_ON_DECOMPRESSION);
    
    monkeyenc->init = FALSE;
  }

  retval = monkeyenc->compress_engine->AddData ((unsigned char *)GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  monkeyenc->audiobytesleft -= GST_BUFFER_SIZE (buffer);
}


static GstElementStateReturn
gst_monkeyenc_change_state (GstElement *element)
{
  GstMonkeyEnc *monkeyenc;

  monkeyenc = GST_MONKEYENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      monkeyenc->init = TRUE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* do something to get out of the chain function faster */
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      monkeyenc->compress_engine->Finish (NULL, 0, 0);
      break;
    case GST_STATE_PAUSED_TO_READY:    
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}
