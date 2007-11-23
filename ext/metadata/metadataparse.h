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

#ifndef __METADATAPARSE_H__
#define __METADATAPARSE_H__

#include <gst/base/gstadapter.h>
#include "metadatatypes.h"

#include "metadataparsejpeg.h"
#include "metadataparsepng.h"

G_BEGIN_DECLS

typedef enum _tag_ParseOption
{
  PARSE_OPT_EXIF = (1 << 0),
  PARSE_OPT_IPTC = (1 << 1),
  PARSE_OPT_XMP = (1 << 2),
  PARSE_OPT_ALL = (1 << 3) - 1
} ParseOption;

typedef enum _tag_ParseState
{
  STATE_NULL,
  STATE_READING,
  STATE_DONE
} ParseState;

typedef enum _tag_ImageType
{
  IMG_NONE,
  IMG_JPEG,
  IMG_PNG
} ImageType;

typedef struct _tag_ParseData
{
  ParseState state;
  ImageType img_type;
  ParseOption option;
  guint32 offset_orig; /* offset since begining of stream */
  union
  {
    JpegData jpeg;
    PngData png;
  } format_data;
  GstAdapter * exif_adapter;
  GstAdapter * iptc_adapter;
  GstAdapter * xmp_adapter;

  MetadataChunkArray strip_chunks;
  MetadataChunkArray inject_chunks;

} ParseData;

#define PARSE_DATA_IMG_TYPE(p) (p).img_type
#define PARSE_DATA_OPTION(p) (p).option
#define set_parse_option(p, m) do { (p).option = (p).option | (m); } while(FALSE)
#define unset_parse_option(p, m) do { (p).option = (p).option & ~(m); } while(FALSE)

extern void metadataparse_init (ParseData * parse_data);

/*
 * offset: number of bytes that MUST be jumped after current "buf" pointer
 * next_size: number of minimum amount of bytes required on next step.
 *            if less than this is provided, the return will be 1 for sure.
 *            and the offset will be 0 (zero)
 * return:
 *   -1 -> error
 *    0 -> done
 *    1 -> need more data
 */
extern int
metadataparse_parse (ParseData * parse_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size);


extern void metadataparse_dispose (ParseData * parse_data);

G_END_DECLS
#endif /* __METADATAPARSE_H__ */
