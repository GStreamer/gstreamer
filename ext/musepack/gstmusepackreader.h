/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_MUSEPACK_READER_H__
#define __GST_MUSEPACK_READER_H__

#include <mpc_dec.h>
#include <gst/bytestream/bytestream.h>

class GstMusepackReader : public MPC_reader {
public:
  GstMusepackReader (GstByteStream *bs);
  virtual ~GstMusepackReader (void);

  mpc_int32_t read (void * ptr, mpc_int32_t size);
  bool seek (mpc_int32_t offset);
  mpc_int32_t tell (void);
  mpc_int32_t get_size (void);
  bool canseek (void);

  bool eos;

private:
  GstByteStream *bs;
};

#endif /* __GST_MUSEPACK_READER_H__ */
