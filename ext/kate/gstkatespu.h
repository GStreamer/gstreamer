/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) <2009> ogg.k.ogg.k <ogg.k.ogg.k at googlemail dot com>
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


#ifndef __GST_KATE_SPU_H__
#define __GST_KATE_SPU_H__

#include <gst/gst.h>
#include <kate/kate.h>
#include "gstkateenc.h"
#include "gstkatedec.h"

#define GST_KATE_UINT16_BE(ptr) ( ( ((guint16)((ptr)[0])) <<8) | ((ptr)[1]) )

/* taken off the DVD SPU decoder - now is time for today's WTF ???? */
#define GST_KATE_STM_TO_GST(stm) ((GST_MSECOND * 1024 * (stm)) / 90)
#define GST_KATE_GST_TO_STM(gst) ((int)(((gst) * 90000 ) / 1024))

#define GST_KATE_SPU_MIME_TYPE "subpicture/x-dvd"

G_BEGIN_DECLS

enum GstKateSpuCmd
{
  SPU_CMD_FSTA_DSP = 0x00,      /* Forced Display */
  SPU_CMD_DSP = 0x01,           /* Display Start */
  SPU_CMD_STP_DSP = 0x02,       /* Display Off */
  SPU_CMD_SET_COLOR = 0x03,     /* Set the color indexes for the palette */
  SPU_CMD_SET_ALPHA = 0x04,     /* Set the alpha indexes for the palette */
  SPU_CMD_SET_DAREA = 0x05,     /* Set the display area for the SPU */
  SPU_CMD_DSPXA = 0x06,         /* Pixel data addresses */
  SPU_CMD_CHG_COLCON = 0x07,    /* Change Color & Contrast */
  SPU_CMD_END = 0xff
};


extern const guint32 gst_kate_spu_default_clut[16];

extern GstFlowReturn
gst_kate_spu_decode_spu (GstKateEnc * ke, GstBuffer * buf, kate_region * kr,
    kate_bitmap * kb, kate_palette * kp);

extern GstBuffer*
gst_kate_spu_encode_spu (GstKateDec * kd, const kate_event * ev);

G_END_DECLS

#endif /* __GST_KATE_SPU_H__ */
