/* GStreamer
 * Copyright (C) 2003 Christophe Fergeau <teuf@gnome.org>
 *
 * gstflactag.c: plug-in for reading/modifying vorbis comments in flac files
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
#include <gst/gsttaginterface.h>
#include <gst/tags/gsttagediting.h>
#include <string.h>

#define GST_TYPE_FLAC_TAG (gst_flac_tag_get_type())
#define GST_FLAC_TAG(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLAC_TAG, GstFlacTag))
#define GST_FLAC_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLAC_TAG, GstFlacTag))
#define GST_IS_FLAC_TAG(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLAC_TAG))
#define GST_IS_FLAC_TAG_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLAC_TAG))

typedef struct _GstFlacTag GstFlacTag;
typedef struct _GstFlacTagClass GstFlacTagClass;

static inline 
gint min (gint a, gint b) {
  if (a < b) {
    return a;
  } else {
    return b;
  }
}
  

typedef enum {
  GST_FLAC_TAG_STATE_INIT,
  GST_FLAC_TAG_STATE_METADATA_BLOCKS,
  GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK,
  GST_FLAC_TAG_STATE_SKIP_METADATA_BLOCK,
  GST_FLAC_TAG_STATE_ADD_VORBIS_COMMENT,
  GST_FLAC_TAG_STATE_AUDIO_DATA
} GstFlacTagState;


struct _GstFlacTag {
  GstElement	 element;

  /* pads */
  GstPad *		sinkpad;
  GstPad *		srcpad;

  GstFlacTagState       state;

  GstBuffer *		buffer;

  guint                 metadata_bytes_remaining;
  gboolean              metadata_last_block;  
  gboolean              metadata_append_vc_block;
};

struct _GstFlacTagClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_flac_tag_details = GST_ELEMENT_DETAILS (
  "flac rettager",
  "Tag",
  "Rewrite tags in a FLAC file",
  "Christope Fergeau <teuf@gnome.org>"
);


/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (flac_tag_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "flac_tag_tag_src",
    "audio/x-flac",
    NULL
    )
)

GST_PAD_TEMPLATE_FACTORY (flac_tag_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "flac_tag_data_sink",
    "audio/x-flac",
    NULL
  )
)


static void		gst_flac_tag_base_init	(gpointer		g_class);
static void		gst_flac_tag_class_init	(GstFlacTagClass *	klass);
static void		gst_flac_tag_init	(GstFlacTag *		tag);

static void		gst_flac_tag_chain	(GstPad *		pad,
						 GstData *		data);

static GstElementStateReturn gst_flac_tag_change_state	(GstElement * element);


static GstElementClass *parent_class = NULL;
/* static guint gst_flac_tag_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_flac_tag_get_type (void)
{
  static GType flac_tag_type = 0;

  if (!flac_tag_type) {
    static const GTypeInfo flac_tag_info = {
      sizeof (GstFlacTagClass),
      gst_flac_tag_base_init,
      NULL,
      (GClassInitFunc) gst_flac_tag_class_init,
      NULL,
      NULL,
      sizeof (GstFlacTag),
      0,
      (GInstanceInitFunc) gst_flac_tag_init,
    };
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };
    
    flac_tag_type = g_type_register_static(GST_TYPE_ELEMENT, "GstFlacTag", &flac_tag_info, 0);

    g_type_add_interface_static (flac_tag_type, GST_TYPE_TAG_SETTER, &tag_setter_info);
    
  }
  return flac_tag_type;
}


static void
gst_flac_tag_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_flac_tag_details);

  gst_element_class_add_pad_template (element_class,
		  GST_PAD_TEMPLATE_GET (flac_tag_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
		  GST_PAD_TEMPLATE_GET (flac_tag_src_template_factory));
}


static void
gst_flac_tag_class_init (GstFlacTagClass *klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;
  
  gstelement_class = (GstElementClass*) klass;
  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_flac_tag_change_state;
}


static void
gst_flac_tag_init (GstFlacTag *tag)
{
  /* create the sink and src pads */
  tag->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (flac_tag_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (tag), tag->sinkpad);
  gst_pad_set_chain_function (tag->sinkpad, GST_DEBUG_FUNCPTR (gst_flac_tag_chain));

  tag->srcpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (flac_tag_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (tag), tag->srcpad);

  tag->buffer = NULL;
}

#define FLAC_MAGIC "fLaC"
#define FLAC_MAGIC_SIZE (sizeof (FLAC_MAGIC) - 1)

static void
gst_flac_tag_chain (GstPad *pad, GstData *data)
{
  GstBuffer *buffer;
  GstFlacTag *tag;
  
  if (GST_IS_EVENT (data)) {
    g_print ("Unhandled event\n");
    return;
  }

  buffer = GST_BUFFER (data);
  tag = GST_FLAC_TAG (gst_pad_get_parent (pad));

  if (tag->buffer) {
    tag->buffer = gst_buffer_merge (tag->buffer, buffer);
  } else {
    tag->buffer = buffer;
  }


  /* Initial state, we don't even know if we are dealing with a flac file */
  if (tag->state == GST_FLAC_TAG_STATE_INIT) {
    if (GST_BUFFER_SIZE (tag->buffer) < sizeof (FLAC_MAGIC)) {
      return;
    }

    if (strncmp (GST_BUFFER_DATA (tag->buffer), FLAC_MAGIC, FLAC_MAGIC_SIZE) == 0) {
      GstBuffer *sub;
      tag->state = GST_FLAC_TAG_STATE_METADATA_BLOCKS;
      sub = gst_buffer_create_sub (tag->buffer, 0, FLAC_MAGIC_SIZE);
	      
      gst_pad_push (tag->srcpad, GST_DATA (sub));
      sub = gst_buffer_create_sub (tag->buffer, FLAC_MAGIC_SIZE, GST_BUFFER_SIZE (tag->buffer) - FLAC_MAGIC_SIZE);
      gst_buffer_unref (tag->buffer);
      /* We do a copy because we need a writable buffer, and _create_sub
       * sets the buffer it uses to read-only
       */
      tag->buffer = gst_buffer_copy (sub);
      gst_buffer_unref (sub);
    } else {
      /* FIXME: does that work well with FLAC files containing ID3v2 tags ? */
      gst_element_error (GST_ELEMENT (tag), "Not a flac stream\n");
    }

    /* We must know if we'll add a vorbiscomment block as the last metadata
     * block as soon as possible: if we won't add a block  by ourselves,
     * we must keep the current vorbiscomment block, and don't change
     * the "last block" flag when we read it
     */
    tag->metadata_append_vc_block = (gst_tag_setter_get_list (GST_TAG_SETTER (tag)) != NULL);
  }

  /* The fLaC magic string has been skipped, try to detect the beginning
   * of a metadata block
   */
  if (tag->state == GST_FLAC_TAG_STATE_METADATA_BLOCKS) {
    guint size;
    guint type;
    gboolean is_last;

    g_assert (tag->metadata_bytes_remaining == 0);
    g_assert (tag->metadata_last_block == FALSE);

    /* The header of a flac metadata block is 4 bytes long:
     * 1st bit: indicates whether this is the last metadata info block
     * 7 next bits: 4 if vorbis comment block
     * 24 next bits: size of the metadata to follow (big endian)
     */
    if (GST_BUFFER_SIZE (tag->buffer) < 4) {
      return;
    }
    is_last = (((GST_BUFFER_DATA (tag->buffer)[0]) & 0x80) == 0x80);
    /* If we have metadata set on the element, the last metadata block 
     * will be the vorbis comment block which we will build ourselves
     */
    if ((is_last) && (tag->metadata_append_vc_block)) {
      (GST_BUFFER_DATA (tag->buffer)[0]) &= (~0x80);
    }

    type = (GST_BUFFER_DATA (tag->buffer)[0]) & 0x7F;
    size = 
      ((GST_BUFFER_DATA (tag->buffer)[1]) << 16) 
      | ((GST_BUFFER_DATA (tag->buffer)[2]) << 8)
      | (GST_BUFFER_DATA (tag->buffer)[3]);

    /* The 4 bytes long header isn't included in the metadata size */
    tag->metadata_bytes_remaining = size + 4;
    tag->metadata_last_block = is_last;

    /* Metadata blocks of type 4 are vorbis comment blocks */
    if ((type == 0x04) && (tag->metadata_append_vc_block)) {
      tag->state = GST_FLAC_TAG_STATE_SKIP_METADATA_BLOCK;
    } else {
      tag->state = GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK;
    }
  }

  /* Reads a metadata block */
  if ((tag->state == GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK) ||
  (tag->state == GST_FLAC_TAG_STATE_SKIP_METADATA_BLOCK)) {
    guint bytes_to_push;

    g_assert (tag->metadata_bytes_remaining != 0);

    bytes_to_push = min (tag->metadata_bytes_remaining, 
			 GST_BUFFER_SIZE (tag->buffer));

    if (tag->state == GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK) {
      GstBuffer *sub;
      sub = gst_buffer_create_sub (tag->buffer, 0, bytes_to_push);
      gst_pad_push (tag->srcpad, GST_DATA (sub));
    }
    tag->metadata_bytes_remaining -= (bytes_to_push);

    if (GST_BUFFER_SIZE (tag->buffer) > bytes_to_push) {
      GstBuffer *sub;

      sub = gst_buffer_create_sub (tag->buffer, bytes_to_push, 
				   GST_BUFFER_SIZE (tag->buffer) - bytes_to_push);
      gst_buffer_unref (tag->buffer);

      /* We do a copy because we need a writable buffer, and _create_sub
       * sets the buffer it uses to read-only
       */
      tag->buffer = gst_buffer_copy (sub);
      gst_buffer_unref (sub);

      if (tag->metadata_last_block == FALSE) {
	tag->state = GST_FLAC_TAG_STATE_METADATA_BLOCKS;
      } else if (tag->metadata_append_vc_block) {
	tag->state = GST_FLAC_TAG_STATE_ADD_VORBIS_COMMENT;
      } else {
	tag->state = GST_FLAC_TAG_STATE_AUDIO_DATA;
      }
    } else if (tag->metadata_bytes_remaining == 0) {
      tag->state = GST_FLAC_TAG_STATE_METADATA_BLOCKS;
      tag->buffer = NULL;
    } else {
      tag->state = GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK;
      tag->buffer = NULL;
    }
  }

  /* Creates a vorbis comment block from the metadata which was set
   * on the gstreamer element, and add it to the flac stream
   */
  if (tag->state == GST_FLAC_TAG_STATE_ADD_VORBIS_COMMENT) {
    guchar header[4];
    GstBuffer *buffer;
    gint size;
    const GstTagList *tags;

    g_assert (tag->metadata_append_vc_block);
    bzero (header, sizeof (header));
    header[0] = 0x84; /* 0x80 = Last metadata block, 
		       * 0x04 = vorbiscomment block
		       */
    tags = gst_tag_setter_get_list (GST_TAG_SETTER (tag));
    buffer = gst_tag_list_to_vorbiscomment_buffer (tags, header, 
						   sizeof (header), NULL);
    if (buffer == NULL) {
      gst_element_error (GST_ELEMENT (tag), "Error filling vorbis comments\n");
      return;
    }
    size = GST_BUFFER_SIZE (buffer) - sizeof (header);
    if ((size > 0xFFFFFF) || (size < 4)) {
      /* FLAC vorbis comment blocks are limited to 2^24 bytes, 
       * while the vorbis specs allow more than that. Shouldn't 
       * be a real world problem though
       */
      gst_element_error (GST_ELEMENT (tag), "Vorbis comment too long\n");
      return;
    } 
    GST_BUFFER_DATA (buffer)[1] = ((size & 0xFF0000) >> 16);
    GST_BUFFER_DATA (buffer)[2] = ((size & 0x00FF00) >>  8);
    GST_BUFFER_DATA (buffer)[3] =  (size & 0x0000FF);
    gst_pad_push (tag->srcpad, GST_DATA (buffer));
    tag->state = GST_FLAC_TAG_STATE_AUDIO_DATA;
  }

  /* The metadata blocks have been read, now we are reading audio data */
  if (tag->state == GST_FLAC_TAG_STATE_AUDIO_DATA) {
    gst_pad_push (tag->srcpad, GST_DATA (tag->buffer));
    tag->buffer = NULL;
  }
}


static GstElementStateReturn
gst_flac_tag_change_state (GstElement *element)
{
  GstFlacTag *tag;

  tag = GST_FLAC_TAG (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* do something to get out of the chain function faster */
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (tag->buffer) {
        gst_buffer_unref (tag->buffer);
	tag->buffer = NULL;
      }
      tag->state = GST_FLAC_TAG_STATE_INIT;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  return parent_class->change_state (element);
}
