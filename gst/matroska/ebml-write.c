/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-write.c: write EBML data to file/stream
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

#include "ebml-write.h"
#include "ebml-ids.h"

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static void gst_ebml_write_class_init (GstEbmlWriteClass * klass);
static void gst_ebml_write_init (GstEbmlWrite * ebml);
static GstElementStateReturn gst_ebml_write_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

GType
gst_ebml_write_get_type (void)
{
  static GType gst_ebml_write_type = 0;

  if (!gst_ebml_write_type) {
    static const GTypeInfo gst_ebml_write_info = {
      sizeof (GstEbmlWriteClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ebml_write_class_init,
      NULL,
      NULL,
      sizeof (GstEbmlWrite),
      0,
      (GInstanceInitFunc) gst_ebml_write_init,
    };

    gst_ebml_write_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstEbmlWrite",
        &gst_ebml_write_info, 0);
  }

  return gst_ebml_write_type;
}

static void
gst_ebml_write_class_init (GstEbmlWriteClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ebml_write_change_state;
}

static void
gst_ebml_write_init (GstEbmlWrite * ebml)
{
  ebml->srcpad = NULL;
  ebml->pos = 0;

  ebml->cache = NULL;
}

static GstElementStateReturn
gst_ebml_write_change_state (GstElement * element)
{
  GstEbmlWrite *ebml = GST_EBML_WRITE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      ebml->pos = 0;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*
 * Caching.
 *
 * The idea is that you use this for writing a lot
 * of small elements. This will just "queue" all of
 * them and they'll be pushed to the next element all
 * at once. This saves memory and time for buffer
 * allocation and init, and it looks better.
 */

void
gst_ebml_write_set_cache (GstEbmlWrite * ebml, guint size)
{
  /* This is currently broken. I don't know why yet. */
  return;

  g_return_if_fail (ebml->cache == NULL);

  ebml->cache = gst_buffer_new_and_alloc (size);
  GST_BUFFER_SIZE (ebml->cache) = 0;
  GST_BUFFER_OFFSET (ebml->cache) = ebml->pos;
  ebml->handled = 0;
}

void
gst_ebml_write_flush_cache (GstEbmlWrite * ebml)
{
  if (!ebml->cache)
    return;

  /* this is very important. It may fail, in which case the client
   * programmer didn't use the cache somewhere. That's fatal. */
  g_assert (ebml->handled == GST_BUFFER_SIZE (ebml->cache));
  g_assert (GST_BUFFER_SIZE (ebml->cache) +
      GST_BUFFER_OFFSET (ebml->cache) == ebml->pos);

  gst_pad_push (ebml->srcpad, GST_DATA (ebml->cache));
  ebml->cache = NULL;
  ebml->handled = 0;
}

/*
 * One-element buffer, in case of no cache. If there is
 * a cache, use that instead.
 */

static GstBuffer *
gst_ebml_write_element_new (GstEbmlWrite * ebml, guint size)
{
  /* Create new buffer of size + ID + length */
  GstBuffer *buf;

  /* length, ID */
  size += 12;

  /* prefer cache */
  if (ebml->cache) {
    if (GST_BUFFER_MAXSIZE (ebml->cache) - GST_BUFFER_SIZE (ebml->cache) < size) {
      GST_LOG ("Cache available, but too small. Clearing...");
      gst_ebml_write_flush_cache (ebml);
    } else {
      return ebml->cache;
    }
  }

  /* else, use a one-element buffer. This is slower */
  buf = gst_buffer_new_and_alloc (size);
  GST_BUFFER_SIZE (buf) = 0;

  return buf;
}

/*
 * Write element ID into a buffer.
 */

static void
gst_ebml_write_element_id (GstBuffer * buf, guint32 id)
{
  guint8 *data = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);
  guint bytes = 4, mask = 0x10;

  /* get ID length */
  while (!(id & (mask << ((bytes - 1) * 8))) && bytes > 0) {
    mask <<= 1;
    bytes--;
  }

  /* if invalid ID, use dummy */
  if (bytes == 0) {
    GST_WARNING ("Invalid ID, voiding");
    bytes = 1;
    id = GST_EBML_ID_VOID;
  }

  /* write out, BE */
  GST_BUFFER_SIZE (buf) += bytes;
  while (bytes--) {
    data[bytes] = id & 0xff;
    id >>= 8;
  }
}

/*
 * Write element length into a buffer.
 */

static void
gst_ebml_write_element_size (GstBuffer * buf, guint64 size)
{
  guint8 *data = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);
  guint bytes = 1, mask = 0x80;

  /* how many bytes? */
  while ((size >> ((bytes - 1) * 8)) >= mask && bytes <= 8) {
    mask >>= 1;
    bytes++;
  }

  /* if invalid size, use max. */
  if (bytes > 8) {
    GST_WARNING ("Invalid size, maximizing");
    mask = 0x01;
    bytes = 8;
    /* Now here's a real FIXME: we cannot read those yet! */
    size = G_GINT64_CONSTANT (0x00ffffffffffffff);
  }

  /* write out, BE, with length size marker */
  GST_BUFFER_SIZE (buf) += bytes;
  while (bytes-- > 0) {
    data[bytes] = size & 0xff;
    size >>= 8;
    if (!bytes)
      *data |= mask;
  }
}

/*
 * Write element data into a buffer.
 */

static void
gst_ebml_write_element_data (GstBuffer * buf, guint8 * write, guint64 length)
{
  guint8 *data = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);

  memcpy (data, write, length);
  GST_BUFFER_SIZE (buf) += length;
}

/*
 * Write out buffer by moving it to the next element.
 */

static void
gst_ebml_write_element_push (GstEbmlWrite * ebml, GstBuffer * buf)
{
  guint data_size = GST_BUFFER_SIZE (buf) - ebml->handled;

  ebml->pos += data_size;
  if (buf == ebml->cache) {
    ebml->handled += data_size;
  }

  /* if there's no cache, then don't push it! */
  if (ebml->cache) {
    g_assert (buf == ebml->cache);
    return;
  }

  gst_pad_push (ebml->srcpad, GST_DATA (buf));
}

/*
 * Seek.
 */

void
gst_ebml_write_seek (GstEbmlWrite * ebml, guint64 pos)
{
  GstEvent *seek;

  /* Cache seeking. A bit dangerous, we assume the client writer
   * knows what he's doing... */
  if (ebml->cache) {
    /* within bounds? */
    if (pos >= GST_BUFFER_OFFSET (ebml->cache) &&
        pos <
        GST_BUFFER_OFFSET (ebml->cache) + GST_BUFFER_MAXSIZE (ebml->cache)) {
      GST_BUFFER_SIZE (ebml->cache) = pos - GST_BUFFER_OFFSET (ebml->cache);
      if (ebml->pos > pos)
        ebml->handled -= ebml->pos - pos;
      else
        ebml->handled += pos - ebml->pos;
      ebml->pos = pos;
    } else {
      GST_LOG ("Seek outside cache range. Clearing...");
      gst_ebml_write_flush_cache (ebml);
    }
  }

  seek = gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET, pos);
  gst_pad_push (ebml->srcpad, GST_DATA (seek));
  ebml->pos = pos;
}

/*
 * Get no. bytes needed to write a uint.
 */

static guint
gst_ebml_write_get_uint_size (guint64 num)
{
  guint size = 1;

  /* get size */
  while (num >= (G_GINT64_CONSTANT (1) << (size * 8)) && size < 8) {
    size++;
  }

  return size;
}


/*
 * Write an uint into a buffer.
 */

static void
gst_ebml_write_set_uint (GstBuffer * buf, guint64 num, guint size)
{
  guint8 *data;

  data = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);
  GST_BUFFER_SIZE (buf) += size;
  while (size-- > 0) {
    data[size] = num & 0xff;
    num >>= 8;
  }
}

/*
 * Data type wrappers.
 */

void
gst_ebml_write_uint (GstEbmlWrite * ebml, guint32 id, guint64 num)
{
  GstBuffer *buf = gst_ebml_write_element_new (ebml, sizeof (num));
  guint size = gst_ebml_write_get_uint_size (num);

  /* write */
  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, size);
  gst_ebml_write_set_uint (buf, num, size);
  gst_ebml_write_element_push (ebml, buf);
}

void
gst_ebml_write_sint (GstEbmlWrite * ebml, guint32 id, gint64 num)
{
  GstBuffer *buf = gst_ebml_write_element_new (ebml, sizeof (num));

  /* if the signed number is on the edge of a extra-byte,
   * then we'll fall over when detecting it. Example: if I
   * have a number (-)0x8000 (G_MINSHORT), then my abs()<<1
   * will be 0x10000; this is G_MAXUSHORT+1! So: if (<0) -1. */
  guint64 unum = (num < 0 ? (-num - 1) << 1 : num << 1);
  guint size = gst_ebml_write_get_uint_size (unum);

  /* make unsigned */
  if (num >= 0) {
    unum = num;
  } else {
    unum = 0x80 << (size - 1);
    unum += num;
    unum |= 0x80 << (size - 1);
  }

  /* write */
  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, size);
  gst_ebml_write_set_uint (buf, unum, size);
  gst_ebml_write_element_push (ebml, buf);
}

void
gst_ebml_write_float (GstEbmlWrite * ebml, guint32 id, gdouble num)
{
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
  gint n;
#endif
  GstBuffer *buf = gst_ebml_write_element_new (ebml, sizeof (num));

  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, 8);
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
  for (n = 0; n < 8; n++)
    GST_BUFFER_DATA (buf)[GST_BUFFER_SIZE (buf)] = ((guint8 *) & num)[7 - n];
  GST_BUFFER_SIZE (buf) += 8;
#else
  gst_ebml_write_element_data (buf, (guint8 *) & num, 8);
#endif
  gst_ebml_write_element_push (ebml, buf);
}

void
gst_ebml_write_ascii (GstEbmlWrite * ebml, guint32 id, const gchar * str)
{
  gint len = strlen (str) + 1;  /* add trailing '\0' */
  GstBuffer *buf = gst_ebml_write_element_new (ebml, len);

  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, len);
  gst_ebml_write_element_data (buf, (guint8 *) str, len);
  gst_ebml_write_element_push (ebml, buf);
}

void
gst_ebml_write_utf8 (GstEbmlWrite * ebml, guint32 id, const gchar * str)
{
  gst_ebml_write_ascii (ebml, id, str);
}

void
gst_ebml_write_date (GstEbmlWrite * ebml, guint32 id, gint64 date)
{
  gst_ebml_write_sint (ebml, id, date);
}

/*
 * Master writing is annoying. We use a size marker of
 * the max. allowed length, so that we can later fill it
 * in validly. 
 */

guint64
gst_ebml_write_master_start (GstEbmlWrite * ebml, guint32 id)
{
  guint64 pos = ebml->pos, t;
  GstBuffer *buf = gst_ebml_write_element_new (ebml, 0);

  t = GST_BUFFER_SIZE (buf);
  gst_ebml_write_element_id (buf, id);
  pos += GST_BUFFER_SIZE (buf) - t;
  gst_ebml_write_element_size (buf, -1);
  gst_ebml_write_element_push (ebml, buf);

  return pos;
}

void
gst_ebml_write_master_finish (GstEbmlWrite * ebml, guint64 startpos)
{
  guint64 pos = ebml->pos;
  GstBuffer *buf;

  gst_ebml_write_seek (ebml, startpos);
  buf = gst_ebml_write_element_new (ebml, 0);
  startpos =
      GUINT64_TO_BE ((G_GINT64_CONSTANT (1) << 56) | (pos - startpos - 8));
  memcpy (GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf), (guint8 *) & startpos,
      8);
  GST_BUFFER_SIZE (buf) += 8;
  gst_ebml_write_element_push (ebml, buf);
  gst_ebml_write_seek (ebml, pos);
}

void
gst_ebml_write_binary (GstEbmlWrite * ebml,
    guint32 id, guint8 * binary, guint64 length)
{
  GstBuffer *buf = gst_ebml_write_element_new (ebml, length);

  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, length);
  gst_ebml_write_element_data (buf, binary, length);
  gst_ebml_write_element_push (ebml, buf);
}

/*
 * For things like video frames and audio samples,
 * you want to use this function, as it doesn't have
 * the overhead of memcpy() that other functions
 * such as write_binary() do have.
 */

void
gst_ebml_write_buffer_header (GstEbmlWrite * ebml, guint32 id, guint64 length)
{
  GstBuffer *buf = gst_ebml_write_element_new (ebml, 0);

  gst_ebml_write_element_id (buf, id);
  gst_ebml_write_element_size (buf, length);
  gst_ebml_write_element_push (ebml, buf);
}

void
gst_ebml_write_buffer (GstEbmlWrite * ebml, GstBuffer * data)
{
  gst_ebml_write_element_push (ebml, data);
}

/*
 * When replacing a uint, we assume that it is *always*
 * 8-byte, since that's the safest guess we can do. This
 * is just for simplicity.
 *
 * FIXME: this function needs to be replaced with something
 * proper. This is a crude hack.
 */

void
gst_ebml_replace_uint (GstEbmlWrite * ebml, guint64 pos, guint64 num)
{
  guint64 oldpos = ebml->pos;
  GstBuffer *buf = gst_buffer_new_and_alloc (8);

  gst_ebml_write_seek (ebml, pos);
  GST_BUFFER_SIZE (buf) = 0;
  gst_ebml_write_set_uint (buf, num, 8);
  gst_ebml_write_element_push (ebml, buf);
  gst_ebml_write_seek (ebml, oldpos);
}

/*
 * Write EBML header.
 */

void
gst_ebml_write_header (GstEbmlWrite * ebml, gchar * doctype, guint version)
{
  guint64 pos;

  /* write the basic EBML header */
  gst_ebml_write_set_cache (ebml, 0x40);
  pos = gst_ebml_write_master_start (ebml, GST_EBML_ID_HEADER);
#if (GST_EBML_VERSION != 1)
  gst_ebml_write_uint (ebml, GST_EBML_ID_EBMLVERSION, GST_EBML_VERSION);
  gst_ebml_write_uint (ebml, GST_EBML_ID_EBMLREADVERSION, GST_EBML_VERSION);
#endif
#if 0
  /* we don't write these until they're "non-default" (never!) */
  gst_ebml_write_uint (ebml, GST_EBML_ID_EBMLMAXIDLENGTH, sizeof (guint32));
  gst_ebml_write_uint (ebml, GST_EBML_ID_EBMLMAXSIZELENGTH, sizeof (guint64));
#endif
  gst_ebml_write_ascii (ebml, GST_EBML_ID_DOCTYPE, doctype);
  gst_ebml_write_uint (ebml, GST_EBML_ID_DOCTYPEVERSION, version);
  gst_ebml_write_uint (ebml, GST_EBML_ID_DOCTYPEREADVERSION, version);
  gst_ebml_write_master_finish (ebml, pos);
  gst_ebml_write_flush_cache (ebml);
}
