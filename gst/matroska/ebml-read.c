/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-read.c: read EBML data from file/stream
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

#include <string.h>

#include "ebml-read.h"
#include "ebml-ids.h"

enum {
  /* FILL ME */
  LAST_SIGNAL
};

static void gst_ebml_read_class_init   (GstEbmlReadClass *klass);
static void gst_ebml_read_init         (GstEbmlRead      *ebml);
static GstElementStateReturn
	    gst_ebml_read_change_state (GstElement       *element);

static GstElementClass *parent_class = NULL;

GType
gst_ebml_read_get_type (void) 
{
  static GType gst_ebml_read_type = 0;

  if (!gst_ebml_read_type) {
    static const GTypeInfo gst_ebml_read_info = {
      sizeof (GstEbmlReadClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_ebml_read_class_init,
      NULL,
      NULL,
      sizeof (GstEbmlRead),
      0,
      (GInstanceInitFunc) gst_ebml_read_init,
    };

    gst_ebml_read_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstEbmlRead",
				&gst_ebml_read_info, 0);
  }

  return gst_ebml_read_type;
}

static void
gst_ebml_read_class_init (GstEbmlReadClass *klass) 
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ebml_read_change_state;
}

static void
gst_ebml_read_init (GstEbmlRead *ebml)
{
  ebml->sinkpad = NULL;
  ebml->bs = NULL;
  ebml->level = NULL;
}

static GstElementStateReturn
gst_ebml_read_change_state (GstElement *element)
{
  GstEbmlRead *ebml = GST_EBML_READ (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      if (!ebml->sinkpad)
        return GST_STATE_FAILURE;
      ebml->bs = gst_bytestream_new (ebml->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (ebml->bs);
      while (ebml->level) {
        GstEbmlLevel *level = ebml->level->data;

        ebml->level = g_list_remove (ebml->level, level);
        g_free (level);
      }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*
 * Return: the amount of levels in the hierarchy that the
 * current element lies higher than the previous one.
 * The opposite isn't done - that's auto-done using master
 * element reading.
 */

static guint
gst_ebml_read_element_level_up (GstEbmlRead *ebml)
{
  guint num = 0;
  guint64 pos = gst_bytestream_tell (ebml->bs);

  while (ebml->level != NULL) {
    GList *last = g_list_last (ebml->level);
    GstEbmlLevel *level = last->data;

    if (pos >= level->start + level->length) {
      ebml->level = g_list_remove (ebml->level, level);
      g_free (level);
      num++;
    } else
      break;
  }

  return num;
}

/*
 * Read: the element content data ID.
 * Return: the number of bytes read or -1 on error.
 */

static gint
gst_ebml_read_element_id (GstEbmlRead *ebml,
			  guint32     *id,
			  guint       *level_up)
{
  guint8 *data;
  gint len_mask = 0x80, read = 1, n = 1;
  guint32 total;

  while (gst_bytestream_peek_bytes (ebml->bs, &data, 1) != 1) {
    GstEvent *event = NULL;
    guint32 remaining;

    /* Here, we might encounter EOS */
    gst_bytestream_get_status (ebml->bs, &remaining, &event);
    if (event) {
      gst_pad_event_default (ebml->sinkpad, event);
    } else {
      guint64 pos = gst_bytestream_tell (ebml->bs);
      gst_event_unref (event);
      GST_ELEMENT_ERROR (ebml, RESOURCE, READ, (NULL),
			 ("Read error at position %llu (0x%llx)",
			 pos, pos));
      return -1;
    }
  }
  total = data[0];
  while (read <= 4 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 4) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
		       ("Invalid EBML ID size tag (0x%x) at position %llu (0x%llx)",
		       data[0], pos, pos));
    return -1;
  }

  if (gst_bytestream_peek_bytes (ebml->bs, &data, read) != read) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, RESOURCE, READ, (NULL),
		       ("Read error at position %llu (0x%llx)", pos, pos));
    return -1;
  }
  while (n < read)
    total = (total << 8) | data[n++];

  *id = total;

  /* level */
  if (level_up)
    *level_up = gst_ebml_read_element_level_up (ebml);

  return read;
}

/*
 * Read: element content length.
 * Return: the number of bytes read or -1 on error.
 */

static gint
gst_ebml_read_element_length (GstEbmlRead *ebml,
			      guint64     *length)
{
  guint8 *data;
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;

  if (gst_bytestream_peek_bytes (ebml->bs, &data, 1) != 1) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, RESOURCE, READ, (NULL),
		       ("Read error at position %llu (0x%llx)", pos, pos));
    return -1;
  }
  total = data[0];
  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
		       ("Invalid EBML length size tag (0x%x) at position %llu (0x%llx)",
		       data[0], pos, pos));
    return -1;
  }

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;
  if (gst_bytestream_peek_bytes (ebml->bs, &data, read) != read) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, RESOURCE, READ, (NULL),
		       ("Read error at position %llu (0x%llx)", pos, pos));
    return -1;
  }
  while (n < read) {
    if (data[n] == 0xff)
      num_ffs++;
    total = (total << 8) | data[n];
    n++;
  }

  if (read == num_ffs)
    *length = G_MAXUINT64;
  else
    *length = total;

  return read;
}

/*
 * Read: the actual data.
 * Return: the data, as a GstBuffer.
 */

static GstBuffer *
gst_ebml_read_element_data (GstEbmlRead *ebml,
			    guint64      length)
{
  GstBuffer *buf = NULL;

  if (gst_bytestream_peek (ebml->bs, &buf, length) != length) {
    guint64 pos = gst_bytestream_tell (ebml->bs);
    GST_ELEMENT_ERROR (ebml, RESOURCE, READ, (NULL),
		       ("Read error at position %llu (0x%llx)", pos, pos));
    if (buf)
      gst_buffer_unref (buf);
    return NULL;
  }

  gst_bytestream_flush_fast (ebml->bs, length);

  return buf;
}

/*
 * Return: the ID of the next element.
 * Level_up contains the amount of levels that this
 * next element lies higher than the previous one.
 */

guint32
gst_ebml_peek_id (GstEbmlRead *ebml,
		  guint       *level_up)
{
  guint32 id;

  g_assert (level_up);

  if (gst_ebml_read_element_id (ebml, &id, level_up) < 0)
    return 0;

  return id;
}

/*
 * Seek to a given offset.
 */

GstEvent *
gst_ebml_read_seek (GstEbmlRead *ebml,
		    guint64      offset)
{
  guint32 remaining;
  GstEvent *event = NULL;
  guchar *data;

  /* first, flush remaining buffers */
  gst_bytestream_get_status (ebml->bs, &remaining, &event);
  if (event) {
    g_warning ("Unexpected event before seek");
    gst_event_unref (event);
  }
  if (remaining)
    gst_bytestream_flush_fast (ebml->bs, remaining);

  /* now seek */
  if (!gst_bytestream_seek (ebml->bs, offset, GST_SEEK_METHOD_SET)) {
    GST_ELEMENT_ERROR (ebml, RESOURCE, SEEK, (NULL),
		       ("Seek to position %llu (0x%llx) failed", offset, offset));
    return NULL;
  }

  while (!event) {
    /* and now, peek a new byte. This will fail because there's a
     * pending event. Then, take the event and return it. */
    if (gst_bytestream_peek_bytes (ebml->bs, &data, 1)) {
      GST_WARNING ("Unexpected data after seek - this means seek failed");
      break;
    }

    /* get the discont event and return */
    gst_bytestream_get_status (ebml->bs, &remaining, &event);
    if (!event) {
      GST_WARNING ("No discontinuity event after seek - seek failed");
      break;
    } else if (GST_EVENT_TYPE (event) != GST_EVENT_DISCONTINUOUS) {
      gst_pad_event_default (ebml->sinkpad, event);
      event = NULL;
    }
  }

  return event;
}

/*
 * Skip the next element.
 */

gboolean
gst_ebml_read_skip (GstEbmlRead *ebml)
{
  gint bytes;
  guint32 id, remaining;
  guint64 length;
  GstEvent *event;

  if ((bytes = gst_ebml_read_element_id (ebml, &id, NULL)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  if ((bytes = gst_ebml_read_element_length (ebml, &length)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  /* do we have enough bytes left to skip? */
  gst_bytestream_get_status (ebml->bs, &remaining, &event);
  if (event) {
    g_warning ("Unexpected event before skip");
    gst_event_unref (event);
  }

  if (remaining >= length)
    return gst_bytestream_flush (ebml->bs, length);

  if (!(event = gst_ebml_read_seek (ebml,
			gst_bytestream_tell (ebml->bs) + length)))
    return FALSE;

  gst_event_unref (event);

  return TRUE;
}

/*
 * Read the next element as a GstBuffer (binary).
 */

gboolean
gst_ebml_read_buffer (GstEbmlRead *ebml,
		      guint32     *id,
		      GstBuffer  **buf)
{
  gint bytes;
  guint64 length;

  if ((bytes = gst_ebml_read_element_id (ebml, id, NULL)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  if ((bytes = gst_ebml_read_element_length (ebml, &length)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  return ((*buf = gst_ebml_read_element_data (ebml, length)) != NULL);
}

/*
 * Read the next element as an unsigned int.
 */

gboolean
gst_ebml_read_uint (GstEbmlRead *ebml,
		    guint32     *id,
		    guint64     *num)
{
  GstBuffer *buf;
  guint8 *data;
  guint size;

  if (!gst_ebml_read_buffer (ebml, id, &buf))
    return FALSE;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
		       ("Invalid integer element size %d at position %llu (0x%llu)",
		       size, GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET (buf)));
    gst_buffer_unref (buf);
    return FALSE;
  }
  *num = 0;
  while (size > 0) {
    *num = (*num << 8) | data[GST_BUFFER_SIZE (buf) - size];
    size--;
  }

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read the next element as a signed int.
 */

gboolean
gst_ebml_read_sint (GstEbmlRead *ebml,
		    guint32     *id,
		    gint64      *num)
{
  GstBuffer *buf;
  guint8 *data;
  guint size, negative = 0, n = 0;

  if (!gst_ebml_read_buffer (ebml, id, &buf))
    return FALSE;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
		       ("Invalid integer element size %d at position %llu (0x%llx)",
		       size, GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET (buf)));
    gst_buffer_unref (buf);
    return FALSE;
  }
  if (data[0] & 0x80) {
    negative = 1;
    data[0] &= ~0x80;
  }
  *num = 0;
  while (n < size) {
    *num = (*num << 8) | data[n++];
  }

  /* make signed */
  if (negative) {
    *num = *num - (1LL << ((8 * size) - 1));
  }

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read the next element as a float.
 */

gboolean
gst_ebml_read_float (GstEbmlRead *ebml,
		     guint32     *id,
		     gdouble     *num)
{
  GstBuffer *buf;
  guint8 *data;
  guint size;

  if (!gst_ebml_read_buffer (ebml, id, &buf))
    return FALSE;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (size != 4 && size != 8 && size != 10) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
		       ("Invalid float element size %d at position %llu (0x%llx)",
		       size, GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET (buf)));
    gst_buffer_unref (buf);
    return FALSE;
  }

  if (size == 10) {
    GST_ELEMENT_ERROR (ebml, CORE, NOT_IMPLEMENTED, (NULL),
		       ("FIXME! 10-byte floats unimplemented"));
    gst_buffer_unref (buf);
    return FALSE;
  }

  if (size == 4) {
    gfloat f;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    f = * (gfloat *) data;
#else
    while (size > 0) {
      ((guint8 *) &f)[size - 1] = data[4 - size];
      size--;
    }
#endif

    *num = f;
  } else {
    gdouble d;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    d = * (gdouble *) data;
#else
    while (size > 0) {
      ((guint8 *) &d)[size - 1] = data[8 - size];
      size--;
    }
#endif

    *num = d;
  }

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read the next element as an ASCII string.
 */

gboolean
gst_ebml_read_ascii (GstEbmlRead *ebml,
		     guint32     *id,
		     gchar      **str)
{
  GstBuffer *buf;

  if (!gst_ebml_read_buffer (ebml, id, &buf))
    return FALSE;

  *str = g_malloc (GST_BUFFER_SIZE (buf) + 1);
  memcpy (*str, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  (*str)[GST_BUFFER_SIZE (buf)] = '\0';

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read the next element as a UTF-8 string.
 */

gboolean
gst_ebml_read_utf8 (GstEbmlRead *ebml,
		    guint32     *id,
		    gchar      **str)
{
  return gst_ebml_read_ascii (ebml, id, str);
}

/*
 * Read the next element as a date (nanoseconds since 1/1/2000).
 */

gboolean
gst_ebml_read_date (GstEbmlRead *ebml,
		    guint32     *id,
		    gint64      *date)
{
  return gst_ebml_read_sint (ebml, id, date);
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 */

gboolean
gst_ebml_read_master (GstEbmlRead *ebml,
		      guint32     *id)
{
  gint bytes;
  guint64 length;
  GstEbmlLevel *level;

  if ((bytes = gst_ebml_read_element_id (ebml, id, NULL)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  if ((bytes = gst_ebml_read_element_length (ebml, &length)) < 0)
    return FALSE;
  gst_bytestream_flush_fast (ebml->bs, bytes);

  /* remember level */
  level = g_new (GstEbmlLevel, 1);
  level->start = gst_bytestream_tell (ebml->bs);
  level->length = length;
  ebml->level = g_list_append (ebml->level, level);

  return TRUE;
}

/*
 * Read the next element as binary data.
 */

gboolean
gst_ebml_read_binary (GstEbmlRead *ebml,
		      guint32     *id,
		      guint8     **binary,
		      guint64     *length)
{
  GstBuffer *buf;

  if (!gst_ebml_read_buffer (ebml, id, &buf))
    return FALSE;

  *length = GST_BUFFER_SIZE (buf);
  *binary = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  gst_buffer_unref (buf);

  return TRUE;
}

/*
 * Read an EBML header.
 */

gboolean
gst_ebml_read_header (GstEbmlRead *ebml,
		      gchar      **doctype,
		      guint       *version)
{
  /* this function is the first to be called */
  guint32 id;
  guint level_up;

  /* default init */
  if (doctype)
    *doctype = NULL;
  if (version)
    *version = 1;

  if (!(id = gst_ebml_peek_id (ebml, &level_up)))
    return FALSE;
  if (level_up != 0 || id != GST_EBML_ID_HEADER) {
    GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }
  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;
  g_assert (id == GST_EBML_ID_HEADER);

  while (TRUE) {
    if (!(id = gst_ebml_peek_id (ebml, &level_up)))
      return FALSE;

    /* end-of-header */
    if (level_up)
      break;

    switch (id) {
      /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION: {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLREADVERSION);
        if (num != GST_EBML_VERSION)
          return FALSE;
        break;
      }

      /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH: {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLMAXSIZELENGTH);
        if (num != sizeof (guint64))
          return FALSE;
        break;
      }

      /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH: {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLMAXIDLENGTH);
        if (num != sizeof (guint32))
          return FALSE;
        break;
      }

      case GST_EBML_ID_DOCTYPE: {
        gchar *text;

        if (!gst_ebml_read_ascii (ebml, &id, &text))
          return FALSE;
        g_assert (id == GST_EBML_ID_DOCTYPE); 
        if (doctype) {
          if (doctype)
            g_free (*doctype);
          *doctype = text;
        } else
          g_free (text);
        break;
      }

      case GST_EBML_ID_DOCTYPEREADVERSION: {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_DOCTYPEREADVERSION); 
        if (version)
          *version = num;
        break;
      }

      default:
        GST_WARNING ("Unknown data type 0x%x in EBML header (ignored)", id);
        /* pass-through */

      /* we ignore these two, as they don't tell us anything we care about */
      case GST_EBML_ID_VOID:
      case GST_EBML_ID_EBMLVERSION:
      case GST_EBML_ID_DOCTYPEVERSION:
        if (!gst_ebml_read_skip (ebml))
          return FALSE;
        break;
    }
  }

  return TRUE;
}
