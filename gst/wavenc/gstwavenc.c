/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer
 * Copyright (C) <2002> Iain Holmes <iain@prettypeople.org>
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
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gstwavenc.h>
#include <riff.h>

static void 	gst_wavenc_base_init 	(gpointer g_class);
static void 	gst_wavenc_class_init 	(GstWavEncClass *klass);
static void 	gst_wavenc_init 	(GstWavEnc *wavenc);
static void 	gst_wavenc_chain 	(GstPad *pad, GstData *_data);

#define WAVE_FORMAT_PCM 0x0001

#define WRITE_U32(buf, x) *(buf) = (unsigned char) (x&0xff);\
*((buf)+1) = (unsigned char)((x>>8)&0xff);\
*((buf)+2) = (unsigned char)((x>>16)&0xff);\
*((buf)+3) = (unsigned char)((x>>24)&0xff);

#define WRITE_U16(buf, x) *(buf) = (unsigned char) (x&0xff);\
*((buf)+1) = (unsigned char)((x>>8)&0xff);

struct riff_struct {
  guint8 	id[4]; 		/* RIFF */
  guint32 	len;
  guint8	wav_id[4]; 	/* WAVE */
};

struct chunk_struct {
  guint8 	id[4];
  guint32  	len;
};

struct common_struct {
  guint16 	wFormatTag;
  guint16 	wChannels;
  guint32	dwSamplesPerSec;
  guint32	dwAvgBytesPerSec;
  guint16 	wBlockAlign;
  guint16 	wBitsPerSample; 	/* Only for PCM */
};

struct wave_header {
  struct riff_struct 	riff;
  struct chunk_struct 	format;
  struct common_struct 	common;
  struct chunk_struct 	data;
};

static GstElementDetails gst_wavenc_details = GST_ELEMENT_DETAILS (
  "WAV encoder",
  "Codec/Encoder/Audio",
  "Encode raw audio into WAV",
  "Iain Holmes <iain@prettypeople.org>"
);

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) LITTLE_ENDIAN, "
    "width = (int) { 8, 16 }, "
    "depth = (int) { 8, 16 }, "
    "signed = (boolean) true"
  )
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-wav")
);

enum {
	PROP_0,
};

static GstElementClass *parent_class = NULL;

static GType
gst_wavenc_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstWavEncClass), 
      gst_wavenc_base_init, 
      NULL,
      (GClassInitFunc) gst_wavenc_class_init, 
      NULL, 
      NULL,
      sizeof (GstWavEnc), 
      0, 
      (GInstanceInitFunc) gst_wavenc_init
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstWavEnc", &info, 0);
  }

  return type;
}

static GstElementStateReturn
gst_wavenc_change_state (GstElement *element)
{
  GstWavEnc *wavenc = GST_WAVENC (element);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      wavenc->setup = FALSE;
      wavenc->flush_header = TRUE;
      break;
    default:
      break;
  }

  if (parent_class->change_state) {
    return parent_class->change_state (element);
  }

  return GST_STATE_SUCCESS;
}

static void
set_property (GObject *object,
	      guint prop_id,
	      const GValue *value,
	      GParamSpec *pspec)
{
	GstWavEnc *enc;

	enc = GST_WAVENC (object);
	
	switch (prop_id) {
	default:
		break;
	}
}

static void
gst_wavenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_wavenc_details);
  
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}
static void
gst_wavenc_class_init (GstWavEncClass *klass)
{
  GstElementClass *element_class;
  GObjectClass *object_class;
	
  element_class = (GstElementClass *) klass;
  object_class = (GObjectClass *) klass;
  object_class->set_property = set_property;
	
  element_class->change_state = gst_wavenc_change_state;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static gboolean
gst_wavenc_setup (GstWavEnc *wavenc)
{
  struct wave_header wave;
  gint size = 0x7fffffff; /* Use a bogus size initially */

  wave.common.wChannels = wavenc->channels;
  wave.common.wBitsPerSample = wavenc->bits;
  wave.common.dwSamplesPerSec = wavenc->rate;

  memset (wavenc->header, 0, WAV_HEADER_LEN);

  /* Fill out our wav-header with some information */
  strncpy (wave.riff.id, "RIFF", 4);
  wave.riff.len = size - 8;
  strncpy (wave.riff.wav_id, "WAVE", 4);

  strncpy (wave.format.id, "fmt ", 4);
  wave.format.len = 16;

  wave.common.wFormatTag = WAVE_FORMAT_PCM;
  wave.common.dwAvgBytesPerSec = wave.common.wChannels * wave.common.dwSamplesPerSec * (wave.common.wBitsPerSample >> 3);
  wave.common.wBlockAlign = wave.common.wChannels * (wave.common.wBitsPerSample >> 3);

  strncpy (wave.data.id, "data", 4);
  wave.data.len = size - 44;

  strncpy (wavenc->header, wave.riff.id, 4);
  WRITE_U32 (wavenc->header + 4, wave.riff.len);
  strncpy (wavenc->header + 8, wave.riff.wav_id, 4);
  strncpy (wavenc->header + 12, wave.format.id, 4);
  WRITE_U32 (wavenc->header + 16, wave.format.len);
  WRITE_U16 (wavenc->header + 20, wave.common.wFormatTag);
  WRITE_U16 (wavenc->header + 22, wave.common.wChannels);
  WRITE_U32 (wavenc->header + 24, wave.common.dwSamplesPerSec);
  WRITE_U32 (wavenc->header + 28, wave.common.dwAvgBytesPerSec);
  WRITE_U16 (wavenc->header + 32, wave.common.wBlockAlign);
  WRITE_U16 (wavenc->header + 34, wave.common.wBitsPerSample);
  strncpy (wavenc->header + 36, wave.data.id, 4);
  WRITE_U32 (wavenc->header + 40, wave.data.len);

  wavenc->setup = TRUE;
  return TRUE;
}

static GstPadLinkReturn
gst_wavenc_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstWavEnc *wavenc;
  GstStructure *structure;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int  (structure, "channels", &wavenc->channels);
  gst_structure_get_int  (structure, "rate", &wavenc->rate);
  gst_structure_get_int  (structure, "depth", &wavenc->bits);

  gst_wavenc_setup (wavenc);

  if (wavenc->setup) {
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_wavenc_stop_file (GstWavEnc *wavenc)
{
  GstEvent *event;
  GstBuffer *outbuf;
	
  event = gst_event_new_seek (GST_FORMAT_BYTES |
															GST_SEEK_METHOD_SET, 0);
  gst_pad_send_event (GST_PAD_PEER (wavenc->srcpad), event);
  
  outbuf = gst_buffer_new_and_alloc (WAV_HEADER_LEN);
  WRITE_U32 (wavenc->header + 4, wavenc->length);
  memcpy (GST_BUFFER_DATA (outbuf), wavenc->header, WAV_HEADER_LEN);
  
  gst_pad_push (wavenc->srcpad, GST_DATA (outbuf));
}

static void
gst_wavenc_init (GstWavEnc *wavenc)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavenc);

  wavenc->sinkpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->sinkpad);
  gst_pad_set_chain_function (wavenc->sinkpad, gst_wavenc_chain);
  gst_pad_set_link_function (wavenc->sinkpad, gst_wavenc_sinkconnect);
  
  wavenc->srcpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->srcpad);

  wavenc->setup = FALSE;
  wavenc->flush_header = TRUE;
	
  GST_FLAG_SET (wavenc, GST_ELEMENT_EVENT_AWARE);
}

struct _maps {
	guint32 id;
	char *name;
} maps[] = {
	{ GST_RIFF_INFO_IARL, "Location" },
	{ GST_RIFF_INFO_IART, "Artist" },
	{ GST_RIFF_INFO_ICMS, "Commissioner" },
	{ GST_RIFF_INFO_ICMT, "Comment" },
	{ GST_RIFF_INFO_ICOP, "Copyright" },
	{ GST_RIFF_INFO_ICRD, "Creation Date" },
	{ GST_RIFF_INFO_IENG, "Engineer" },
	{ GST_RIFF_INFO_IGNR, "Genre" },
	{ GST_RIFF_INFO_IKEY, "Keywords" },
	{ GST_RIFF_INFO_INAM, "Title" }, /* Name */
	{ GST_RIFF_INFO_IPRD, "Product" },
	{ GST_RIFF_INFO_ISBJ, "Subject" },
	{ GST_RIFF_INFO_ISFT, "Software" },
	{ GST_RIFF_INFO_ITCH, "Technician" },
	{ 0, NULL }
};

#if 0
static guint32
get_id_from_name (const char *name)
{
	int i;

	for (i = 0; maps[i].name; i++) {
		if (strcasecmp (maps[i].name, name) == 0) {
			return maps[i].id;
		}
	}

	return 0;
}

static void
write_metadata (GstWavEnc *wavenc)
{
	GString *info_str;
	GList *props;
	int total = 4;
	gboolean need_to_write = FALSE;
	
	info_str = g_string_new ("LIST    INFO");

	for (props = wavenc->metadata->properties->properties; props; props = props->next) {
		GstPropsEntry *entry = props->data;
		const char *name;
		guint32 id;

		name = gst_props_entry_get_name (entry);
		id = get_id_from_name (name);
		if (id != 0) {
			const char *text;
			char *tmp;
			int len, req, i;

			need_to_write = TRUE; /* We've got at least one entry */
			
			gst_props_entry_get_string (entry, &text);
			len = strlen (text) + 1; /* The length in the file includes the \0 */

			tmp = g_strdup_printf (GST_FOURCC_FORMAT "%d%s", GST_FOURCC_ARGS (id),
														 GUINT32_TO_LE (len), text); 
			g_string_append (info_str, tmp);
			g_free (tmp);

			/* Check that we end on an even boundary */
			req = ((len + 8) + 1) & ~1;
			for (i = 0; i < req - len; i++) {
				g_string_append_printf (info_str, "%c", 0);
			}

			total += req;
		}
	}

	if (need_to_write) {
		GstBuffer *buf;
		/* Now we've got all the strings together, we can write our length in */
		info_str->str[4] = GUINT32_TO_LE (total);

		buf = gst_buffer_new ();
		gst_buffer_set_data (buf, info_str->str, info_str->len);

		gst_pad_push (wavenc->srcpad, GST_DATA (buf));
		g_string_free (info_str, FALSE);
	}
}

static void
write_cues (GstWavEnc *wavenc)
{
	GString *cue_string, *point_string;
	GstBuffer *buf;
	GList *cue_list, *c;
	int num_cues, total = 4;

	if (gst_props_get (wavenc->metadata->properties,
										 "cues", &cue_list,
										 NULL) == FALSE) {
		/* No cues, move along please, nothing to see here */
		return;
	}

	/* Space for 'cue ', chunk size and number of cuepoints */
	cue_string = g_string_new ("cue         ");
#define CUEPOINT_SIZE 24
	point_string = g_string_sized_new (CUEPOINT_SIZE);
	
	for (c = cue_list, num_cues = 0; c; c = c->next, num_cues++) {
		GstCaps *cue_caps = c->data;
		guint32 pos;
		
		gst_props_get (cue_caps->properties, "position", &pos, NULL);
		
		point_string->str[0] = GUINT32_TO_LE (num_cues + 1);
		point_string->str[4] = GUINT32_TO_LE (0);
		/* Fixme: There is probably a macro for this */
		point_string->str[8] = 'd';
		point_string->str[9] = 'a';
		point_string->str[10] = 't';
		point_string->str[11] = 'a';
		point_string->str[12] = GUINT32_TO_LE (0);
		point_string->str[16] = GUINT32_TO_LE (0);
		point_string->str[20] = GUINT32_TO_LE (pos);
		
		total += CUEPOINT_SIZE;
	}

	/* Set the length and chunk size */
	cue_string->str[4] = GUINT32_TO_LE (total);
	cue_string->str[8] = GUINT32_TO_LE (num_cues);
	/* Stick the cue points on the end */
	g_string_append (cue_string, point_string->str);
	g_string_free (point_string, TRUE);

	buf = gst_buffer_new ();
	gst_buffer_set_data (buf, cue_string->str, cue_string->len);

	gst_pad_push (wavenc->srcpad, GST_DATA (buf));
	g_string_free (cue_string, FALSE);
}

static void
write_labels (GstWavEnc *wavenc)
{
	GstBuffer *buf;
	GString *info_str;
	int total = 4;
	GList *caps;
	
	info_str = g_string_new ("LIST    adtl");
	if (gst_props_get (wavenc->metadata->properties, "ltxts", &caps, NULL)) {
		GList *p;
		int i;

		for (p = caps, i = 1; p; p = p->next, i++) {
			GstCaps *ltxt_caps = p->data;
			GString *ltxt;
			char *label = NULL;
			int len, req, j;
			
			gst_props_get (ltxt_caps->properties, "name", &label, NULL);
			len = strlen (label);

#define LTXT_SIZE 28
			ltxt = g_string_new ("ltxt                        ");
			ltxt->str[8] = GUINT32_TO_LE (i); /* Identifier */
			ltxt->str[12] = GUINT32_TO_LE (0); /* Sample Length */
			ltxt->str[16] = GUINT32_TO_LE (0); /* FIXME: Don't save the purpose yet */
			ltxt->str[20] = GUINT16_TO_LE (0); /* Country */
			ltxt->str[22] = GUINT16_TO_LE (0); /* Language */
			ltxt->str[24] = GUINT16_TO_LE (0); /* Dialect */
			ltxt->str[26] = GUINT16_TO_LE (0); /* Code Page */
			g_string_append (ltxt, label);
			g_free (label);

			len += LTXT_SIZE;

			ltxt->str[4] = GUINT32_TO_LE (len);
			
			/* Check that we end on an even boundary */
			req = ((len + 8) + 1) & ~1;
			for (j = 0; j < req - len; j++) {
				g_string_append_printf (ltxt, "%c", 0);
			}

			total += req;

			g_string_append (info_str, ltxt->str);
			g_string_free (ltxt, TRUE);
		}
	}

	if (gst_props_get (wavenc->metadata->properties, "labels", &caps, NULL)) {
		GList *p;
		int i;

		for (p = caps, i = 1; p; p = p->next, i++) {
			GstCaps *labl_caps = p->data;
			GString *labl;
			char *label = NULL;
			int len, req, j;

			gst_props_get (labl_caps->properties, "name", &label, NULL);
			len = strlen (label);

#define LABL_SIZE 4
			labl = g_string_new ("labl        ");
			labl->str[8] = GUINT32_TO_LE (i);
			g_string_append (labl, label);
			g_free (label);
			
			len += LABL_SIZE;

			labl->str[4] = GUINT32_TO_LE (len);

			/* Check our size */
			req = ((len + 8) + 1) & ~1;
			for (j = 0; j < req - len; j++) {
				g_string_append_printf (labl, "%c", 0);
			}

			total += req;

			g_string_append (info_str, labl->str);
			g_string_free (labl, TRUE);
		}
	}

	if (gst_props_get (wavenc->metadata->properties, "notes", &caps, NULL)) {
		GList *p;
		int i;

		for (p = caps, i = 1; p; p = p->next, i++) {
			GstCaps *note_caps = p->data;
			GString *note;
			char *label = NULL;
			int len, req, j;

			gst_props_get (note_caps->properties, "name", &label, NULL);
			len = strlen (label);

#define NOTE_SIZE 4
			note = g_string_new ("note        ");
			note->str[8] = GUINT32_TO_LE (i);
			g_string_append (note, label);
			g_free (label);

			len += NOTE_SIZE;

			note->str[4] = GUINT32_TO_LE (len);

			/* Size check */
			req = ((len + 8) + 1) & ~1;
			for (j = 0; j < req - len; j++) {
				g_string_append_printf (note, "%c", 0);
			}

			total += req;

			g_string_append (info_str, note->str);
			g_string_free (note, TRUE);
		}
	}

	info_str->str[4] = GUINT32_TO_LE (total);

	buf = gst_buffer_new ();
	gst_buffer_set_data (buf, info_str->str, info_str->len);

	gst_pad_push (wavenc->srcpad, GST_DATA (buf));
	g_string_free (info_str, FALSE);
}
#endif

static void
gst_wavenc_chain (GstPad *pad,
									GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstWavEnc *wavenc;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  if (!wavenc->setup) {
    gst_buffer_unref (buf);
    gst_element_error (GST_ELEMENT (wavenc), "encoder not initialised (input is not audio?)");
    return;
  }

	if (GST_IS_EVENT (buf)) {
		if (GST_EVENT_TYPE (buf) == GST_EVENT_EOS) {
			wavenc->pad_eos = TRUE;

#if 0
			/* Write our metadata if we have any */
			if (wavenc->metadata) {
				write_metadata (wavenc);
				write_cues (wavenc);
				write_labels (wavenc);
			}
#endif
			
			gst_wavenc_stop_file (wavenc);
			gst_pad_push (wavenc->srcpad,
										GST_DATA (gst_event_new (GST_EVENT_EOS)));
			gst_element_set_eos (GST_ELEMENT (wavenc));
		} else {
			gst_pad_event_default (wavenc->srcpad, GST_EVENT (buf));
		}
		return;
	}

  if (GST_PAD_IS_USABLE (wavenc->srcpad)) {
    if (wavenc->flush_header) {
      GstBuffer *outbuf;
      
      outbuf = gst_buffer_new_and_alloc (WAV_HEADER_LEN);
      memcpy (GST_BUFFER_DATA (outbuf), wavenc->header, WAV_HEADER_LEN);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
      
      gst_pad_push (wavenc->srcpad, GST_DATA (outbuf));
      wavenc->flush_header = FALSE;
    }

    wavenc->length += GST_BUFFER_SIZE (buf);
    gst_pad_push (wavenc->srcpad, GST_DATA (buf));
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "wavenc", GST_RANK_NONE, GST_TYPE_WAVENC);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "wavenc",
  "Encode raw audio into WAV",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
    
