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


static void	gst_monkeyenc_class_init	(GstMonkeyEncClass *klass);
static void	gst_monkeyenc_init		    (GstMonkeyEnc *monkeyenc);
static void gst_monkeyenc_loop        (GstElement *element);

static GstElementStateReturn
		gst_monkeyenc_change_state        (GstElement *element);

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

/*
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

  monkeyenc->linked = TRUE;

  return GST_PAD_LINK_OK;
}*/

static void
gst_monkeyenc_init (GstMonkeyEnc * monkeyenc)
{
  monkeyenc->sinkpad = gst_pad_new_from_template (monkeyenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (monkeyenc), monkeyenc->sinkpad);

  monkeyenc->srcpad = gst_pad_new_from_template (monkeyenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (monkeyenc), monkeyenc->srcpad);

  /*gst_pad_set_link_function (monkeyenc->sinkpad, gst_monkeyenc_sinklink);*/

  gst_element_set_loop_function (GST_ELEMENT (monkeyenc), gst_monkeyenc_loop);

  monkeyenc->linked = FALSE;
}


static void
gst_monkeyenc_loop (GstElement *element)
{
  GstMonkeyEnc *monkeyenc;
  gint retval, size;
 
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MONKEYENC (element));
	
  monkeyenc = GST_MONKEYENC (element);
	
  if (!GST_PAD_IS_USABLE (monkeyenc->srcpad))
    return;

  if (monkeyenc->init)
  {    
    unsigned char *wav_header;

    monkeyenc->src_io = new srcpad_CIO;
    monkeyenc->src_io->Open("");
    monkeyenc->src_io->srcpad = monkeyenc->srcpad; 

    monkeyenc->sink_io = new sinkpad_CIO;
    monkeyenc->sink_io->sinkpad = monkeyenc->sinkpad;
    monkeyenc->sink_io->bs = gst_bytestream_new (monkeyenc->sinkpad);
    if (monkeyenc->sink_io->bs == NULL)
    {
      gst_element_error(element, "Failed to initiliaze bytestream from sinkpad");
      return;
    }  

    monkeyenc->inputsrc = new CWAVInputSource (monkeyenc->sink_io, &(monkeyenc->waveformatex), &(monkeyenc->total_blocks), 
                                               &(monkeyenc->header_size), &(monkeyenc->terminating), &retval); 

    monkeyenc->audiobytes = monkeyenc->total_blocks * monkeyenc->waveformatex.nBlockAlign;
    monkeyenc->audiobytesleft = monkeyenc->audiobytes;

    wav_header = (unsigned char*) g_malloc0 (monkeyenc->header_size);
    retval = monkeyenc->inputsrc->GetHeaderData (wav_header);
    
    monkeyenc->compress_engine = CreateIAPECompress (&retval);

    if (monkeyenc->compress_engine == NULL)
    {
      gst_element_error(GST_ELEMENT (monkeyenc), "Failed to initiliaze MonkeyAudio encoder engine : %d", retval);
      return;
    }

    retval = monkeyenc->compress_engine->StartEx (monkeyenc->src_io, &(monkeyenc->waveformatex), monkeyenc->audiobytes,
                                         COMPRESSION_LEVEL_NORMAL, wav_header, monkeyenc->header_size);
    monkeyenc->init = FALSE;
  }

  retval = monkeyenc->compress_engine->AddDataFromInputSource (monkeyenc->inputsrc, monkeyenc->audiobytesleft, &size);
  monkeyenc->audiobytesleft -= size;

  if (monkeyenc->audiobytesleft <= 0)
  {
     unsigned char *terminating_data;

    terminating_data = (unsigned char *) g_malloc0 (monkeyenc->terminating);
    retval = monkeyenc->inputsrc->GetTerminatingData (terminating_data);

    retval = monkeyenc->compress_engine->Finish (terminating_data, monkeyenc->terminating, monkeyenc->terminating);

    monkeyenc->sink_io->SetEOF();
    monkeyenc->src_io->SetEOF();
    monkeyenc->sink_io->Close();
    gst_element_set_eos (element);
  }
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
      break;
    case GST_STATE_PAUSED_TO_READY:    
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}
