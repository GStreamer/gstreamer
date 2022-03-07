/* GStreamer
 * Copyright (C) 2019 Igalia S.L
 * Copyright (C) 2019 Metrological
 *   Author: Charlie Turner <cturner@igalia.com>
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

#ifndef __GST_MOCKDECRYPTOR_H__
#define __GST_MOCKDECRYPTOR_H__

typedef struct _GstMockDecryptor GstMockDecryptor;
typedef struct _GstMockDecryptorClass GstMockDecryptorClass;

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_MOCKDECRYPTOR \
  (gst_mockdecryptor_get_type ())
#define GST_MOCKDECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MOCKDECRYPTOR,GstMockDecryptor))
#define GST_MOCKDECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MOCKDECRYPTOR,GstMockDecryptorClass))
#define GST_IS_MOCKDECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MOCKDECRYPTOR))
#define GST_IS_MOCKDECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MOCKDECRYPTOR))
#define GST_MOCKDECRYPTOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MOCKDECRYPTOR,GstMockDecryptorClass))

#define GST_MOCKDECRYPTOR_NAME            "mockdecryptor"
struct _GstMockDecryptor
{
  GstBaseTransform element;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMockDecryptorClass
{
  GstBaseTransformClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

G_GNUC_INTERNAL GType gst_mockdecryptor_get_type (void);

G_END_DECLS

#endif /* __GST_MOCKDECRYPTOR_H__ */
