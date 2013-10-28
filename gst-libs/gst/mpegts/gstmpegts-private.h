/*
 * mpegts.h -
 * Copyright (C) 2013 Edward Hervey
 *
 * Authors:
 *   Edward Hervey <edward@collabora.com>
 *
 * This library is free softwagre; you can redistribute it and/or
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_MPEGTS_PRIVATE_H_
#define _GST_MPEGTS_PRIVATE_H_

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mpegts_debug);
#define GST_CAT_DEFAULT gst_mpegts_debug

G_GNUC_INTERNAL void __initialize_descriptors (void);
G_GNUC_INTERNAL guint32 _calc_crc32 (const guint8 *data, guint datalen);
G_GNUC_INTERNAL gchar *get_encoding_and_convert (const gchar *text, guint length);
G_GNUC_INTERNAL guint8 *dvb_text_from_utf8 (const gchar * text, gsize *out_size);
G_GNUC_INTERNAL void _free_descriptor (GstMpegTsDescriptor *descriptor);
G_GNUC_INTERNAL GstMpegTsDescriptor *_new_descriptor (guint8 tag, guint8 length);
G_GNUC_INTERNAL GstMpegTsDescriptor *_new_descriptor_with_extension (guint8 tag,
    guint8 tag_extension, guint8 length);
G_GNUC_INTERNAL void _packetize_descriptor_array (GPtrArray * array,
    guint8 ** out_data);
G_GNUC_INTERNAL GstMpegTsSection *_gst_mpegts_section_init (guint16 pid, guint8 table_id);
G_GNUC_INTERNAL void _packetize_common_section (GstMpegTsSection * section, gsize length);

typedef gpointer (*GstMpegTsParseFunc) (GstMpegTsSection *section);
G_GNUC_INTERNAL gpointer __common_desc_checks (GstMpegTsSection *section,
					       guint minsize,
					       GstMpegTsParseFunc parsefunc,
					       GDestroyNotify destroynotify);

G_END_DECLS

#endif	/* _GST_MPEGTS_PRIVATE_H_ */
