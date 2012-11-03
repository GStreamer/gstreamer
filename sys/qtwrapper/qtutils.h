/*
 * GStreamer QuickTime codec mapping
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007> Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef G_OS_WIN32
#include <ImageCodec.h>
#else
#include <QuickTime/ImageCodec.h>
#endif
#include <gst/gst.h>
#include "qtwrapper.h"

#ifndef __QTUTILS_H__
#define __QTUTILS_H__

#define QT_UINT32(a)  (GST_READ_UINT32_BE(a))
#define QT_UINT24(a)  (GST_READ_UINT32_BE(a) >> 8)
#define QT_UINT16(a)  (GST_READ_UINT16_BE(a))
#define QT_UINT8(a)   (GST_READ_UINT8(a))
#define QT_FP32(a)    ((GST_READ_UINT32_BE(a))/65536.0)
#define QT_FP16(a)    ((GST_READ_UINT16_BE(a))/256.0)
#define QT_FOURCC(a)  (GST_READ_UINT32_LE(a))
#define QT_UINT64(a)  ((((guint64)QT_UINT32(a))<<32)|QT_UINT32(((guint8 *)a)+4))
#define QT_FOURCC_ARGS(fourcc)			\
  ((gchar) (((fourcc)>>24)&0xff)),		\
    ((gchar) (((fourcc)>>16)&0xff)),		\
    ((gchar) (((fourcc)>>8 )&0xff)),		\
    ((gchar) ((fourcc)     &0xff))

#define QT_WRITE_UINT8(data, num)      GST_WRITE_UINT8(data, num)

#define QT_MAKE_FOURCC_BE(a,b,c,d)      (guint32)((a)|(b)<<8|(c)<<16|(d)<<24)
#define QT_MAKE_FOURCC_LE(a,b,c,d)	QT_MAKE_FOURCC_BE(d,c,b,a)

#define _QT_PUT(__data, __idx, __size, __shift, __num) \
    (((guint8 *) (__data))[__idx] = (((guint##__size) __num) >> __shift) & 0xff)

/* endianness-dependent macros */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define QT_MAKE_FOURCC(a,b,c,d)		QT_MAKE_FOURCC_LE(a,b,c,d)
#define QT_WRITE_UINT16(data, num)	GST_WRITE_UINT16_LE(data, num)
#define QT_WRITE_UINT24(data, num)	do {				\
					  _QT_PUT (data, 0, 32,  0, num); \
					  _QT_PUT (data, 1, 32,  8, num); \
					  _QT_PUT (data, 2, 32, 16, num); \
					} while (0)
#define QT_WRITE_UINT32(data, num)	GST_WRITE_UINT32_LE(data, num)
#define QT_READ_UINT16(data)		GST_READ_UINT16_LE(data)
#define QT_READ_UINT32(data)		GST_READ_UINT32_LE(data)
#else
#define QT_MAKE_FOURCC(a,b,c,d)         QT_MAKE_FOURCC_BE(a,b,c,d)
#define QT_WRITE_UINT16(data, num)     GST_WRITE_UINT16_BE(data, num)
#define QT_WRITE_UINT24(data, num)	do {				\
					  _QT_PUT (data, 0, 32, 16, num); \
					  _QT_PUT (data, 1, 32,  8, num); \
					  _QT_PUT (data, 2, 32,  0, num); \
					} while (0)
#define QT_WRITE_UINT32(data, num)     GST_WRITE_UINT32_BE(data, num)
#define QT_READ_UINT16(data)		GST_READ_UINT16_BE(data)
#define QT_READ_UINT32(data)		GST_READ_UINT32_BE(data)
#endif


/*
 * get_name_info_from_component:
 *
 * Fills name and info with the name and description from a Component
 */

gboolean
get_name_info_from_component (Component component, ComponentDescription * desc,
    gchar ** name, gchar ** info);



gboolean get_output_info_from_component (Component component);



void dump_image_description (ImageDescription * desc);
void dump_codec_decompress_params (CodecDecompressParams * params);

guint32 destination_pixel_types_to_fourcc (OSType ** types);
void
addSInt32ToDictionary (CFMutableDictionaryRef dictionary, CFStringRef key,
    SInt32 numberSInt32);

void dump_cvpixel_buffer (CVPixelBufferRef pixbuf);

void dump_avcc_atom (guint8 * atom);

AudioBufferList *AllocateAudioBufferList(UInt32 numChannels, UInt32 size);

void DestroyAudioBufferList(AudioBufferList* list);

#endif /* __QTUTILS_H__ */
