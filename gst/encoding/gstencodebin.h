/* GStreamer encoding bin
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2009 Nokia Corporation
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

#ifndef __GST_ENCODEBIN_H__
#define __GST_ENCODEBIN_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#define GST_TYPE_ENCODE_BIN               (gst_encode_bin_get_type())
#define GST_ENCODE_BIN(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ENCODE_BIN,GstEncodeBin))
#define GST_ENCODE_BIN_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ENCODE_BIN,GstEncodeBinClass))
#define GST_IS_ENCODE_BIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ENCODE_BIN))
#define GST_IS_ENCODE_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ENCODE_BIN))

typedef struct _GstEncodeBin GstEncodeBin;
typedef struct _GstEncodeBinClass GstEncodeBinClass;

GType gst_encode_bin_get_type(void);

#endif /* __GST_ENCODEBIN_H__ */
