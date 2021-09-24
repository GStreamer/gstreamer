/* GStreamer
 *
 * jpegformat: a plugin for JPEG Interchange Format
 *
 * Copyright (C) <2010> Stefan Kost <ensonic@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_JPEG_FORMAT_H__
#define __GST_JPEG_FORMAT_H__

G_BEGIN_DECLS

/*
 * JPEG Markers
 */
 
/* Start Of Frame markers, non-differential, Huffman coding */
#define SOF0      0xc0  /* Baseline DCT */
#define SOF1      0xc1  /* Extended sequential DCT */
#define SOF2      0xc2  /* Progressive DCT */
#define SOF3      0xc3  /* Lossless */

/* Start Of Frame markers, differential, Huffman coding */
#define SOF5      0xc5
#define SOF6      0xc6
#define SOF7      0xc7

/* Start Of Frame markers, non-differential, arithmetic coding */
#define JPG       0xc8  /* Reserved */
#define SOF9      0xc9
#define SOF10     0xca
#define SOF11     0xcb

/* Start Of Frame markers, differential, arithmetic coding */
#define SOF13     0xcd
#define SOF14     0xce
#define SOF15     0xcf

/* Restart interval termination */
#define RST0      0xd0  /* Restart ... */
#define RST1      0xd1
#define RST2      0xd2
#define RST3      0xd3
#define RST4      0xd4
#define RST5      0xd5
#define RST6      0xd6
#define RST7      0xd7

#define SOI       0xd8  /* Start of image */
#define EOI       0xd9  /* End Of Image */
#define SOS       0xda  /* Start Of Scan */

#define DHT       0xc4  /* Huffman Table(s) */
#define DAC       0xcc  /* Algorithmic Coding Table */
#define DQT       0xdb  /* Quantisation Table(s) */
#define DNL       0xdc  /* Number of lines */
#define DRI       0xdd  /* Restart Interval */
#define DHP       0xde  /* Hierarchical progression */
#define EXP       0xdf

#define APP0      0xe0  /* Application marker */
#define APP1      0xe1
#define APP2      0xe2
#define APP13     0xed
#define APP14     0xee
#define APP15     0xef

#define JPG0      0xf0  /* Reserved ... */
#define JPG13     0xfd
#define COM       0xfe  /* Comment */

#define TEM       0x01

G_END_DECLS

#endif /* __GST_JPEG_FORMAT_H__ */
