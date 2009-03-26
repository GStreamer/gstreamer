/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Handling of the basic MXF types */

#ifndef __MXF_WRITE_H__
#define __MXF_WRITE_H__

#include <string.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "mxfmetadata.h"
#include "mxftypes.h"
#include "mxfparse.h"

typedef GstFlowReturn (*MXFEssenceElementWriteFunc) (GstBuffer *buffer, GstCaps *caps, gpointer mapping_data, GstAdapter *adapter, GstBuffer **outbuf, gboolean flush);

typedef struct {
   MXFMetadataFileDescriptor * (*get_descriptor) (GstPadTemplate *tmpl, GstCaps *caps, MXFEssenceElementWriteFunc *handler, gpointer *mapping_data);
   void (*update_descriptor) (MXFMetadataFileDescriptor *d, GstCaps *caps, gpointer mapping_data, GstBuffer *buf);
   void (*get_edit_rate) (MXFMetadataFileDescriptor *a, GstCaps *caps, gpointer mapping_data, GstBuffer *buf, MXFMetadataSourcePackage *package, MXFMetadataTimelineTrack *track, MXFFraction *edit_rate);
   guint32 (*get_track_number_template) (MXFMetadataFileDescriptor *a, GstCaps *caps, gpointer mapping_data);
   const GstPadTemplate *pad_template;
   MXFUL data_definition;
} MXFEssenceElementWriter;

typedef enum {
  MXF_OP_UNKNOWN = 0,
  MXF_OP_ATOM,
  MXF_OP_1a,
  MXF_OP_1b,
  MXF_OP_1c,
  MXF_OP_2a,
  MXF_OP_2b,
  MXF_OP_2c,
  MXF_OP_3a,
  MXF_OP_3b,
  MXF_OP_3c,
} MXFOperationalPattern;

void mxf_essence_element_writer_register (const MXFEssenceElementWriter *writer);
const GstPadTemplate ** mxf_essence_element_writer_get_pad_templates (void);
const MXFEssenceElementWriter *mxf_essence_element_writer_find (const GstPadTemplate *templ);

void mxf_ul_set (MXFUL *ul, GHashTable *hashtable);
void mxf_umid_set (MXFUMID *umid);

void mxf_timestamp_set_now (MXFTimestamp *timestamp);
void mxf_timestamp_write (const MXFTimestamp *timestamp, guint8 *data);

void mxf_op_set_atom (MXFUL *ul, gboolean single_sourceclip, gboolean single_essence_track);
void mxf_op_set_generalized (MXFUL *ul, MXFOperationalPattern pattern, gboolean internal_essence, gboolean streamable, gboolean single_track);

guint16 mxf_primer_pack_add_mapping (MXFPrimerPack *primer, guint16 local_tag, const MXFUL *ul);

guint mxf_ber_encode_size (guint size, guint8 ber[9]);

guint8 * mxf_utf8_to_utf16 (const gchar *str, guint16 *size);

void mxf_product_version_write (const MXFProductVersion *version, guint8 *data);

GstBuffer * mxf_partition_pack_to_buffer (const MXFPartitionPack *pack);
GstBuffer * mxf_primer_pack_to_buffer (const MXFPrimerPack *pack);
GstBuffer * mxf_fill_new (guint size);

GstBuffer * mxf_random_index_pack_to_buffer (const GArray *array);

#endif /* __MXF_WRITE_H__ */
