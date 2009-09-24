/* GStreamer QuickTime atom parser
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
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

#ifndef QT_ATOM_PARSER_H
#define QT_ATOM_PARSER_H

#include <gst/base/gstbytereader.h>

/* our inlined version of GstByteReader */

typedef GstByteReader QtAtomParser;

#define qt_atom_parser_init gst_byte_reader_init
#define qt_atom_parser_get_remaining gst_byte_reader_get_remaining
#define qt_atom_parser_has_remaining gst_byte_reader_has_remaining

static inline gboolean
qt_atom_parser_has_chunks (QtAtomParser * parser, guint32 n_chunks,
    guint32 chunk_size)
{
  /* assumption: n_chunks and chunk_size are 32-bit, we cast to 64-bit here
   * to avoid overflows, to handle e.g. (guint32)-1 * size correctly */
  return qt_atom_parser_has_remaining (parser, (guint64) n_chunks * chunk_size);
}

#define qt_atom_parser_skip gst_byte_reader_skip
#define qt_atom_parser_skip_unchecked gst_byte_reader_skip_unchecked

#define qt_atom_parser_get_uint8  gst_byte_reader_get_uint8
#define qt_atom_parser_get_uint16 gst_byte_reader_get_uint16_be
#define qt_atom_parser_get_uint24 gst_byte_reader_get_uint24_be
#define qt_atom_parser_get_uint32 gst_byte_reader_get_uint32_be
#define qt_atom_parser_get_uint64 gst_byte_reader_get_uint64_be

#define qt_atom_parser_peek_uint8  gst_byte_reader_peek_uint8
#define qt_atom_parser_peek_uint16 gst_byte_reader_peek_uint16_be
#define qt_atom_parser_peek_uint24 gst_byte_reader_peek_uint24_be
#define qt_atom_parser_peek_uint32 gst_byte_reader_peek_uint32_be
#define qt_atom_parser_peek_uint64 gst_byte_reader_peek_uint64_be

#define qt_atom_parser_get_uint8_unchecked  gst_byte_reader_get_uint8_unchecked
#define qt_atom_parser_get_uint16_unchecked gst_byte_reader_get_uint16_be_unchecked
#define qt_atom_parser_get_uint24_unchecked gst_byte_reader_get_uint24_be_unchecked
#define qt_atom_parser_get_uint32_unchecked gst_byte_reader_get_uint32_be_unchecked
#define qt_atom_parser_get_uint64_unchecked gst_byte_reader_get_uint64_be_unchecked

#define qt_atom_parser_peek_uint8_unchecked  gst_byte_reader_peek_uint8_unchecked
#define qt_atom_parser_peek_uint16_unchecked gst_byte_reader_peek_uint16_be_unchecked
#define qt_atom_parser_peek_uint24_unchecked gst_byte_reader_peek_uint24_be_unchecked
#define qt_atom_parser_peek_uint32_unchecked gst_byte_reader_peek_uint32_be_unchecked
#define qt_atom_parser_peek_uint64_unchecked gst_byte_reader_peek_uint64_be_unchecked

#define qt_atom_parser_peek_bytes_unchecked gst_byte_reader_peek_data_unchecked

static inline gboolean
qt_atom_parser_peek_sub (QtAtomParser * parser, guint offset, guint size,
    QtAtomParser * sub)
{
  *sub = *parser;

  if (G_UNLIKELY (!qt_atom_parser_skip (sub, offset)))
    return FALSE;

  return (qt_atom_parser_get_remaining (sub) >= size);
}

static inline gboolean
qt_atom_parser_skipn_and_get_uint32 (QtAtomParser * parser,
    guint bytes_to_skip, guint32 * val)
{
  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < (bytes_to_skip + 4)))
    return FALSE;

  qt_atom_parser_skip_unchecked (parser, bytes_to_skip);
  *val = qt_atom_parser_get_uint32_unchecked (parser);
  return TRUE;
}

/* off_size must be either 4 or 8 */
static inline gboolean
qt_atom_parser_get_offset (QtAtomParser * parser, guint off_size, guint64 * val)
{
  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < off_size))
    return FALSE;

  if (off_size == sizeof (guint64)) {
    *val = qt_atom_parser_get_uint64_unchecked (parser);
  } else {
    *val = qt_atom_parser_get_uint32_unchecked (parser);
  }
  return TRUE;
}

/* off_size must be either 4 or 8 */
static inline guint64
qt_atom_parser_get_offset_unchecked (QtAtomParser * parser, guint off_size)
{
  if (off_size == sizeof (guint64)) {
    return qt_atom_parser_get_uint64_unchecked (parser);
  } else {
    return qt_atom_parser_get_uint32_unchecked (parser);
  }
}

static inline gboolean
qt_atom_parser_get_fourcc (QtAtomParser * parser, guint32 * fourcc)
{
  guint32 f_be;

  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < 4))
    return FALSE;

  f_be = qt_atom_parser_get_uint32_unchecked (parser);
  *fourcc = GUINT32_SWAP_LE_BE (f_be);
  return TRUE;
}

static inline guint32
qt_atom_parser_get_fourcc_unchecked (QtAtomParser * parser)
{
  guint32 fourcc;

  fourcc = qt_atom_parser_get_uint32_unchecked (parser);
  return GUINT32_SWAP_LE_BE (fourcc);
}

#endif /* QT_ATOM_PARSER_H */
