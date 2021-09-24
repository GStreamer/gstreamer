/* GStreamer
 * Copyright (C) 2020 Matthew Waters <matthew@centricular.com>
 *
 * gstrtphdrexttwcc.h: transport-wide-cc RTP header extensions for the
 *   Audio/Video RTP Profile
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

#ifndef __GST_RTPHDREXT_TWCC_H__
#define __GST_RTPHDREXT_TWCC_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

G_BEGIN_DECLS

GType gst_rtp_header_extension_twcc_get_type (void);
#define GST_TYPE_RTP_HEADER_EXTENSION_TWCC (gst_rtp_header_extension_twcc_get_type())
#define GST_RTP_HEADER_EXTENSION_TWCC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_HEADER_EXTENSION_TWCC,GstRTPHeaderExtensionTWCC))
#define GST_RTP_HEADER_EXTENSION_TWCC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_HEADER_EXTENSION_TWCC,GstRTPHeaderExtensionTWCCClass))
#define GST_RTP_HEADER_EXTENSION_TWCC_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_RTP_HEADER_EXTENSION_TWCC,GstRTPHeaderExtensionTWCCClass))
#define GST_IS_RTP_HEADER_EXTENSION_TWCC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_HEADER_EXTENSION_TWCC))
#define GST_IS_RTP_HEADER_EXTENSION_TWCC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_HEADER_EXTENSION_TWCC))
#define GST_RTP_HEADER_EXTENSION_TWCC_CAST(obj) ((GstRTPHeaderExtensionTWCC *)(obj))

typedef struct _GstRTPHeaderExtensionTWCC      GstRTPHeaderExtensionTWCC;
typedef struct _GstRTPHeaderExtensionTWCCClass GstRTPHeaderExtensionTWCCClass;

/**
 * GstRTPHeaderExtensionTWCC:
 * @parent: the parent #GstRTPHeaderExtension
 *
 * Instance struct for a transport-wide-cc RTP Audio/Video header extension.
 *
 * http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
 */
struct _GstRTPHeaderExtensionTWCC
{
  GstRTPHeaderExtension parent;

  guint16 seqnum;
  guint n_streams;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstRTPHeaderExtensionTWCCClass:
 * @parent_class: the parent class
 */
struct _GstRTPHeaderExtensionTWCCClass
{
  GstRTPHeaderExtensionClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_ELEMENT_REGISTER_DECLARE (rtphdrexttwcc);

G_END_DECLS

#endif /* __GST_RTPHDREXT_TWCC_H__ */
