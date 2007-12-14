/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:metadatamux-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatamux ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

/*
 * static functions declaration
 */

#include "gstmetadatacommon.h"

/*
 * offset - offset of buffer in original stream
 * size - size of buffer
 * seg_offset - offset of segment in original stream
 * seg_size - size of segment
 * boffset - offset inside buffer where segment starts (-1 for no intersection)
 * bsize - size of intersection
 * seg_binter - if segment start inside buffer is zero. if segment start before
 *               buffer and intersect, it is the offset inside segment.
 *
 * ret:
 *  -1 - segment before buffer
 *   0 - segment intersects
 *   1 - segment after buffer
 */

static int
gst_metadata_common_get_strip_seg (const gint64 offset, guint32 size,
    const gint64 seg_offset, const guint32 seg_size,
    gint64 * boffset, guint32 * bsize, guint32 * seg_binter)
{
  int ret = -1;

  *boffset = -1;
  *bsize = 0;
  *seg_binter = -1;

  /* all segment after buffer */
  if (seg_offset >= offset + size) {
    ret = 1;
    goto done;
  }

  if (seg_offset < offset) {
    /* segment start somewhere before buffer */

    /* all segment before buffer */
    if (seg_offset + seg_size <= offset) {
      ret = -1;
      goto done;
    }

    *seg_binter = offset - seg_offset;
    *boffset = 0;

    /* FIXME : optimize to >= size -> = size */
    if (seg_offset + seg_size >= offset + size) {
      /* segment cover all buffer */
      *bsize = size;
    } else {
      /* segment goes from start of buffer to somewhere before end */
      *bsize = seg_size - *seg_binter;
    }

    ret = 0;

  } else {
    /* segment start somewhere into buffer */

    *boffset = seg_offset - offset;
    *seg_binter = 0;

    if (seg_offset + seg_size <= offset + size) {
      /* all segment into buffer */
      *bsize = seg_size;
    } else {
      *bsize = size - *boffset;
    }

    ret = 0;

  }

done:

  return ret;

}

/*
 * extern functions declaration
 */

void
gst_metadata_common_init (GstMetadataCommon * common, gboolean parse,
    guint8 options)
{
  metadata_init (&common->metadata, parse, options);
}

void
gst_metadata_common_dispose (GstMetadataCommon * common)
{
  if (common->append_buffer) {
    gst_buffer_unref (common->append_buffer);
    common->append_buffer = NULL;
  }
  metadata_dispose (&common->metadata);
}

/*

/*
 *  TRUE -> buffer striped or injeted
 *  FALSE -> buffer unmodified
 */

gboolean
gst_metadata_common_strip_push_buffer (GstMetadataCommon * common,
    gint64 offset_orig, GstBuffer ** prepend, GstBuffer ** buf)
{
  MetadataChunk *strip = common->metadata.strip_chunks.chunk;
  MetadataChunk *inject = common->metadata.inject_chunks.chunk;
  const gsize strip_len = common->metadata.strip_chunks.len;
  const gsize inject_len = common->metadata.inject_chunks.len;

  gboolean buffer_reallocated = FALSE;

  guint32 size_buf_in = GST_BUFFER_SIZE (*buf);

  gint64 *boffset_strip = NULL;
  guint32 *bsize_strip = NULL;
  guint32 *seg_binter_strip = NULL;

  int i, j;
  gboolean need_free_strip = FALSE;

  guint32 striped_bytes = 0;
  guint32 injected_bytes = 0;

  guint32 prepend_size = prepend && *prepend ? GST_BUFFER_SIZE (*prepend) : 0;

  if (inject_len) {

    for (i = 0; i < inject_len; ++i) {
      int res;

      if (inject[i].offset_orig >= offset_orig) {
        if (inject[i].offset_orig < offset_orig + size_buf_in) {
          injected_bytes += inject[i].size;
        } else {
          /* segment is after size (segments are sorted) */
          break;
        }
      }
    }

  }

  /*
   * strip segments
   */

  if (strip_len == 0)
    goto inject;

  if (G_UNLIKELY (strip_len > 16)) {
    boffset_strip = g_new (gint64, strip_len);
    bsize_strip = g_new (guint32, strip_len);
    seg_binter_strip = g_new (guint32, strip_len);
    need_free_strip = TRUE;
  } else {
    boffset_strip = g_alloca (sizeof (boffset_strip[0]) * strip_len);
    bsize_strip = g_alloca (sizeof (bsize_strip[0]) * strip_len);
    seg_binter_strip = g_alloca (sizeof (seg_binter_strip[0]) * strip_len);
  }

  memset (bsize_strip, 0x00, sizeof (bsize_strip[0]) * strip_len);

  for (i = 0; i < strip_len; ++i) {
    int res;

    res = gst_metadata_common_get_strip_seg (offset_orig, size_buf_in,
        strip[i].offset_orig, strip[i].size, &boffset_strip[i], &bsize_strip[i],
        &seg_binter_strip[i]);

    /* segment is after size (segments are sorted) */
    striped_bytes += bsize_strip[i];
    if (res > 0) {
      break;
    }

  }

  if (striped_bytes) {

    guint8 *data;

    if (!buffer_reallocated) {
      buffer_reallocated = TRUE;
      if (injected_bytes + prepend_size > striped_bytes) {
        GstBuffer *new_buf =
            gst_buffer_new_and_alloc (GST_BUFFER_SIZE (*buf) + injected_bytes +
            prepend_size - striped_bytes);

        memcpy (GST_BUFFER_DATA (new_buf), GST_BUFFER_DATA (*buf),
            GST_BUFFER_SIZE (*buf));

        gst_buffer_unref (*buf);
        *buf = new_buf;

      } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY)) {
        GstBuffer *new_buf = gst_buffer_copy (*buf);

        gst_buffer_unref (*buf);
        *buf = new_buf;
        GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
        GST_BUFFER_SIZE (*buf) += injected_bytes + prepend_size - striped_bytes;
      }
    }

    data = GST_BUFFER_DATA (*buf);

    striped_bytes = 0;
    for (i = 0; i < strip_len; ++i) {
      /* intersect */
      if (bsize_strip[i]) {
        memmove (data + boffset_strip[i] - striped_bytes,
            data + boffset_strip[i] + bsize_strip[i] - striped_bytes,
            size_buf_in - boffset_strip[i] - bsize_strip[i]);
        striped_bytes += bsize_strip[i];
      }
    }
    size_buf_in -= striped_bytes;

  }

inject:

  /*
   * inject segments
   */

  if (inject_len) {

    guint8 *data;
    guint32 striped_so_far;

    if (!buffer_reallocated) {
      buffer_reallocated = TRUE;
      if (injected_bytes + prepend_size > striped_bytes) {
        GstBuffer *new_buf =
            gst_buffer_new_and_alloc (GST_BUFFER_SIZE (*buf) + injected_bytes +
            prepend_size - striped_bytes);

        memcpy (GST_BUFFER_DATA (new_buf), GST_BUFFER_DATA (*buf),
            GST_BUFFER_SIZE (*buf));

        gst_buffer_unref (*buf);
        *buf = new_buf;

      } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY)) {
        GstBuffer *new_buf = gst_buffer_copy (*buf);

        gst_buffer_unref (*buf);
        *buf = new_buf;
        GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
        GST_BUFFER_SIZE (*buf) += injected_bytes + prepend_size - striped_bytes;
      }
    }

    data = GST_BUFFER_DATA (*buf);

    injected_bytes = 0;
    striped_so_far = 0;
    j = 0;
    for (i = 0; i < inject_len; ++i) {
      int res;

      while (j < strip_len) {
        if (strip[j].offset_orig < inject[i].offset_orig)
          striped_so_far += bsize_strip[j++];
        else
          break;
      }

      if (inject[i].offset_orig >= offset_orig) {
        if (inject[i].offset_orig <
            offset_orig + size_buf_in + striped_bytes - injected_bytes) {
          /* insert */
          guint32 buf_off =
              inject[i].offset_orig - offset_orig - striped_so_far +
              injected_bytes;
          memmove (data + buf_off + inject[i].size, data + buf_off,
              size_buf_in - buf_off);
          memcpy (data + buf_off, inject[i].data, inject[i].size);
          injected_bytes += inject[i].size;
          size_buf_in += inject[i].size;
        } else {
          /* segment is after size (segments are sorted) */
          break;
        }
      }
    }

  }


done:

  if (prepend_size) {
    if (injected_bytes == 0 && striped_bytes == 0) {
      GstBuffer *new_buf =
          gst_buffer_new_and_alloc (size_buf_in + prepend_size);

      memcpy (GST_BUFFER_DATA (new_buf) + prepend_size, GST_BUFFER_DATA (*buf),
          size_buf_in);

      gst_buffer_unref (*buf);
      *buf = new_buf;
    } else {
      memmove (GST_BUFFER_DATA (*buf) + prepend_size, GST_BUFFER_DATA (*buf),
          size_buf_in);
    }
    memcpy (GST_BUFFER_DATA (*buf), GST_BUFFER_DATA (*prepend), prepend_size);
    gst_buffer_unref (*prepend);
    *prepend = NULL;
  }

  GST_BUFFER_SIZE (*buf) = size_buf_in + prepend_size;

  if (need_free_strip) {
    g_free (boffset_strip);
    g_free (bsize_strip);
    g_free (seg_binter_strip);
  }

  return injected_bytes || striped_bytes;

}

/*
 * pos - position in stream striped
 * orig_pos - position in original stream
 * return TRUE - position in original buffer
 *        FALSE - position in inserted chunk
 */
gboolean
gst_metadata_common_translate_pos_to_orig (GstMetadataCommon * common,
    gint64 pos, gint64 * orig_pos, GstBuffer ** buf)
{
  MetadataChunk *strip = common->metadata.strip_chunks.chunk;
  MetadataChunk *inject = common->metadata.inject_chunks.chunk;
  const gsize strip_len = common->metadata.strip_chunks.len;
  const gsize inject_len = common->metadata.inject_chunks.len;
  const gint64 duration_orig = common->duration_orig;
  const gint64 duration = common->duration;

  int i;
  gboolean ret = TRUE;
  guint64 new_buf_size = 0;
  guint64 injected_before = 0;

  if (G_UNLIKELY (pos == -1)) {
    *orig_pos = -1;
    return TRUE;
  } else if (G_UNLIKELY (pos >= duration)) {
    /* this should never happen */
    *orig_pos = duration_orig;
    return TRUE;
  }

  /* calculate for injected */

  /* just calculate size */
  *orig_pos = pos;              /* save pos */
  for (i = 0; i < inject_len; ++i) {
    /* check if pos in inside chunk */
    if (inject[i].offset <= pos) {
      if (pos < inject[i].offset + inject[i].size) {
        /* orig pos points after insert chunk */
        new_buf_size += inject[i].size;
        /* put pos after current chunk */
        pos = inject[i].offset + inject[i].size;
        ret = FALSE;
      } else {
        /* in case pos is not inside a injected chunk */
        injected_before += inject[i].size;
      }
    } else {
      break;
    }
  }

  /* alloc buffer and calcute original pos */
  if (buf && ret == FALSE) {
    guint8 *data;

    if (*buf)
      gst_buffer_unref (*buf);
    *buf = gst_buffer_new_and_alloc (new_buf_size);
    data = GST_BUFFER_DATA (*buf);
    pos = *orig_pos;            /* recover saved pos */
    for (i = 0; i < inject_len; ++i) {
      if (inject[i].offset > pos) {
        break;
      }
      if (inject[i].offset <= pos && pos < inject[i].offset + inject[i].size) {
        memcpy (data, inject[i].data, inject[i].size);
        data += inject[i].size;
        pos = inject[i].offset + inject[i].size;
        /* out position after insert chunk orig */
        *orig_pos = inject[i].offset_orig + inject[i].size;
      }
    }
  }

  if (ret == FALSE) {
    /* if it inside a injected is already done */
    goto done;
  }

  /* calculate for striped */

  *orig_pos = pos - injected_before;
  for (i = 0; i < strip_len; ++i) {
    if (strip[i].offset_orig > pos) {
      break;
    }
    *orig_pos += strip[i].size;
  }

done:

  if (G_UNLIKELY (*orig_pos >= duration_orig)) {
    *orig_pos = duration_orig - 1;
  }

  return ret;

}

/*
 * return:
 *   -1 -> error
 *    0 -> succeded
 *    1 -> need more data
 */

gboolean
gst_metadata_common_calculate_offsets (GstMetadataCommon * common)
{
  int i, j;
  guint32 append_size;
  guint32 bytes_striped, bytes_inject;
  MetadataChunk *strip = common->metadata.strip_chunks.chunk;
  MetadataChunk *inject = common->metadata.inject_chunks.chunk;
  gsize strip_len;
  gsize inject_len;

  if (common->state != MT_STATE_PARSED)
    return FALSE;

  metadata_lazy_update (&common->metadata);

  strip_len = common->metadata.strip_chunks.len;
  inject_len = common->metadata.inject_chunks.len;

  bytes_striped = 0;
  bytes_inject = 0;

  /* calculate the new position off injected chunks */
  j = 0;
  for (i = 0; i < inject_len; ++i) {
    for (; j < strip_len; ++j) {
      if (strip[j].offset_orig >= inject[i].offset_orig) {
        break;
      }
      bytes_striped += strip[j].size;
    }
    inject[i].offset = inject[i].offset_orig - bytes_striped + bytes_inject;
    bytes_inject += inject[i].size;
  }

  /* calculate append (doesnt make much sense, but, anyway..) */
  append_size = 0;
  for (i = inject_len - 1; i >= 0; --i) {
    if (inject[i].offset_orig == common->duration_orig)
      append_size += inject[i].size;
    else
      break;
  }
  if (append_size) {
    guint8 *data;

    common->append_buffer = gst_buffer_new_and_alloc (append_size);
    GST_BUFFER_FLAG_SET (common->append_buffer, GST_BUFFER_FLAG_READONLY);
    data = GST_BUFFER_DATA (common->append_buffer);
    for (i = inject_len - 1; i >= 0; --i) {
      if (inject[i].offset_orig == common->duration_orig) {
        memcpy (data, inject[i].data, inject[i].size);
        data += inject[i].size;
      } else {
        break;
      }
    }
  }

  if (common->duration_orig) {
    common->duration = common->duration_orig;
    for (i = 0; i < inject_len; ++i) {
      common->duration += inject[i].size;
    }
    for (i = 0; i < strip_len; ++i) {
      common->duration -= strip[i].size;
    }
  }

  return TRUE;

}

void
gst_metadata_common_update_segment_with_new_buffer (GstMetadataCommon * common,
    guint8 ** buf, guint32 * size, MetadataChunkType type)
{
  int i;
  MetadataChunk *inject = common->metadata.inject_chunks.chunk;
  const gsize inject_len = common->metadata.inject_chunks.len;

  if (!(buf && size))
    goto done;
  if (*buf == 0)
    goto done;
  if (*size == 0)
    goto done;

  for (i = 0; i < inject_len; ++i) {
    if (inject[i].type == type) {
      inject[i].size = *size;
      if (inject[i].data)
        g_free (inject[i].data);
      inject[i].data = *buf;
      *size = 0;
      *buf = 0;
      break;
    }
  }

done:

  return;

}

const gchar *
gst_metadata_common_get_type_name (int img_type)
{
  gchar *type_name = NULL;

  switch (img_type) {
    case IMG_JPEG:
      type_name = "jpeg";
      break;
    case IMG_PNG:
      type_name = "png";
      break;
    default:
      type_name = "invalid type";
      break;
  }
  return type_name;
}
