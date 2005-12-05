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

static gchar *toFind = "--ThisRandomString\nContent-type: ";
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

static GstFlowReturn gst_multipart_demux_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_multipart_demux_change_state (GstElement *
    element, GstStateChange transition);


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

  gstelement_class->change_state = gst_multipart_demux_change_state;
}

static void
gst_multipart_demux_init (GstMultipartDemux * multipart,
    GstMultipartDemuxClass * g_class)
{
  /* create the sink pad */
  multipart->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&multipart_demux_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (multipart), multipart->sinkpad);
  gst_pad_set_chain_function (multipart->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multipart_demux_chain));

  multipart->maxlen = 4096;
  multipart->parsing_mime = NULL;
  multipart->numpads = 0;
  multipart->scanpos = 0;
  multipart->lastpos = 0;
}

static GstMultipartPad *
gst_multipart_find_pad_by_mime (GstMultipartDemux * demux, gchar * mime,
    gboolean * created)
{
  GSList *walk;

  walk = demux->srcpads;
  while (walk) {
    GstMultipartPad *pad = (GstMultipartPad *) walk->data;

    if (!strcmp (pad->mime, mime)) {
      if (created) {
        *created = FALSE;
      }
      return pad;
    }

    walk = walk->next;
  }
  /* pad not found, create it */
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
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_caps (pad, caps);

    mppad->pad = pad;
    mppad->mime = g_strdup (mime);

    demux->srcpads = g_slist_prepend (demux->srcpads, mppad);
    demux->numpads++;

    gst_element_add_pad (GST_ELEMENT (demux), pad);

    if (created) {
      *created = TRUE;
    }

    return mppad;
  }
}

static GstFlowReturn
gst_multipart_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstMultipartDemux *multipart;
  gint size, matchpos;
  guchar *data;
  GstFlowReturn ret = GST_FLOW_OK;

  multipart = GST_MULTIPART_DEMUX (gst_pad_get_parent (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* first make sure our buffer is long enough */
  if (multipart->bufsize + size > multipart->maxlen) {
    gint newsize = (multipart->bufsize + size) * 2;

    multipart->buffer = g_realloc (multipart->buffer, newsize);
    multipart->maxlen = newsize;
  }
  /* copy bytes into the buffer */
  memcpy (multipart->buffer + multipart->bufsize, data, size);
  multipart->bufsize += size;

  /* find \n */
  while (multipart->scanpos < multipart->bufsize) {
    if (multipart->buffer[multipart->scanpos] == '\n') {
      break;
    }
    multipart->scanpos++;
  }

  /* then scan for the boundary */
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
        /* find \n */
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
          gboolean created = FALSE;

          srcpad =
              gst_multipart_find_pad_by_mime (multipart,
              multipart->parsing_mime, &created);
          if (srcpad != NULL) {
            ret =
                gst_pad_alloc_buffer_and_set_caps (srcpad->pad,
                GST_BUFFER_OFFSET_NONE, datalen, GST_PAD_CAPS (srcpad->pad),
                &outbuf);
            if (ret != GST_FLOW_OK) {
              GST_WARNING_OBJECT (multipart, "failed allocating a %d bytes "
                  "buffer", datalen);
            } else {
              memcpy (GST_BUFFER_DATA (outbuf), multipart->buffer, datalen);
              if (created) {
                GstEvent *event;

                /* Push new segment */
                event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                    0, -1, 0);
                if (GST_IS_EVENT (event)) {
                  gst_pad_push_event (srcpad->pad, event);
                }
                GST_BUFFER_TIMESTAMP (outbuf) = 0;
              } else {
                GST_BUFFER_TIMESTAMP (outbuf) = -1;
              }
              gst_pad_push (srcpad->pad, outbuf);
            }
          }
        }
        /* move rest downward */
        multipart->bufsize -= multipart->scanpos;
        memmove (multipart->buffer, multipart->buffer + multipart->scanpos,
            multipart->bufsize);

        g_free (multipart->parsing_mime);
        multipart->parsing_mime = mime_type;
        multipart->scanpos = 0;
      }
    } else {
      matchpos = 0;
    }
  }

  gst_buffer_unref (buf);
  gst_object_unref (multipart);

  return ret;
}

static GstStateChangeReturn
gst_multipart_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMultipartDemux *multipart;
  GstStateChangeReturn ret;

  multipart = GST_MULTIPART_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      multipart->buffer = g_malloc (multipart->maxlen);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_free (multipart->parsing_mime);
      multipart->parsing_mime = NULL;
      g_free (multipart->buffer);
      multipart->buffer = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_multipart_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multipart_demux_debug,
      "multipartdemux", 0, "multipart demuxer");

  return gst_element_register (plugin, "multipartdemux", GST_RANK_PRIMARY,
      GST_TYPE_MULTIPART_DEMUX);
}
