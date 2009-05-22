/* GStreamer Sub-Picture Unit - VobSub/DVD handling
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"
#include "gstspu-vobsub.h"

GST_DEBUG_CATEGORY_EXTERN (dvdspu_debug);
#define GST_CAT_DEFAULT dvdspu_debug

/* Define to dump out a text description of the incoming SPU commands */
#define DUMP_DCSQ 0

/* Convert an STM offset in the SPU sequence to a GStreamer timestamp */
#define STM_TO_GST(stm) ((GST_MSECOND * 1024 * (stm)) / 90)

static void
gst_dvd_spu_parse_chg_colcon (GstDVDSpu * dvdspu, guint8 * data, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;
  guint8 *cur;
  gint16 n_entries;
  gint16 i;

  /* Clear any existing chg colcon info */
  state->n_line_ctrl_i = 0;
  if (state->line_ctrl_i != NULL) {
    g_free (state->line_ctrl_i);
    state->line_ctrl_i = NULL;
  }
  GST_DEBUG_OBJECT (dvdspu, "Change Color & Contrast. Pixel data = %d bytes",
      (gint16) (end - data));

  /* Count the number of entries we'll need */
  n_entries = 0;
  for (cur = data; cur < end;) {
    guint8 n_changes;
    guint32 code;

    if (cur + 4 > end)
      break;

    code = GST_READ_UINT32_BE (cur);
    if (code == 0x0fffffff)
      break;                    /* Termination code */

    n_changes = CLAMP ((cur[2] >> 4), 1, 8);
    cur += 4 + (6 * n_changes);

    if (cur > end)
      break;                    /* Invalid entry overrunning buffer */

    n_entries++;
  }

  state->n_line_ctrl_i = n_entries;
  state->line_ctrl_i = g_new (SpuLineCtrlI, n_entries);

  cur = data;
  for (i = 0; i < n_entries; i++) {
    SpuLineCtrlI *cur_line_ctrl = state->line_ctrl_i + i;
    guint8 n_changes = CLAMP ((cur[2] >> 4), 1, 8);
    guint8 c;

    cur_line_ctrl->n_changes = n_changes;
    cur_line_ctrl->top = ((cur[0] << 8) & 0x300) | cur[1];
    cur_line_ctrl->bottom = ((cur[2] << 8) & 0x300) | cur[3];

    GST_LOG_OBJECT (dvdspu, "ChgColcon Entry %d Top: %d Bottom: %d Changes: %d",
        i, cur_line_ctrl->top, cur_line_ctrl->bottom, n_changes);
    cur += 4;

    for (c = 0; c < n_changes; c++) {
      SpuPixCtrlI *cur_pix_ctrl = cur_line_ctrl->pix_ctrl_i + c;

      cur_pix_ctrl->left = ((cur[0] << 8) & 0x300) | cur[1];
      cur_pix_ctrl->palette = GST_READ_UINT32_BE (cur + 2);
      GST_LOG_OBJECT (dvdspu, "  %d: left: %d palette 0x%x", c,
          cur_pix_ctrl->left, cur_pix_ctrl->palette);
      cur += 6;
    }
  }
}

static void
gst_dvd_spu_exec_cmd_blk (GstDVDSpu * dvdspu, guint8 * data, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;

  while (data < end) {
    guint8 cmd;

    cmd = data[0];

    switch (cmd) {
      case SPU_CMD_FSTA_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Forced Display");
        state->flags |= SPU_STATE_FORCED_DSP;
        data += 1;
        break;
      case SPU_CMD_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Display On");
        state->flags |= SPU_STATE_DISPLAY;
        data += 1;
        break;
      case SPU_CMD_STP_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Display Off");
        state->flags &= ~(SPU_STATE_FORCED_DSP | SPU_STATE_DISPLAY);
        data += 1;
        break;
      case SPU_CMD_SET_COLOR:{
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid SET_COLOR cmd at the end of the blk */

        state->main_idx[3] = data[1] >> 4;
        state->main_idx[2] = data[1] & 0x0f;
        state->main_idx[1] = data[2] >> 4;
        state->main_idx[0] = data[2] & 0x0f;

        state->main_pal_dirty = TRUE;

        GST_DEBUG_OBJECT (dvdspu,
            " Set Color bg %u pattern %u emph-1 %u emph-2 %u",
            state->main_idx[0], state->main_idx[1], state->main_idx[2],
            state->main_idx[3]);
        data += 3;
        break;
      }
      case SPU_CMD_SET_ALPHA:{
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid SET_ALPHA cmd at the end of the blk */

        state->main_alpha[3] = data[1] >> 4;
        state->main_alpha[2] = data[1] & 0x0f;
        state->main_alpha[1] = data[2] >> 4;
        state->main_alpha[0] = data[2] & 0x0f;

        state->main_pal_dirty = TRUE;

        GST_DEBUG_OBJECT (dvdspu,
            " Set Alpha bg %u pattern %u emph-1 %u emph-2 %u",
            state->main_alpha[0], state->main_alpha[1], state->main_alpha[2],
            state->main_alpha[3]);
        data += 3;
        break;
      }
      case SPU_CMD_SET_DAREA:{
        SpuRect *r = &state->disp_rect;

        if (G_UNLIKELY (data + 7 >= end))
          return;               /* Invalid SET_DAREA cmd at the end of the blk */

        r->top = ((data[4] & 0x3f) << 4) | ((data[5] & 0xe0) >> 4);
        r->left = ((data[1] & 0x3f) << 4) | ((data[2] & 0xf0) >> 4);
        r->right = ((data[2] & 0x03) << 8) | data[3];
        r->bottom = ((data[5] & 0x03) << 8) | data[6];

        GST_DEBUG_OBJECT (dvdspu,
            " Set Display Area top %u left %u bottom %u right %u", r->top,
            r->left, r->bottom, r->right);

        data += 7;
        break;
      }
      case SPU_CMD_DSPXA:{
        if (G_UNLIKELY (data + 5 >= end))
          return;               /* Invalid SET_DSPXE cmd at the end of the blk */

        state->pix_data[0] = GST_READ_UINT16_BE (data + 1);
        state->pix_data[1] = GST_READ_UINT16_BE (data + 3);
        /* Store a reference to the current command buffer, as that's where 
         * we'll need to take our pixel data from */
        gst_buffer_replace (&state->pix_buf, state->buf);

        GST_DEBUG_OBJECT (dvdspu, " Set Pixel Data Offsets top: %u bot: %u",
            state->pix_data[0], state->pix_data[1]);

        data += 5;
        break;
      }
      case SPU_CMD_CHG_COLCON:{
        guint16 field_size;

        GST_DEBUG_OBJECT (dvdspu, " Set Color & Contrast Change");
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid CHG_COLCON cmd at the end of the blk */

        data++;
        field_size = GST_READ_UINT16_BE (data);

        if (G_UNLIKELY (data + field_size >= end))
          return;               /* Invalid CHG_COLCON cmd at the end of the blk */

        gst_dvd_spu_parse_chg_colcon (dvdspu, data + 2, data + field_size);
        state->line_ctrl_i_pal_dirty = TRUE;
        data += field_size;
        break;
      }
      case SPU_CMD_END:
      default:
        GST_DEBUG_OBJECT (dvdspu, " END");
        data = end;
        break;
    }
  }
}

static void
gst_dvd_spu_finish_spu_buf (GstDVDSpu * dvdspu)
{
  SpuState *state = &dvdspu->spu_state;

  state->next_ts = state->base_ts = GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&state->buf, NULL);

  GST_DEBUG_OBJECT (dvdspu, "Finished SPU buffer");
}

static gboolean
gst_dvd_spu_setup_cmd_blk (GstDVDSpu * dvdspu, guint16 cmd_blk_offset,
    guint8 * start, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;
  guint16 delay;
  guint8 *cmd_blk = start + cmd_blk_offset;

  if (G_UNLIKELY (cmd_blk + 5 >= end))
    return FALSE;               /* No valid command block to read */

  delay = GST_READ_UINT16_BE (cmd_blk);
  state->next_ts = state->base_ts + STM_TO_GST (delay);
  state->cur_cmd_blk = cmd_blk_offset;

  GST_DEBUG_OBJECT (dvdspu, "Setup CMD Block @ %u with TS %" GST_TIME_FORMAT,
      state->cur_cmd_blk, GST_TIME_ARGS (state->next_ts));
  return TRUE;
}

#if DUMP_DCSQ
static void
gst_dvd_spu_dump_dcsq (GstDVDSpu * dvdspu,
    GstClockTime start_ts, GstBuffer * spu_buf)
{
  guint16 cmd_blk_offset;
  guint16 next_blk;
  guint8 *start, *end;

  start = GST_BUFFER_DATA (spu_buf);
  end = start + GST_BUFFER_SIZE (spu_buf);

  g_return_if_fail (start != NULL);

  /* First command */
  next_blk = GST_READ_UINT16_BE (start + 2);
  cmd_blk_offset = 0;

  /* Loop through all commands */
  g_print ("SPU begins @ %" GST_TIME_FORMAT " offset %u\n",
      GST_TIME_ARGS (start_ts), next_blk);

  while (cmd_blk_offset != next_blk) {
    guint8 *data;
    GstClockTime cmd_blk_ts;

    cmd_blk_offset = next_blk;

    if (G_UNLIKELY (start + cmd_blk_offset + 5 >= end))
      break;                    /* No valid command to read */

    data = start + cmd_blk_offset;

    cmd_blk_ts = start_ts + STM_TO_GST (GST_READ_UINT16_BE (data));
    next_blk = GST_READ_UINT16_BE (data + 2);

    g_print ("Cmd Blk @ offset %u next %u ts %" GST_TIME_FORMAT "\n",
        cmd_blk_offset, next_blk, GST_TIME_ARGS (cmd_blk_ts));

    data += 4;
    gst_dvd_spu_exec_cmd_blk (dvdspu, data, end);
  }
}
#endif

void
gst_dvd_spu_handle_new_vobsub_buf (GstDVDSpu * dvdspu, SpuPacket * packet)
{
  guint8 *start, *end;
  SpuState *state = &dvdspu->spu_state;

#if DUMP_DCSQ
  gst_dvd_spu_dump_dcsq (dvdspu, packet->event_ts, packet->buf);
#endif

  if (G_UNLIKELY (GST_BUFFER_SIZE (packet->buf) < 4))
    goto invalid;

  if (state->buf != NULL) {
    gst_buffer_unref (state->buf);
    state->buf = NULL;
  }
  state->buf = packet->buf;
  state->base_ts = packet->event_ts;

  start = GST_BUFFER_DATA (state->buf);
  end = start + GST_BUFFER_SIZE (state->buf);

  /* Configure the first command block in this buffer as our initial blk */
  state->cur_cmd_blk = GST_READ_UINT16_BE (start + 2);
  gst_dvd_spu_setup_cmd_blk (dvdspu, state->cur_cmd_blk, start, end);
  /* Clear existing chg-colcon info */
  state->n_line_ctrl_i = 0;
  if (state->line_ctrl_i != NULL) {
    g_free (state->line_ctrl_i);
    state->line_ctrl_i = NULL;
  }
  return;

invalid:
  /* Invalid buffer */
  gst_dvd_spu_finish_spu_buf (dvdspu);
}

void
gst_dvdspu_vobsub_execute_event (GstDVDSpu * dvdspu)
{
  guint8 *start, *cmd_blk, *end;
  guint16 next_blk;
  SpuState *state = &dvdspu->spu_state;

  GST_DEBUG_OBJECT (dvdspu, "Executing cmd blk with TS %" GST_TIME_FORMAT
      " @ offset %u", GST_TIME_ARGS (state->next_ts), state->cur_cmd_blk);

  start = GST_BUFFER_DATA (state->buf);
  end = start + GST_BUFFER_SIZE (state->buf);

  cmd_blk = start + state->cur_cmd_blk;

  if (G_UNLIKELY (cmd_blk + 5 >= end)) {
    /* Invalid. Finish the buffer and loop again */
    gst_dvd_spu_finish_spu_buf (dvdspu);
    return;
  }

  gst_dvd_spu_exec_cmd_blk (dvdspu, cmd_blk + 4, end);

  next_blk = GST_READ_UINT16_BE (cmd_blk + 2);
  if (next_blk != state->cur_cmd_blk) {
    /* Advance to the next block of commands */
    gst_dvd_spu_setup_cmd_blk (dvdspu, next_blk, start, end);
  } else {
    /* Next Block points to the current block, so we're finished with this
     * SPU buffer */
    gst_dvd_spu_finish_spu_buf (dvdspu);
  }
}
