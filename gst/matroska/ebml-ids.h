/* GStreamer EBML I/O
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * ebml-ids.h: definition of EBML data IDs
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

#ifndef __GST_EBML_IDS_H__
#define __GST_EBML_IDS_H__

G_BEGIN_DECLS
/* EBML version supported */
#define GST_EBML_VERSION 1
/* top-level master-IDs */
#define GST_EBML_ID_HEADER             0x1A45DFA3
/* IDs in the HEADER master */
#define GST_EBML_ID_EBMLVERSION        0x4286
#define GST_EBML_ID_EBMLREADVERSION    0x42F7
#define GST_EBML_ID_EBMLMAXIDLENGTH    0x42F2
#define GST_EBML_ID_EBMLMAXSIZELENGTH  0x42F3
#define GST_EBML_ID_DOCTYPE            0x4282
#define GST_EBML_ID_DOCTYPEVERSION     0x4287
#define GST_EBML_ID_DOCTYPEREADVERSION 0x4285
/* general EBML types */
#define GST_EBML_ID_VOID               0xEC
    G_END_DECLS
#endif /* __GST_EBML_IDS_H__ */
