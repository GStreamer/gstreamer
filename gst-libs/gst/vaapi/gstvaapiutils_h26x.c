/*
 *  gstvaapiutils_h26x.c - H.26x related utilities
 *
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapiutils_h26x_priv.h"

/* Write an unsigned integer Exp-Golomb-coded syntax element. i.e. ue(v) */
gboolean
bs_write_ue (GstBitWriter * bs, guint32 value)
{
  guint32 size_in_bits = 0;
  guint32 tmp_value = ++value;

  while (tmp_value) {
    ++size_in_bits;
    tmp_value >>= 1;
  }
  if (size_in_bits > 1
      && !gst_bit_writer_put_bits_uint32 (bs, 0, size_in_bits - 1))
    return FALSE;
  if (!gst_bit_writer_put_bits_uint32 (bs, value, size_in_bits))
    return FALSE;
  return TRUE;
}

/* Write a signed integer Exp-Golomb-coded syntax element. i.e. se(v) */
gboolean
bs_write_se (GstBitWriter * bs, gint32 value)
{
  guint32 new_val;

  if (value <= 0)
    new_val = -(value << 1);
  else
    new_val = (value << 1) - 1;

  if (!bs_write_ue (bs, new_val))
    return FALSE;
  return TRUE;
}
