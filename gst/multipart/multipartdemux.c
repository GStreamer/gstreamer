/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmultipartdemux.c: multipart stream demuxer
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

GST_DEBUG_CATEGORY_STATIC (gst_multipart_demux_debug);
#define GST_CAT_DEFAULT gst_multipart_demux_debug

#define GST_TYPE_MULTIPART_DEMUX (gst_multipart_demux_get_type())
#define GST_MULTIPART_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIPART_DEMUX, GstMultipartDemux))
#define GST_MULTIPART_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTIPART_DEMUX, GstMultipartDemux))
#define GST_IS_MULTIPART_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIPART_DEMUX))
#define GST_IS_MULTIPART_DEMUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTIPART_DEMUX))

#define MAX_LINE_LEN 500

typedef struct _GstMultipartDemux GstMultipartDemux;
typedef struct _GstMultipartDemuxClass GstMultipartDemuxClass;

static gchar *toFind = "--ThisRandomString\nContent-type: ";    //image/jpeg\n\n";
static gint toFindLen;

/* all information needed for one multipart stream */
typedef struct
{
  GstPad *pad;                  /* reference for this pad is held by element we belong to */

  gchar *mime;

  guint64 offset;               /* end offset of last buffer */
  guint64 known_offset;         /* last known offset */

  guint flags;
}
GstMultipartPad;

struct _GstMultipartDemux
{
  GstElement element;

  /* pad */
  GstPad *sinkpad;

  GSList *srcpads;
  gint numpads;

  gchar *parsing_mime;
  gchar *buffer;
  gint maxlen;
  gint bufsize;
  gint scanpos;
  gint lastpos;
};

struct _GstMultipartDemuxClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_multipart_demux_details =
GST_ELEMENT_DETAILS ("multipart demuxer",
    "Codec/Demuxer",
    "demux multipart streams",
    "Wim Taymans <wim@fluendo.com>");


/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate multipart_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate multipart_demux_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("multipart/x-mixed-replace")
    );


static void gst_multipart_demux_finalize (GObject * object);

static void gst_multipart_demux_chain (GstPad * pad, GstData * buffer);

static GstElementStateReturn gst_multipart_demux_change_state (GstElement *
    element);


GST_BOILERPLATE (GstMultipartDemux, gst_multipart_demux, GstElement,
    GST_TYPE_ELEMENT)

     static void gst_multipart_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_multipart_demux_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&multipart_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&multipart_demux_src_template_factory));

  toFindLen = strlen (toFind);
}

static void
gst_multipart_demux_class_init (GstMultipartDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gstelement_class->change_state = gst_multipart_demux_change_state;

  gobject_class->finalize = gst_multipart_demux_finalize;
}

static void
gst_multipart_demux_init (GstMultipartDemux * multipart)
{
  /* create the sink pad */
  multipart->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&multipart_demux_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (multipart), multipart->sinkpad);
  gst_pad_set_chain_function (multipart->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multipart_demux_chain));

  GST_FLAG_SET (multipart, GST_ELEMENT_EVENT_AWARE);

  multipart->maxlen = 4096;
  multipart->buffer = g_malloc (multipart->maxlen);
  multipart->parsing_mime = NULL;
  multipart->numpads = 0;
  multipart->scanpos = 0;
  multipart->lastpos = 0;
}

static void
gst_multipart_demux_finalize (GObject * object)
{
  GstMultipartDemux *multipart;

  multipart = GST_MULTIPART_DEMUX (object);
}

static void
gst_multipart_demux_handle_event (GstPad * pad, GstEvent * event)
{
  //GstMultipartDemux *multipart = GST_MULTIPART_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    case GST_EVENT_EOS:
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return;
}

static GstMultipartPad *
gst_multipart_find_pad_by_mime (GstMultipartDemux * demux, gchar * mime)
{
  GSList *walk;

  walk = demux->srcpads;
  while (walk) {
    GstMultipartPad *pad = (GstMultipartPad *) walk->data;

    if (!strcmp (pad->mime, mime)) {
      return pad;
    }

    walk = walk->next;
  }
  // pad not found, create it
  {
    GstPad *pad;
    GstMultipartPad *mppad;
    gchar *name;
    GstCaps *caps;

    mppad = g_new0 (GstMultipartPad, 1);

    name = g_strdup_printf ("src_%d", demux->numpads);
    pad = gst_pad_new_from_template (gst_static_pad_template_get
        (&multipart_demux_src_template_factory), name);
    g_free (name);
    caps = gst_caps_from_string (mime);
    gst_pad_use_explicit_caps (pad);
    gst_pad_set_explicit_caps (pad, caps);

    mppad->pad = pad;
    mppad->mime = g_strdup (mime);

    demux->srcpads = g_slist_prepend (demux->srcpads, mppad);
    demux->numpads++;

    gst_element_add_pad (GST_ELEMENT (demux), pad);

    return mppad;
  }
}

static void
gst_multipart_demux_chain (GstPad * pad, GstData * buffer)
{
  GstMultipartDemux *multipart;
  gint size;
  gchar *data;
  gint matchpos;

  /* handle events */
  if (GST_IS_EVENT (buffer)) {
    gst_multipart_demux_handle_event (pad, GST_EVENT (buffer));
    return;
  }

  multipart = GST_MULTIPART_DEMUX (gst_pad_get_parent (pad));

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  // first make sure our buffer is long enough
  if (multipart->bufsize + size > multipart->maxlen) {
    gint newsize = (multipart->bufsize + size) * 2;

    multipart->buffer = g_realloc (multipart->buffer, newsize);
    multipart->maxlen = newsize;
  }
  // copy bytes into the buffer
  memcpy (multipart->buffer + multipart->bufsize, data, size);
  multipart->bufsize += size;

  // find \n
  while (multipart->scanpos < multipart->bufsize) {
    if (multipart->buffer[multipart->scanpos] == '\n') {

    }
    multipart->scanpos++;
  }

  // then scan for the boundary
  for (matchpos = 0;
      multipart->scanpos + toFindLen + MAX_LINE_LEN - matchpos <
      multipart->bufsize; multipart->scanpos++) {
    if (multipart->buffer[multipart->scanpos] == toFind[matchpos]) {
      matchpos++;
      if (matchpos == toFindLen) {
        int datalen;
        int i, start;
        gchar *mime_type;

        multipart->scanpos++;

        start = multipart->scanpos;
        // find \n
        for (i = 0; i < MAX_LINE_LEN; i++) {
          if (multipart->buffer[multipart->scanpos] == '\n')
            break;
          multipart->scanpos++;
          matchpos++;
        }
        mime_type =
            g_strndup (multipart->buffer + start, multipart->scanpos - start);
        multipart->scanpos += 2;
        matchpos += 3;

        datalen = multipart->scanpos - matchpos;
        if (datalen > 0 && multipart->parsing_mime) {
          GstBuffer *outbuf;
          GstMultipartPad *srcpad;

          srcpad =
              gst_multipart_find_pad_by_mime (multipart,
              multipart->parsing_mime);
          if (srcpad != NULL) {
            outbuf = gst_buffer_new_and_alloc (datalen);

            memcpy (GST_BUFFER_DATA (outbuf), multipart->buffer, datalen);
            GST_BUFFER_TIMESTAMP (outbuf) = 0;
            gst_pad_push (srcpad->pad, GST_DATA (outbuf));
          }
        }
        // move rest downward
        multipart->bufsize -= multipart->scanpos;
        memcpy (multipart->buffer, multipart->buffer + multipart->scanpos,
            multipart->bufsize);

        multipart->parsing_mime = mime_type;
        multipart->scanpos = 0;
      }
    } else {
      matchpos = 0;
    }
  }

  gst_buffer_unref (buffer);
}

static GstElementStateReturn
gst_multipart_demux_change_state (GstElement * element)
{
  GstMultipartDemux *multipart;

  multipart = GST_MULTIPART_DEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return parent_class->change_state (element);
}

gboolean
gst_multipart_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multipart_demux_debug,
      "multipartdemux", 0, "multipart demuxer");

  return gst_element_register (plugin, "multipartdemux", GST_RANK_PRIMARY,
      GST_TYPE_MULTIPART_DEMUX);
}
