/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __MXF_UL_H__
#define __MXF_UL_H__

#include <gst/gst.h>

/* SMPTE 377M 3.2 */
typedef struct {
  guint8 u[16];
} MXFUL;

typedef enum {
  MXF_UL_SMPTE = 0,
  MXF_UL_FILL,
  MXF_UL_PARTITION_PACK,
  MXF_UL_PRIMER_PACK,
  MXF_UL_METADATA,
  MXF_UL_DESCRIPTIVE_METADATA,
  MXF_UL_RANDOM_INDEX_PACK,
  MXF_UL_INDEX_TABLE_SEGMENT,
  MXF_UL_GENERIC_CONTAINER_SYSTEM_ITEM,
  MXF_UL_GENERIC_CONTAINER_ESSENCE_ELEMENT,
  MXF_UL_GENERIC_CONTAINER_ESSENCE_CONTAINER_LABEL,
  MXF_UL_AVID_ESSENCE_CONTAINER_ESSENCE_ELEMENT,
  MXF_UL_AVID_ESSENCE_CONTAINER_ESSENCE_LABEL,
  MXF_UL_OPERATIONAL_PATTERN_IDENTIFICATION,
  MXF_UL_MAX
} MXFULId;

extern const MXFUL _mxf_ul_table[MXF_UL_MAX];

#define MXF_UL(id) (&_mxf_ul_table[MXF_UL_##id])

gboolean mxf_ul_is_equal (const MXFUL *a, const MXFUL *b);
gboolean mxf_ul_is_subclass (const MXFUL *class, const MXFUL *subclass);
gboolean mxf_ul_is_zero (const MXFUL *ul);
gboolean mxf_ul_is_valid (const MXFUL *ul);
guint mxf_ul_hash (const MXFUL *ul);

gchar * mxf_ul_to_string (const MXFUL *ul, gchar str[48]);
MXFUL * mxf_ul_from_string (const gchar *str, MXFUL *ul);

gboolean mxf_ul_array_parse (MXFUL **array, guint32 *count, const guint8 *data, guint size);

#endif /* __MXF_UL_H__ */
