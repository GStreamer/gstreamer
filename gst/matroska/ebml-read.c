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

GST_DEBUG_CATEGORY_STATIC (ebmlread_debug);
#define GST_CAT_DEFAULT ebmlread_debug

static void gst_ebml_read_class_init (GstEbmlReadClass * klass);
static void gst_ebml_read_init (GstEbmlRead * ebml);
static GstStateChangeReturn gst_ebml_read_change_state (GstElement * element,
    GstStateChange transition);

/* convenience functions */
static gboolean gst_ebml_read_peek_bytes (GstEbmlRead * ebml, guint size,
    GstBuffer ** p_buf, guint8 ** bytes);
static gboolean gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size,
    GstBuffer ** p_buf, guint8 ** bytes);


static GstElementClass *parent_class;   /* NULL */

GType
gst_ebml_read_get_type (void)
{
  static GType gst_ebml_read_type;      /* 0 */

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
gst_ebml_read_class_init (GstEbmlReadClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (ebmlread_debug, "ebmlread",
      0, "EBML stream helper class");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ebml_read_change_state);
}

static void
gst_ebml_read_init (GstEbmlRead * ebml)
{
  ebml->sinkpad = NULL;
  ebml->level = NULL;
}

static GstStateChangeReturn
gst_ebml_read_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstEbmlRead *ebml = GST_EBML_READ (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!ebml->sinkpad) {
        g_return_val_if_reached (GST_STATE_CHANGE_FAILURE);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      g_list_foreach (ebml->level, (GFunc) g_free, NULL);
      g_list_free (ebml->level);
      ebml->level = NULL;
      if (ebml->cached_buffer) {
        gst_buffer_unref (ebml->cached_buffer);
        ebml->cached_buffer = NULL;
      }
      ebml->offset = 0;
      break;
    }
    default:
      break;
  }

  return ret;
}

/*
 * Return: the amount of levels in the hierarchy that the
 * current element lies higher than the previous one.
 * The opposite isn't done - that's auto-done using master
 * element reading.
 */

static guint
gst_ebml_read_element_level_up (GstEbmlRead * ebml)
{
  guint num = 0;
  guint64 pos = ebml->offset;

  while (ebml->level != NULL) {
    GList *last = g_list_last (ebml->level);
    GstEbmlLevel *level = last->data;

    if (pos >= level->start + level->length) {
      ebml->level = g_list_remove (ebml->level, level);
      g_free (level);
      num++;
    } else {
      break;
    }
  }

  return num;
}

/*
 * Calls pull_range for (offset,size) without advancing our offset
 */
static gboolean
gst_ebml_read_peek_bytes (GstEbmlRead * ebml, guint size, GstBuffer ** p_buf,
    guint8 ** bytes)
{
  GstFlowReturn ret;

  /* Caching here actually makes much less difference than one would expect.
   * We do it mainly to avoid pulling buffers of 1 byte all the time */
  if (ebml->cached_buffer) {
    guint64 cache_offset = GST_BUFFER_OFFSET (ebml->cached_buffer);
    guint cache_size = GST_BUFFER_SIZE (ebml->cached_buffer);

    if (cache_offset <= ebml->offset &&
        (ebml->offset + size) < (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_create_sub (ebml->cached_buffer,
            ebml->offset - cache_offset, size);
      if (bytes)
        *bytes =
            GST_BUFFER_DATA (ebml->cached_buffer) + ebml->offset - cache_offset;
      return TRUE;
    }
    gst_buffer_unref (ebml->cached_buffer);
    ebml->cached_buffer = NULL;
  }

  if (gst_pad_pull_range (ebml->sinkpad, ebml->offset, MAX (size, 64 * 1024),
          &ebml->cached_buffer) == GST_FLOW_OK &&
      GST_BUFFER_SIZE (ebml->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_create_sub (ebml->cached_buffer, 0, size);
    if (bytes)
      *bytes = GST_BUFFER_DATA (ebml->cached_buffer);
    return TRUE;
  }

  if (!p_buf)
    return FALSE;

  ret = gst_pad_pull_range (ebml->sinkpad, ebml->offset, size, p_buf);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("pull_range returned %d", ret);
    return FALSE;
  }

  if (GST_BUFFER_SIZE (*p_buf) < size) {
    GST_WARNING_OBJECT (ebml, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", ebml->offset,
        size, GST_BUFFER_SIZE (*p_buf));
    gst_buffer_unref (*p_buf);
    *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return FALSE;
  }

  if (bytes)
    *bytes = GST_BUFFER_DATA (*p_buf);

  return TRUE;
}

/*
 * Calls pull_range for (offset,size) and advances our offset by size
 */
static gboolean
gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size, GstBuffer ** p_buf,
    guint8 ** bytes)
{
  if (!gst_ebml_read_peek_bytes (ebml, size, p_buf, bytes))
    return FALSE;

  ebml->offset += size;
  return TRUE;
}

/*
 * Read: the element content data ID.
 * Return: FALSE on error.
 */

static gboolean
gst_ebml_read_element_id (GstEbmlRead * ebml, guint32 * id, guint * level_up)
{
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1;
  guint32 total;
  guint8 b;

  if (!gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf))
    return FALSE;

  b = GST_READ_UINT8 (buf);

  total = (guint32) b;

  while (read <= 4 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 4) {
    guint64 pos = ebml->offset;

    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid EBML ID size tag (0x%x) at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", (guint) b, pos, pos));
    return FALSE;
  }

  if (!gst_ebml_read_peek_bytes (ebml, read, NULL, &buf))
    return FALSE;

  while (n < read) {
    b = GST_READ_UINT8 (buf + n);
    total = (total << 8) | b;
    ++n;
  }

  *id = total;

  /* level */
  if (level_up)
    *level_up = gst_ebml_read_element_level_up (ebml);

  ebml->offset += read;
  return TRUE;
}

/*
 * Read: element content length.
 * Return: the number of bytes read or -1 on error.
 */

static gint
gst_ebml_read_element_length (GstEbmlRead * ebml, guint64 * length)
{
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;
  guint8 b;

  if (!gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf))
    return -1;

  b = GST_READ_UINT8 (buf);

  total = (guint64) b;

  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8) {
    guint64 pos = ebml->offset;

    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid EBML length size tag (0x%x) at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", (guint) b, pos, pos));
    return -1;
  }

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;

  if (!gst_ebml_read_peek_bytes (ebml, read, NULL, &buf))
    return -1;

  while (n < read) {
    guint8 b = GST_READ_UINT8 (buf + n);

    if (b == 0xff)
      num_ffs++;
    total = (total << 8) | b;
    ++n;
  }

  if (read == num_ffs)
    *length = G_MAXUINT64;
  else
    *length = total;

  ebml->offset += read;

  return read;
}

/*
 * Return: the ID of the next element.
 * Level_up contains the amount of levels that this
 * next element lies higher than the previous one.
 */

gboolean
gst_ebml_peek_id (GstEbmlRead * ebml, guint * level_up, guint32 * id)
{
  guint64 off;

  g_assert (level_up);

  off = ebml->offset;           /* save offset */

  if (!gst_ebml_read_element_id (ebml, id, level_up))
    return FALSE;

  ebml->offset = off;           /* restore offset */
  return TRUE;
}

/*
 * Return the length of the stream in bytes
 */

gint64
gst_ebml_read_get_length (GstEbmlRead * ebml)
{
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 end;

  if (!gst_pad_query_duration (GST_PAD_PEER (ebml->sinkpad), &fmt, &end))
    g_return_val_if_reached (0);        ///// FIXME /////////

  if (fmt != GST_FORMAT_BYTES || end < 0)
    g_return_val_if_reached (0);        ///// FIXME /////////

  return end;
}

/*
 * Seek to a given offset.
 */

gboolean
gst_ebml_read_seek (GstEbmlRead * ebml, guint64 offset)
{
  if (offset >= gst_ebml_read_get_length (ebml))
    return FALSE;

  ebml->offset = offset;

  return TRUE;
}

/*
 * Skip the next element.
 */

gboolean
gst_ebml_read_skip (GstEbmlRead * ebml)
{
  guint64 length;
  guint32 id;

  if (!gst_ebml_read_element_id (ebml, &id, NULL))
    return FALSE;

  if (gst_ebml_read_element_length (ebml, &length) < 0)
    return FALSE;

  ebml->offset += length;
  return TRUE;
}

/*
 * Read the next element as a GstBuffer (binary).
 */

gboolean
gst_ebml_read_buffer (GstEbmlRead * ebml, guint32 * id, GstBuffer ** buf)
{
  guint64 length;

  if (!gst_ebml_read_element_id (ebml, id, NULL))
    return FALSE;

  if (gst_ebml_read_element_length (ebml, &length) < 0)
    return FALSE;

  if (length == 0) {
    *buf = gst_buffer_new ();
    return TRUE;
  }

  *buf = NULL;
  if (!gst_ebml_read_pull_bytes (ebml, (guint) length, buf, NULL))
    return FALSE;

  return TRUE;
}

/*
 * Read the next element, return a pointer to it and its size.
 */

static gboolean
gst_ebml_read_bytes (GstEbmlRead * ebml, guint32 * id, guint8 ** data,
    guint * size)
{
  guint64 length;

  *size = 0;

  if (!gst_ebml_read_element_id (ebml, id, NULL))
    return FALSE;

  if (gst_ebml_read_element_length (ebml, &length) < 0)
    return FALSE;

  if (length == 0) {
    *data = NULL;
    return TRUE;
  }

  *data = NULL;
  if (!gst_ebml_read_pull_bytes (ebml, (guint) length, NULL, data))
    return FALSE;

  *size = (guint) length;

  return TRUE;
}

/*
 * Read the next element as an unsigned int.
 */

gboolean
gst_ebml_read_uint (GstEbmlRead * ebml, guint32 * id, guint64 * num)
{
  guint8 *data;
  guint size;

  if (!gst_ebml_read_bytes (ebml, id, &data, &size))
    return FALSE;

  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid integer element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)",
            size, ebml->offset - size, ebml->offset - size));
    return FALSE;
  }
  *num = 0;
  while (size > 0) {
    *num = (*num << 8) | *data;
    size--;
    data++;
  }

  return TRUE;
}

/*
 * Read the next element as a signed int.
 */

gboolean
gst_ebml_read_sint (GstEbmlRead * ebml, guint32 * id, gint64 * num)
{
  guint8 *data;
  guint size;
  gboolean negative = 0;

  if (!gst_ebml_read_bytes (ebml, id, &data, &size))
    return FALSE;

  if (size < 1 || size > 8) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid integer element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", size, ebml->offset - size,
            ebml->offset - size));
    return FALSE;
  }

  *num = 0;
  if (*data & 0x80) {
    negative = 1;
    *num = *data & ~0x80;
    size--;
    data++;
  }

  while (size > 0) {
    *num = (*num << 8) | *data;
    size--;
    data++;
  }

  /* make signed */
  if (negative) {
    *num = 0 - *num;
  }

  return TRUE;
}

/*
 * Read the next element as a float.
 */

gboolean
gst_ebml_read_float (GstEbmlRead * ebml, guint32 * id, gdouble * num)
{
  guint8 *data;
  guint size;

  if (!gst_ebml_read_bytes (ebml, id, &data, &size))
    return FALSE;

  if (size != 4 && size != 8 && size != 10) {
    GST_ELEMENT_ERROR (ebml, STREAM, DEMUX, (NULL),
        ("Invalid float element size %d at position %" G_GUINT64_FORMAT
            " (0x%" G_GINT64_MODIFIER "x)", size, ebml->offset - size,
            ebml->offset - size));
    return FALSE;
  }

  if (size == 10) {
    GST_ELEMENT_ERROR (ebml, CORE, NOT_IMPLEMENTED, (NULL),
        ("FIXME! 10-byte floats unimplemented"));
    return FALSE;
  }

  if (size == 4) {
    gfloat f;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    f = *(gfloat *) data;
#else
    while (size > 0) {
      ((guint8 *) & f)[size - 1] = data[4 - size];
      size--;
    }
#endif

    *num = f;
  } else {
    gdouble d;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    d = *(gdouble *) data;
#else
    while (size > 0) {
      ((guint8 *) & d)[size - 1] = data[8 - size];
      size--;
    }
#endif

    *num = d;
  }

  return TRUE;
}

/*
 * Read the next element as an ASCII string.
 */

gboolean
gst_ebml_read_ascii (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  guint8 *data;
  guint size;

  if (!gst_ebml_read_bytes (ebml, id, &data, &size))
    return FALSE;

  *str = g_malloc (size + 1);
  memcpy (*str, data, size);
  (*str)[size] = '\0';

  return TRUE;
}

/*
 * Read the next element as a UTF-8 string.
 */

gboolean
gst_ebml_read_utf8 (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  gboolean ret;

#ifndef GST_DISABLE_GST_DEBUG
  guint64 oldoff = ebml->offset;
#endif

  ret = gst_ebml_read_ascii (ebml, id, str);

  if (str != NULL && *str != NULL && **str != '\0' &&
      !g_utf8_validate (*str, -1, NULL)) {
    GST_WARNING ("Invalid UTF-8 string at offset %" G_GUINT64_FORMAT, oldoff);
  }

  return ret;
}

/*
 * Read the next element as a date.
 * Returns the seconds since the unix epoch.
 */

gboolean
gst_ebml_read_date (GstEbmlRead * ebml, guint32 * id, gint64 * date)
{
  gint64 ebml_date;
  gboolean res = gst_ebml_read_sint (ebml, id, &ebml_date);

  *date = (ebml_date / GST_SECOND) + GST_EBML_DATE_OFFSET;
  return res;
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 */

gboolean
gst_ebml_read_master (GstEbmlRead * ebml, guint32 * id)
{
  GstEbmlLevel *level;
  guint64 length;

  if (!gst_ebml_read_element_id (ebml, id, NULL))
    return FALSE;

  if (gst_ebml_read_element_length (ebml, &length) < 0)
    return FALSE;

  /* remember level */
  level = g_new (GstEbmlLevel, 1);
  level->start = ebml->offset;
  level->length = length;
  ebml->level = g_list_append (ebml->level, level);

  return TRUE;
}

/*
 * Read the next element as binary data.
 */

gboolean
gst_ebml_read_binary (GstEbmlRead * ebml,
    guint32 * id, guint8 ** binary, guint64 * length)
{
  guint8 *data;
  guint size;

  if (!gst_ebml_read_bytes (ebml, id, &data, &size))
    return FALSE;

  *length = size;
  *binary = g_memdup (data, size);

  return TRUE;
}

/*
 * Read an EBML header.
 */

gboolean
gst_ebml_read_header (GstEbmlRead * ebml, gchar ** doctype, guint * version)
{
  /* this function is the first to be called */
  guint32 id;
  guint level_up;

  /* default init */
  if (doctype)
    *doctype = NULL;
  if (version)
    *version = 1;

  if (!gst_ebml_peek_id (ebml, &level_up, &id))
    return FALSE;

  GST_DEBUG_OBJECT (ebml, "id: %08x", GST_READ_UINT32_BE (&id));

  if (level_up != 0 || id != GST_EBML_ID_HEADER) {
    GST_ELEMENT_ERROR (ebml, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }
  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  while (TRUE) {
    if (!gst_ebml_peek_id (ebml, &level_up, &id))
      return FALSE;

    /* end-of-header */
    if (level_up)
      break;

    switch (id) {
        /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLREADVERSION);
        if (num != GST_EBML_VERSION)
          return FALSE;
        break;
      }

        /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLMAXSIZELENGTH);
        if (num != sizeof (guint64))
          return FALSE;
        break;
      }

        /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          return FALSE;
        g_assert (id == GST_EBML_ID_EBMLMAXIDLENGTH);
        if (num != sizeof (guint32))
          return FALSE;
        break;
      }

      case GST_EBML_ID_DOCTYPE:{
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

      case GST_EBML_ID_DOCTYPEREADVERSION:{
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
