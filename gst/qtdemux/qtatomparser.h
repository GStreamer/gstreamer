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

static inline void
qt_atom_parser_init (QtAtomParser * parser, const guint8 * data, guint size)
{
  gst_byte_reader_init (parser, data, size);
}

static inline guint
qt_atom_parser_get_remaining (QtAtomParser * parser)
{
  return parser->size - parser->byte;
}

static inline gboolean
qt_atom_parser_skip (QtAtomParser * parser, guint nbytes)
{
  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < nbytes))
    return FALSE;

  parser->byte += nbytes;
  return TRUE;
}

static inline void
qt_atom_parser_skip_unchecked (QtAtomParser * parser, guint nbytes)
{
  parser->byte += nbytes;
}

#ifndef GST_READ_UINT8_BE
#define GST_READ_UINT8_BE GST_READ_UINT8
#endif

#define _QT_ATOM_PARSER_GET_PEEK_BITS(bits,typebits) \
static inline gboolean \
qt_atom_parser_get_uint##bits (QtAtomParser * parser, guint##typebits * val)  \
{ \
  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < (bits / 8))) \
    return FALSE; \
  *val = GST_READ_UINT##bits##_BE (parser->data + parser->byte);  \
  parser->byte += bits / 8; \
  return TRUE; \
} \
\
static inline gboolean \
qt_atom_parser_peek_uint##bits (QtAtomParser * parser, guint##typebits * val)  \
{ \
  if (G_UNLIKELY (qt_atom_parser_get_remaining (parser) < (bits / 8))) \
    return FALSE; \
  *val = GST_READ_UINT##bits##_BE (parser->data + parser->byte);  \
  return TRUE; \
} \
\
static inline guint##typebits \
qt_atom_parser_get_uint##bits##_unchecked (QtAtomParser * parser)  \
{ \
  guint##typebits val = GST_READ_UINT##bits##_BE (parser->data + parser->byte);  \
  parser->byte += bits / 8; \
  return val; \
} \
\
static inline guint##typebits \
qt_atom_parser_peek_uint##bits##_unchecked (QtAtomParser * parser)  \
{ \
  return GST_READ_UINT##bits##_BE (parser->data + parser->byte);  \
}

_QT_ATOM_PARSER_GET_PEEK_BITS(8,8);
_QT_ATOM_PARSER_GET_PEEK_BITS(16,16);
_QT_ATOM_PARSER_GET_PEEK_BITS(24,32);
_QT_ATOM_PARSER_GET_PEEK_BITS(32,32);
_QT_ATOM_PARSER_GET_PEEK_BITS(64,64);

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

#endif /* QT_ATOM_PARSER_H */
