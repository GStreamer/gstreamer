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

#include "gstmonkeydec.h"


GstElementDetails gst_monkeydec_details = {
  "MonkeyAudio decoder",
  "Codec/Audio/Decoder",
  "Free to use",
  "Decode MonkeyAudio audio stream (.ape)",
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
  ARG_0,
	ARG_METADATA,
};

static void	gst_monkeydec_class_init      (GstMonkeyDecClass *klass);
static void	gst_monkeydec_init		        (GstMonkeyDec *monkeydec);
static void gst_monkeydec_loop            (GstElement *element);
static gboolean
            gst_monkeydec_src_event       (GstPad *pad, GstEvent *event);
static gboolean
            gst_monkeydec_src_query       (GstPad *pad, GstQueryType type,
		                                       GstFormat *format, gint64 *value);
static const GstQueryType*
            gst_monkeydec_get_query_types (GstPad *pad);
static const GstFormat*
            gst_monkeydec_get_formats     (GstPad *pad);
static GstElementStateReturn
        		gst_monkeydec_change_state    (GstElement *element);
static void gst_monkeydec_get_property 		(GObject *object, guint prop_id, 
																					 GValue *value, 
						 															 GParamSpec *pspec);
static void gst_monkeydec_set_property 		(GObject *object, guint prop_id, 
						 															 const GValue *value, 
						 															 GParamSpec *pspec);


static GstElementClass *parent_class = NULL;

GType gst_monkeydec_get_type (void)
{
  static GType monkeydec_type = 0;

  if (!monkeydec_type) {
    static const GTypeInfo monkeydec_info = {
      sizeof (GstMonkeyDecClass), NULL,
      NULL,
      (GClassInitFunc) gst_monkeydec_class_init,
      NULL,
      NULL,
      sizeof (GstMonkeyDec),
      0,
      (GInstanceInitFunc) gst_monkeydec_init,
    };

    monkeydec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstMonkeyDec", &monkeydec_info, (GTypeFlags)0);
  }
  return monkeydec_type;
}


static void
gst_monkeydec_class_init (GstMonkeyDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = gst_monkeydec_change_state;
	gobject_class->get_property = gst_monkeydec_get_property;
  gobject_class->set_property = gst_monkeydec_set_property;

  parent_class = GST_ELEMENT_CLASS( g_type_class_ref(GST_TYPE_ELEMENT));
	
	g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "(logical) Stream metadata",
                         GST_TYPE_CAPS, G_PARAM_READABLE));
}


static void
gst_monkeydec_init (GstMonkeyDec * monkeydec)
{
  monkeydec->sinkpad = gst_pad_new_from_template (monkeydec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (monkeydec), monkeydec->sinkpad);

  monkeydec->srcpad = gst_pad_new_from_template (monkeydec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (monkeydec), monkeydec->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (monkeydec), gst_monkeydec_loop);

  gst_pad_set_event_function (monkeydec->srcpad, (GstPadEventFunction)(gst_monkeydec_src_event));
  gst_pad_set_query_function (monkeydec->srcpad, gst_monkeydec_src_query);
  gst_pad_set_query_type_function (monkeydec->srcpad, (GstPadQueryTypeFunction)(gst_monkeydec_get_query_types));
  gst_pad_set_formats_function (monkeydec->srcpad, (GstPadFormatsFunction)(gst_monkeydec_get_formats));
	
	monkeydec->metadata = NULL;
}


static const GstFormat*
gst_monkeydec_get_formats (GstPad *pad)
{
  static const GstFormat src_formats[] = {
/*    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  static const GstFormat sink_formats[] = {
    /*GST_FORMAT_BYTES,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}


static const GstQueryType*
gst_monkeydec_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_monkeydec_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    (GstQueryType)0
  };
  
  return gst_monkeydec_src_query_types;
}


static gboolean
gst_monkeydec_src_query (GstPad *pad, GstQueryType type,
		                   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstMonkeyDec *monkeydec;

  monkeydec = GST_MONKEYDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_TIME:
            *value = monkeydec->decomp->GetInfo (APE_DECOMPRESS_LENGTH_MS) * 1000000LL;
            break;
        default:
            res = FALSE;
            break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
         default:
           *value = monkeydec->decomp->GetInfo (APE_DECOMPRESS_CURRENT_MS) * 1000000LL;  
           break;
      }
    default:
      break;
  }

  return res;
}    


static gboolean
gst_monkeydec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstMonkeyDec *monkeydec; 

  monkeydec = GST_MONKEYDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gboolean flush;
      GstFormat format;

      format = GST_FORMAT_TIME;

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

      monkeydec->seek_to = GST_EVENT_SEEK_OFFSET (event) * monkeydec->frequency / GST_SECOND;
      monkeydec->io->need_discont = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }
  
  gst_event_unref (event);

  return res;
}


static gboolean
gst_monkeydec_update_metadata (GstMonkeyDec *monkeydec)
{
  GstProps *props = NULL;
  GstPropsEntry *entry;
  gchar *name, *value;
  CAPETagField *field;
  CAPETag *tag;
  BOOL ret;

  /* clear old one */
  if (monkeydec->metadata) {
    gst_caps_unref (monkeydec->metadata);
    monkeydec->metadata = NULL;
  }
	
  g_return_val_if_fail (monkeydec, FALSE);

  tag = new CAPETag (monkeydec->io, TRUE);
  g_return_val_if_fail (tag, FALSE);

  if (!tag->GetHasAPETag ())
    return FALSE;

  ret = tag->GetNextTagField (TRUE, &field);
  g_return_val_if_fail (ret, FALSE);
	
  /* create props to hold the key/value pairs */
  props = gst_props_empty_new ();

  while (ret)
  { 		
    if (field->GetFieldValueSize() != 0)
    {
      name = g_strndup (field->GetFieldName(), field->GetFieldSize());
      value = g_strndup (field->GetFieldValue(), field->GetFieldValueSize());
		
      entry = gst_props_entry_new (name, GST_PROPS_STRING_TYPE, value);
      gst_props_add_entry (props, (GstPropsEntry *) entry);
		
      g_print ("%s : %s\n", name, value); 
    }
				
    ret = tag->GetNextTagField (FALSE, &field);
  }
	
  monkeydec->metadata = gst_caps_new ("monkeydec_metadata", "application/x-gst-metadata",
                                      props);

  g_object_notify (G_OBJECT (monkeydec), "metadata");
	
  delete tag;
  return TRUE;
}



static void
gst_monkeydec_loop (GstElement *element)
{
  GstMonkeyDec *monkeydec;
  gint blocks_retrieved;
  GstBuffer *buffer_out;
  GstFormat format;
  gint64 timestamp;
  gint retval;
	
	
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MONKEYDEC (element));
	
  monkeydec = GST_MONKEYDEC (element);

  if (!GST_PAD_IS_USABLE (monkeydec->srcpad))
    return;

  if (monkeydec->init)
  {
    monkeydec->io = new sinkpad_CIO;
    monkeydec->io->bs = gst_bytestream_new (monkeydec->sinkpad);
    if (monkeydec->io->bs == NULL)
    {
      gst_element_error(element, "Failed to initiliaze bytestream from sinkpad");
      return;
    }    

    monkeydec->io->sinkpad = monkeydec->sinkpad;
    monkeydec->io->Open ("");
		
		gst_monkeydec_update_metadata (monkeydec);
		/*monkeydec->tag = new CAPETag (monkeydec->io, TRUE);
		if (monkeydec->tag->GetHasAPETag ())
			g_print ("Yes APE\n");
  	else
			g_print ("No APE\n");
		
	  if (monkeydec->tag->GetHasID3Tag ())
			g_print ("Yes Id3\n");
  	else
			g_print ("No Id3\n");
	
		if (monkeydec->tag->GetNextTagField(TRUE, &field))
			g_print ("Champs : %s %.*s\n", field->GetFieldName(), field->GetFieldValueSize(), field->GetFieldValue());
*/			
    monkeydec->decomp = CreateIAPEDecompressEx (monkeydec->io, &retval);
    if (monkeydec->decomp == NULL)
    {
      gst_element_error(element, "Failed to initiliaze MonkeyAudio decoder engine : %d", retval);
      return;
    }

    monkeydec->channels = monkeydec->decomp->GetInfo (APE_INFO_CHANNELS);
    monkeydec->frequency = monkeydec->decomp->GetInfo (APE_INFO_SAMPLE_RATE);
    monkeydec->depth = monkeydec->decomp->GetInfo (APE_INFO_BITS_PER_SAMPLE);
						
    monkeydec->io->eos = FALSE;

    monkeydec->total_samples = 0;
    monkeydec->io->need_discont = FALSE;
    monkeydec->seek_to = 0;

    monkeydec->init = FALSE;
  }

  buffer_out = gst_buffer_new_and_alloc (1024 * monkeydec->decomp->GetInfo(APE_INFO_BLOCK_ALIGN));

  if (monkeydec->seek_to != 0)
  {
    monkeydec->decomp->Seek (monkeydec->seek_to);
    monkeydec->seek_to = 0;
  }
	
  monkeydec->decomp->GetData((char *) GST_BUFFER_DATA (buffer_out), 1024, &blocks_retrieved);

  format = GST_FORMAT_TIME;
  gst_monkeydec_src_query (monkeydec->srcpad, GST_QUERY_POSITION, &format, &timestamp);
	
	GST_BUFFER_SIZE (buffer_out) = blocks_retrieved * monkeydec->decomp->GetInfo(APE_INFO_BLOCK_ALIGN);
  GST_BUFFER_TIMESTAMP (buffer_out) = timestamp;

  if (monkeydec->io->need_discont) 
  {
    monkeydec->io->need_discont = FALSE;

    /* if the pad is not usable, don't push it out */
    if (GST_PAD_IS_USABLE (monkeydec->srcpad))
    {
      GstEvent *discont;

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, timestamp, NULL); 

      gst_pad_push (monkeydec->srcpad, GST_BUFFER (discont));
    }
  }

  if (!GST_PAD_CAPS (monkeydec->srcpad)) {
    gst_pad_try_set_caps (monkeydec->srcpad,
		    GST_CAPS_NEW (
		      "monkeydec_caps",
		      "audio/raw",
		      "format", 	GST_PROPS_STRING ("int"),
          "law",         GST_PROPS_INT (0),
          "endianness",  GST_PROPS_INT (G_LITTLE_ENDIAN),
          "signed",      GST_PROPS_BOOLEAN (TRUE),
          "width",       GST_PROPS_INT (monkeydec->depth),
          "depth",       GST_PROPS_INT (monkeydec->depth),
          "rate",     	GST_PROPS_INT (monkeydec->frequency),
          "channels", 	GST_PROPS_INT (monkeydec->channels)
		    ));
		  }

  gst_pad_push (monkeydec->srcpad, buffer_out);

  if (monkeydec->io->eos)
  {
    GstEvent *event;

    event = gst_event_new (GST_EVENT_EOS);
    gst_pad_push (monkeydec->srcpad, GST_BUFFER (event));
    gst_element_set_eos (element);
  }
}



static GstElementStateReturn
gst_monkeydec_change_state (GstElement *element)
{
  GstMonkeyDec *monkeydec;

  monkeydec = GST_MONKEYDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      monkeydec->init = TRUE;      
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

static void
gst_monkeydec_set_property (GObject *object, guint prop_id, 
		             const GValue *value, GParamSpec *pspec)
{
  GstMonkeyDec *monkeydec;
	      
  g_return_if_fail (GST_IS_MONKEYDEC (object));

  monkeydec = GST_MONKEYDEC (object);

  switch (prop_id) {
    default:
      g_warning ("Unknown property id\n");
  }
}

static void 
gst_monkeydec_get_property (GObject *object, guint prop_id, 
		             GValue *value, GParamSpec *pspec)
{
  GstMonkeyDec *monkeydec;
	      
  g_return_if_fail (GST_IS_MONKEYDEC (object));

  monkeydec = GST_MONKEYDEC (object);

  switch (prop_id) {
    case ARG_METADATA:
      g_value_set_boxed (value, monkeydec->metadata);
      break;
   /* case ARG_STREAMINFO:
      g_value_set_boxed (value, monkeydec->streaminfo);
      break;*/
    default:
      g_warning ("Unknown property id\n");
  }
}
