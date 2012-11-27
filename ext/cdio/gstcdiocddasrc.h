/* GStreamer
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_CDIO_CDDA_SRC_H__
#define __GST_CDIO_CDDA_SRC_H__

#include <gst/audio/gstaudiocdsrc.h>
#include <cdio/cdio.h>

#define GST_TYPE_CDIO_CDDA_SRC            (gst_cdio_cdda_src_get_type ())
#define GST_CDIO_CDDA_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CDIO_CDDA_SRC, GstCdioCddaSrc))
#define GST_CDIO_CDDA_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_CDIO_CDDA_SRC, GstCdioCddaSrcClass))
#define GST_IS_CDIO_CDDA_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CDIO_CDDA_SRC))
#define GST_IS_CDIO_CDDA_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_TYPE_CDIO_CDDA_SRC))

typedef struct _GstCdioCddaSrc GstCdioCddaSrc;
typedef struct _GstCdioCddaSrcClass GstCdioCddaSrcClass;

struct _GstCdioCddaSrc
{
  GstAudioCdSrc  audiocdsrc;

  gint           read_speed;    /* ATOMIC */

  gboolean       swap_le_be;    /* Drive produces samples in other endianness */

  CdIo          *cdio;          /* NULL if not open */
};

struct _GstCdioCddaSrcClass
{
  GstAudioCdSrcClass  audiocdsrc_class;
};

GType   gst_cdio_cdda_src_get_type (void);

#endif /* __GST_CDIO_CDDA_SRC_H__ */

