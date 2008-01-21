/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
#ifndef __DVD_SPU_H__
#define __DVD_SPU_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DVD_SPU \
  (gst_dvd_spu_get_type())
#define GST_DVD_SPU(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVD_SPU,GstDVDSpu))
#define GST_DVD_SPU_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVD_SPU,GstDVDSpuClass))
#define GST_IS_DVD_SPU(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVD_SPU))
#define GST_IS_DVD_SPU_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVD_SPU))

#define DVD_SPU_LOCK(s) g_mutex_lock ((s)->spu_lock);
#define DVD_SPU_UNLOCK(s) g_mutex_unlock ((s)->spu_lock);

typedef struct _GstDVDSpu      GstDVDSpu;
typedef struct _GstDVDSpuClass GstDVDSpuClass;

typedef struct SpuRect SpuRect;
typedef struct SpuPixCtrlI SpuPixCtrlI;
typedef struct SpuLineCtrlI SpuLineCtrlI;
typedef struct SpuColour SpuColour;
typedef enum SpuStateFlags SpuStateFlags;
typedef struct SpuState SpuState;
typedef struct SpuPacket SpuPacket;
typedef enum SpuCmd SpuCmd;

/* Describe the limits of a rectangle */
struct SpuRect {
  gint16 left;
  gint16 top;
  gint16 right;
  gint16 bottom;
};

/* Store a pre-multiplied colour value. The YUV fields hold the YUV values
 * multiplied by the 8-bit alpha, to save computing it while rendering */
struct SpuColour {
  guint16 Y;
  guint16 U;
  guint16 V;
  guint8 A;
};

/* Pixel Control Info from a Change Color Contrast command */
struct SpuPixCtrlI {
  gint16 left;
  guint32 palette;

  /* Pre-multiplied palette values, updated as
   * needed */
  SpuColour pal_cache[4];
};

struct SpuLineCtrlI {
  guint8 n_changes; /* 1 to 8 */
  SpuPixCtrlI pix_ctrl_i[8];

  gint16 top;
  gint16 bottom;
};

enum SpuCmd {
  SPU_CMD_FSTA_DSP     = 0x00, /* Forced Display */
  SPU_CMD_DSP          = 0x01, /* Display Start */
  SPU_CMD_STP_DSP      = 0x02, /* Display Off */
  SPU_CMD_SET_COLOR    = 0x03, /* Set the color indexes for the palette */
  SPU_CMD_SET_ALPHA    = 0x04, /* Set the alpha indexes for the palette */
  SPU_CMD_SET_DAREA    = 0x05, /* Set the display area for the SPU */
  SPU_CMD_DSPXA        = 0x06, /* Pixel data addresses */
  SPU_CMD_CHG_COLCON   = 0x07, /* Change Color & Contrast */
  SPU_CMD_END          = 0xff
};

enum SpuStateFlags {
  SPU_STATE_NONE        = 0x00,
  /* Flags cleared on a flush */
  SPU_STATE_DISPLAY     = 0x01,
  SPU_STATE_FORCED_DSP  = 0x02,
  SPU_STATE_STILL_FRAME = 0x04,
  /* Persistent flags */
  SPU_STATE_FORCED_ONLY = 0x100
};

#define SPU_STATE_FLAGS_MASK (0xff)

struct SpuState {
  GstClockTime next_ts; /* Next event TS in running time */

  GstClockTime base_ts; /* base TS for cmd blk delays in running time */
  GstBuffer *buf; /* Current SPU packet we're executing commands from */
  guint16 cur_cmd_blk; /* Offset into the buf for the current cmd block */

  SpuStateFlags flags;
  
  /* Top + Bottom field offsets in the buffer. 0 = not set */
  guint16 pix_data[2]; 
  GstBuffer *pix_buf; /* Current SPU packet the pix_data references */
  
  SpuRect disp_rect;
  SpuRect hl_rect;

  guint32 current_clut[16]; /* Colour lookup table from incoming events */

  guint8 main_idx[4]; /* Indices for current main palette */
  guint8 main_alpha[4]; /* Alpha values for main palette */

  guint8 hl_idx[4]; /* Indices for current highlight palette */
  guint8 hl_alpha[4]; /* Alpha values for highlight palette */

  /* Pre-multiplied colour palette for the main palette */
  SpuColour main_pal[4];
  gboolean main_pal_dirty;

  /* Line control info for rendering the highlight palette */
  SpuLineCtrlI hl_ctrl_i;
  gboolean hl_pal_dirty; /* Indicates that the HL palette info needs refreshing */

  /* LineCtrlI Info from a Change Color & Contrast command */
  SpuLineCtrlI *line_ctrl_i;
  gint16 n_line_ctrl_i;
  gboolean line_ctrl_i_pal_dirty; /* Indicates that the palettes for the line_ctrl_i
                                   * need recalculating */

  /* Rendering state vars below */
  guint16 *comp_bufs[3]; /* Compositing buffers for U+V & A */
  gint16 comp_last_x[2]; /* Maximum X values we rendered into the comp buffer (odd & even) */
  gint16 *comp_last_x_ptr; /* Ptr to the current comp_last_x value to be updated by the render */
  gint16 vid_width, vid_height;
  gint16 Y_stride, UV_stride;
  gint16 Y_height, UV_height;

  gint fps_n, fps_d;

  /* Current Y Position */
  gint16 cur_Y;

  /* Current offset in nibbles into the pix_data */
  guint16 cur_offsets[2];
  guint16 max_offset;

  /* current ChgColCon Line Info */
  SpuLineCtrlI *cur_chg_col;
  SpuLineCtrlI *cur_chg_col_end;

  /* Output position tracking */
  guint8  *out_Y;
  guint16 *out_U;
  guint16 *out_V;
  guint16 *out_A;
};

/* Structure used to store the queue of pending SPU packets. The start_ts is
 * stored in running time... 
 * Also used to carry in-band events so they remain serialised properly */
struct SpuPacket {
  GstClockTime event_ts;
  GstBuffer *buf;
  GstEvent *event;
};

struct _GstDVDSpu {
  GstElement element;

  GstPad *videosinkpad;
  GstPad *subpic_sinkpad;
  GstPad *srcpad;

  /* Mutex to protect state we access from different chain funcs */
  GMutex *spu_lock;

  GstSegment video_seg;
  GstSegment subp_seg;

  SpuState spu_state;

  /* GQueue of SpuBuf structures */
  GQueue *pending_spus;

  /* Accumulator for collecting partial SPU buffers until they're complete */
  GstBuffer *partial_spu;

  /* Store either a reference or a copy of the last video frame for duplication
   * during still-frame conditions */
  GstBuffer *ref_frame;

  /* Buffer to push after handling a DVD event, if any */
  GstBuffer *pending_frame;
};

struct _GstDVDSpuClass {
  GstElementClass parent_class;
};

GType gst_dvd_spu_get_type (void);

G_END_DECLS

#endif /* __DVD_SPU_H__ */
