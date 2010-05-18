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

#include <math.h>

/* NAN is supposed to be in math.h, Microsoft defines it in xmath.h */
#ifdef _MSC_VER
#include <xmath.h>
#endif

/* If everything goes wrong try 0.0/0.0 which should be NAN */
#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

GST_DEBUG_CATEGORY_STATIC (ebmlread_debug);
#define GST_CAT_DEFAULT ebmlread_debug

static void gst_ebml_read_class_init (GstEbmlReadClass * klass);

static void gst_ebml_read_init (GstEbmlRead * ebml);

static GstStateChangeReturn gst_ebml_read_change_state (GstElement * element,
    GstStateChange transition);

/* convenience functions */
static GstFlowReturn gst_ebml_read_peek_bytes (GstEbmlRead * ebml, guint size,
    GstBuffer ** p_buf, guint8 ** bytes);
static GstFlowReturn gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size,
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

void
gst_ebml_level_free (GstEbmlLevel * level)
{
  g_slice_free (GstEbmlLevel, level);
}

static void
gst_ebml_finalize (GObject * obj)
{
  GstEbmlRead *ebml = GST_EBML_READ (obj);

  g_list_foreach (ebml->level, (GFunc) gst_ebml_level_free, NULL);
  g_list_free (ebml->level);
  ebml->level = NULL;
  if (ebml->cached_buffer) {
    gst_buffer_unref (ebml->cached_buffer);
    ebml->cached_buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_ebml_read_class_init (GstEbmlReadClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (ebmlread_debug, "ebmlread",
      0, "EBML stream helper class");

  gobject_class->finalize = gst_ebml_finalize;

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
      g_list_foreach (ebml->level, (GFunc) gst_ebml_level_free, NULL);
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
 * Used in push mode.
 * Provided buffer is used as cache, based on offset 0, and no further reads
 * will be issued.
 */

void
gst_ebml_read_reset_cache (GstEbmlRead * ebml, GstBuffer * buffer,
    guint64 offset)
{
  if (ebml->cached_buffer)
    gst_buffer_unref (ebml->cached_buffer);

  ebml->cached_buffer = buffer;
  ebml->push_cache = TRUE;
  buffer = gst_buffer_make_metadata_writable (buffer);
  GST_BUFFER_OFFSET (buffer) = offset;
  ebml->offset = offset;
  g_list_foreach (ebml->level, (GFunc) gst_ebml_level_free, NULL);
  g_list_free (ebml->level);
  ebml->level = NULL;
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
    GstEbmlLevel *level = ebml->level->data;

    if (pos >= level->start + level->length) {
      ebml->level = g_list_delete_link (ebml->level, ebml->level);
      gst_ebml_level_free (level);
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
static GstFlowReturn
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
        (ebml->offset + size) <= (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_create_sub (ebml->cached_buffer,
            ebml->offset - cache_offset, size);
      if (bytes)
        *bytes =
            GST_BUFFER_DATA (ebml->cached_buffer) + ebml->offset - cache_offset;
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    /* never drop pushed cache */
    if (ebml->push_cache) {
      if (ebml->offset == cache_offset + cache_size)
        return GST_FLOW_END;
      else
        return GST_FLOW_UNEXPECTED;
    }
    gst_buffer_unref (ebml->cached_buffer);
    ebml->cached_buffer = NULL;
  }

  /* refill the cache */
  ret = gst_pad_pull_range (ebml->sinkpad, ebml->offset, MAX (size, 64 * 1024),
      &ebml->cached_buffer);
  if (ret != GST_FLOW_OK) {
    ebml->cached_buffer = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (ebml->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_create_sub (ebml->cached_buffer, 0, size);
    if (bytes)
      *bytes = GST_BUFFER_DATA (ebml->cached_buffer);
    return GST_FLOW_OK;
  }

  /* Not possible to get enough data, try a last time with
   * requesting exactly the size we need */
  gst_buffer_unref (ebml->cached_buffer);
  ebml->cached_buffer = NULL;

  ret =
      gst_pad_pull_range (ebml->sinkpad, ebml->offset, size,
      &ebml->cached_buffer);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (ebml, "pull_range returned %d", ret);
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (ebml->cached_buffer) < size) {
    GST_WARNING_OBJECT (ebml, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", ebml->offset,
        size, GST_BUFFER_SIZE (ebml->cached_buffer));

    gst_buffer_unref (ebml->cached_buffer);
    ebml->cached_buffer = NULL;
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return GST_FLOW_UNEXPECTED;
  }

  if (p_buf)
    *p_buf = gst_buffer_create_sub (ebml->cached_buffer, 0, size);
  if (bytes)
    *bytes = GST_BUFFER_DATA (*p_buf);

  return GST_FLOW_OK;
}

/*
 * Calls pull_range for (offset,size) and advances our offset by size
 */
static GstFlowReturn
gst_ebml_read_pull_bytes (GstEbmlRead * ebml, guint size, GstBuffer ** p_buf,
    guint8 ** bytes)
{
  GstFlowReturn ret;

  ret = gst_ebml_read_peek_bytes (ebml, size, p_buf, bytes);
  if (ret != GST_FLOW_OK)
    return ret;

  ebml->offset += size;
  return GST_FLOW_OK;
}

/*
 * Read: the element content data ID.
 * Return: FALSE on error.
 */

static GstFlowReturn
gst_ebml_read_element_id (GstEbmlRead * ebml, guint32 * id, guint * level_up)
{
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1;
  guint32 total;
  guint8 b;
  GstFlowReturn ret;

  ret = gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  b = GST_READ_UINT8 (buf);

  total = (guint32) b;

  while (read <= 4 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 4) {
    GST_ERROR_OBJECT (ebml,
        "Invalid EBML ID size tag (0x%x) at position %" G_GUINT64_FORMAT " (0x%"
        G_GINT64_MODIFIER "x)", (guint) b, ebml->offset, ebml->offset);
    return GST_FLOW_ERROR;
  }

  ret = gst_ebml_read_peek_bytes (ebml, read, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

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
  return GST_FLOW_OK;
}

/*
 * Read: element content length.
 * Return: the number of bytes read or -1 on error.
 */

static GstFlowReturn
gst_ebml_read_element_length (GstEbmlRead * ebml, guint64 * length,
    gint * rread)
{
  GstFlowReturn ret;
  guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;
  guint8 b;

  ret = gst_ebml_read_peek_bytes (ebml, 1, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  b = GST_READ_UINT8 (buf);

  total = (guint64) b;

  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8) {
    GST_ERROR_OBJECT (ebml,
        "Invalid EBML length size tag (0x%x) at position %" G_GUINT64_FORMAT
        " (0x%" G_GINT64_MODIFIER "x)", (guint) b, ebml->offset, ebml->offset);
    return GST_FLOW_ERROR;
  }

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;

  ret = gst_ebml_read_peek_bytes (ebml, read, NULL, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

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

  if (rread)
    *rread = read;

  ebml->offset += read;

  return GST_FLOW_OK;
}

/*
 * Return: the ID of the next element.
 * Level_up contains the amount of levels that this
 * next element lies higher than the previous one.
 */

GstFlowReturn
gst_ebml_peek_id (GstEbmlRead * ebml, guint * level_up, guint32 * id)
{
  guint64 off;
  guint level_up_tmp = 0;
  GstFlowReturn ret;

  g_assert (level_up);
  g_assert (id);

  *level_up = 0;

next:
  off = ebml->offset;           /* save offset */

  if ((ret = gst_ebml_read_element_id (ebml, id, &level_up_tmp)) != GST_FLOW_OK) {
    if (ret != GST_FLOW_END)
      return ret;
    else {
      /* simulate dummy VOID element,
       * and have the call stack bail out all the way */
      *id = GST_EBML_ID_VOID;
      *level_up = G_MAXUINT32 >> 2;
      return GST_FLOW_OK;
    }
  }

  ebml->offset = off;           /* restore offset */

  *level_up += level_up_tmp;
  level_up_tmp = 0;

  switch (*id) {
    case GST_EBML_ID_VOID:
      GST_DEBUG_OBJECT (ebml, "Skipping EBML Void element");
      if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
        return ret;
      goto next;
      break;
    case GST_EBML_ID_CRC32:
      GST_DEBUG_OBJECT (ebml, "Skipping EBML CRC32 element");
      if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
        return ret;
      goto next;
      break;
  }

  return ret;
}

/*
 * Return the length of the stream in bytes
 */

gint64
gst_ebml_read_get_length (GstEbmlRead * ebml)
{
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 end;

  /* FIXME: what to do if we don't get the upstream length */
  if (!gst_pad_query_peer_duration (ebml->sinkpad, &fmt, &end) ||
      fmt != GST_FORMAT_BYTES || end < 0)
    g_return_val_if_reached (0);

  return end;
}

/*
 * Seek to a given offset.
 */

GstFlowReturn
gst_ebml_read_seek (GstEbmlRead * ebml, guint64 offset)
{
  if (offset >= gst_ebml_read_get_length (ebml))
    return GST_FLOW_UNEXPECTED;

  ebml->offset = offset;

  return GST_FLOW_OK;
}

/*
 * Skip the next element.
 */

GstFlowReturn
gst_ebml_read_skip (GstEbmlRead * ebml)
{
  guint64 length;
  guint32 id;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, &id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ebml->offset += length;
  return ret;
}

/*
 * Read the next element as a GstBuffer (binary).
 */

GstFlowReturn
gst_ebml_read_buffer (GstEbmlRead * ebml, guint32 * id, GstBuffer ** buf)
{
  guint64 length;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (length == 0) {
    *buf = gst_buffer_new ();
    return GST_FLOW_OK;
  }

  *buf = NULL;
  ret = gst_ebml_read_pull_bytes (ebml, (guint) length, buf, NULL);

  return ret;
}

/*
 * Read the next element, return a pointer to it and its size.
 */

static GstFlowReturn
gst_ebml_read_bytes (GstEbmlRead * ebml, guint32 * id, guint8 ** data,
    guint * size)
{
  guint64 length;
  GstFlowReturn ret;

  *size = 0;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  if (length == 0) {
    *data = NULL;
    return ret;
  }

  *data = NULL;
  ret = gst_ebml_read_pull_bytes (ebml, (guint) length, NULL, data);
  if (ret != GST_FLOW_OK)
    return ret;

  *size = (guint) length;

  return ret;
}

/*
 * Read the next element as an unsigned int.
 */

GstFlowReturn
gst_ebml_read_uint (GstEbmlRead * ebml, guint32 * id, guint64 * num)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size < 1 || size > 8) {
    GST_ERROR_OBJECT (ebml,
        "Invalid integer element size %d at position %" G_GUINT64_FORMAT " (0x%"
        G_GINT64_MODIFIER "x)", size, ebml->offset - size, ebml->offset - size);
    return GST_FLOW_ERROR;
  }
  *num = 0;
  while (size > 0) {
    *num = (*num << 8) | *data;
    size--;
    data++;
  }

  return ret;
}

/*
 * Read the next element as a signed int.
 */

GstFlowReturn
gst_ebml_read_sint (GstEbmlRead * ebml, guint32 * id, gint64 * num)
{
  guint8 *data;
  guint size;
  gboolean negative = 0;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size < 1 || size > 8) {
    GST_ERROR_OBJECT (ebml,
        "Invalid integer element size %d at position %" G_GUINT64_FORMAT " (0x%"
        G_GINT64_MODIFIER "x)", size, ebml->offset - size, ebml->offset - size);
    return GST_FLOW_ERROR;
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

  return ret;
}

/* Convert 80 bit extended precision float in big endian format to double.
 * Code taken from libavutil/intfloat_readwrite.c from ffmpeg,
 * licensed under LGPL */

struct _ext_float
{
  guint8 exponent[2];
  guint8 mantissa[8];
};

static gdouble
_ext2dbl (guint8 * data)
{
  struct _ext_float ext;
  guint64 m = 0;
  gint e, i;

  memcpy (&ext.exponent, data, 2);
  memcpy (&ext.mantissa, data + 2, 8);

  for (i = 0; i < 8; i++)
    m = (m << 8) + ext.mantissa[i];
  e = (((gint) ext.exponent[0] & 0x7f) << 8) | ext.exponent[1];
  if (e == 0x7fff && m)
    return NAN;
  e -= 16383 + 63;              /* In IEEE 80 bits, the whole (i.e. 1.xxxx)
                                 * mantissa bit is written as opposed to the
                                 * single and double precision formats */
  if (ext.exponent[0] & 0x80)
    m = -m;
  return ldexp (m, e);
}

/*
 * Read the next element as a float.
 */

GstFlowReturn
gst_ebml_read_float (GstEbmlRead * ebml, guint32 * id, gdouble * num)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  if (size != 4 && size != 8 && size != 10) {
    GST_ERROR_OBJECT (ebml,
        "Invalid float element size %d at position %" G_GUINT64_FORMAT " (0x%"
        G_GINT64_MODIFIER "x)", size, ebml->offset - size, ebml->offset - size);
    return GST_FLOW_ERROR;
  }

  if (size == 4) {
    gfloat f;

    memcpy (&f, data, 4);
    f = GFLOAT_FROM_BE (f);

    *num = f;
  } else if (size == 8) {
    gdouble d;

    memcpy (&d, data, 8);
    d = GDOUBLE_FROM_BE (d);

    *num = d;
  } else {
    *num = _ext2dbl (data);
  }

  return ret;
}

/*
 * Read the next element as a C string.
 */

static GstFlowReturn
gst_ebml_read_string (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  *str = g_malloc (size + 1);
  memcpy (*str, data, size);
  (*str)[size] = '\0';

  return ret;
}

/*
 * Read the next element as an ASCII string.
 */

GstFlowReturn
gst_ebml_read_ascii (GstEbmlRead * ebml, guint32 * id, gchar ** str_out)
{
  GstFlowReturn ret;
  gchar *str;
  gchar *iter;

#ifndef GST_DISABLE_GST_DEBUG
  guint64 oldoff = ebml->offset;
#endif

  ret = gst_ebml_read_string (ebml, id, &str);
  if (ret != GST_FLOW_OK)
    return ret;

  for (iter = str; *iter != '\0'; iter++) {
    if (G_UNLIKELY (*iter & 0x80)) {
      GST_ERROR_OBJECT (ebml,
          "Invalid ASCII string at offset %" G_GUINT64_FORMAT, oldoff);
      g_free (str);
      return GST_FLOW_ERROR;
    }
  }

  *str_out = str;
  return ret;
}

/*
 * Read the next element as a UTF-8 string.
 */

GstFlowReturn
gst_ebml_read_utf8 (GstEbmlRead * ebml, guint32 * id, gchar ** str)
{
  GstFlowReturn ret;

#ifndef GST_DISABLE_GST_DEBUG
  guint64 oldoff = ebml->offset;
#endif

  ret = gst_ebml_read_string (ebml, id, str);
  if (ret != GST_FLOW_OK)
    return ret;

  if (str != NULL && *str != NULL && **str != '\0' &&
      !g_utf8_validate (*str, -1, NULL)) {
    GST_WARNING_OBJECT (ebml,
        "Invalid UTF-8 string at offset %" G_GUINT64_FORMAT, oldoff);
  }

  return ret;
}

/*
 * Read the next element as a date.
 * Returns the seconds since the unix epoch.
 */

GstFlowReturn
gst_ebml_read_date (GstEbmlRead * ebml, guint32 * id, gint64 * date)
{
  gint64 ebml_date;
  GstFlowReturn ret;

  ret = gst_ebml_read_sint (ebml, id, &ebml_date);
  if (ret != GST_FLOW_OK)
    return ret;

  *date = (ebml_date / GST_SECOND) + GST_EBML_DATE_OFFSET;

  return ret;
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 */

GstFlowReturn
gst_ebml_read_master (GstEbmlRead * ebml, guint32 * id)
{
  GstEbmlLevel *level;
  guint64 length;
  GstFlowReturn ret;

  ret = gst_ebml_read_element_id (ebml, id, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_ebml_read_element_length (ebml, &length, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  /* remember level */
  level = g_slice_new (GstEbmlLevel);
  level->start = ebml->offset;
  level->length = length;
  ebml->level = g_list_prepend (ebml->level, level);

  return GST_FLOW_OK;
}

/*
 * Read the next element as binary data.
 */

GstFlowReturn
gst_ebml_read_binary (GstEbmlRead * ebml,
    guint32 * id, guint8 ** binary, guint64 * length)
{
  guint8 *data;
  guint size;
  GstFlowReturn ret;

  ret = gst_ebml_read_bytes (ebml, id, &data, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  *length = size;
  *binary = g_memdup (data, size);

  return GST_FLOW_OK;
}

/*
 * Read an EBML header.
 */

GstFlowReturn
gst_ebml_read_header (GstEbmlRead * ebml, gchar ** doctype, guint * version)
{
  /* this function is the first to be called */
  guint32 id;
  guint level_up;
  GstFlowReturn ret;

  /* default init */
  if (doctype)
    *doctype = NULL;
  if (version)
    *version = 1;

  ret = gst_ebml_peek_id (ebml, &level_up, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_DEBUG_OBJECT (ebml, "id: %08x", GST_READ_UINT32_BE (&id));

  if (level_up != 0 || id != GST_EBML_ID_HEADER) {
    GST_ERROR_OBJECT (ebml, "Failed to read header");
    return GST_FLOW_ERROR;
  }
  ret = gst_ebml_read_master (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  while (TRUE) {
    ret = gst_ebml_peek_id (ebml, &level_up, &id);
    if (ret != GST_FLOW_OK)
      return ret;

    /* end-of-header */
    if (level_up)
      break;

    switch (id) {
        /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLREADVERSION);
        if (num != GST_EBML_VERSION) {
          GST_ERROR_OBJECT (ebml, "Unsupported EBML version %" G_GUINT64_FORMAT,
              num);
          return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLMAXSIZELENGTH);
        if (num > sizeof (guint64)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum size %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxSizeLength: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_EBMLMAXIDLENGTH);
        if (num > sizeof (guint32)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum ID %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxIdLength: %" G_GUINT64_FORMAT, num);
        break;
      }

      case GST_EBML_ID_DOCTYPE:{
        gchar *text;

        ret = gst_ebml_read_ascii (ebml, &id, &text);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_DOCTYPE);

        GST_DEBUG_OBJECT (ebml, "EbmlDocType: %s", GST_STR_NULL (text));

        if (doctype) {
          g_free (*doctype);
          *doctype = text;
        } else
          g_free (text);
        break;
      }

      case GST_EBML_ID_DOCTYPEREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        g_assert (id == GST_EBML_ID_DOCTYPEREADVERSION);
        if (version)
          *version = num;
        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

      default:
        GST_WARNING_OBJECT (ebml,
            "Unknown data type 0x%x in EBML header (ignored)", id);
        /* pass-through */

        /* we ignore these two, as they don't tell us anything we care about */
      case GST_EBML_ID_EBMLVERSION:
      case GST_EBML_ID_DOCTYPEVERSION:
        ret = gst_ebml_read_skip (ebml);
        if (ret != GST_FLOW_OK)
          return ret;
        break;
    }
  }

  return GST_FLOW_OK;
}
