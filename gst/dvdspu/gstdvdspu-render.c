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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"

GST_DEBUG_CATEGORY_EXTERN (dvdspu_debug);
#define GST_CAT_DEFAULT dvdspu_debug

static void
dvdspu_recalc_palette (GstDVDSpu * dvdspu,
    SpuColour * dest, guint8 * idx, guint8 * alpha)
{
  SpuState *state = &dvdspu->spu_state;
  gint i;

  for (i = 0; i < 4; i++, dest++) {
    guint32 col = state->current_clut[idx[i]];

    dest->Y = (guint16) ((col >> 16) & 0xff) * alpha[i];
    /* U/V are stored as V/U in the clut words, so switch them */
    dest->U = (guint16) (col & 0xff) * alpha[i];
    dest->V = (guint16) ((col >> 8) & 0xff) * alpha[i];
    dest->A = alpha[i];
  }
}

/* Recalculate the main, HL & ChgCol palettes */
static void
dvdspu_update_palettes (GstDVDSpu * dvdspu, SpuState * state)
{
  gint16 l, c;
  guint8 index[4];              /* Indices for the palette */
  guint8 alpha[4];              /* Alpha values the palette */

  if (state->main_pal_dirty) {
    dvdspu_recalc_palette (dvdspu, state->main_pal, state->main_idx,
        state->main_alpha);

    /* Need to refresh the hl_ctrl info copies of the main palette too */
    memcpy (state->hl_ctrl_i.pix_ctrl_i[0].pal_cache, state->main_pal,
        4 * sizeof (SpuColour));
    memcpy (state->hl_ctrl_i.pix_ctrl_i[2].pal_cache, state->main_pal,
        4 * sizeof (SpuColour));

    state->main_pal_dirty = FALSE;
  }

  if (state->hl_pal_dirty) {
    dvdspu_recalc_palette (dvdspu, state->hl_ctrl_i.pix_ctrl_i[1].pal_cache,
        state->hl_idx, state->hl_alpha);
    state->hl_pal_dirty = FALSE;
  }

  /* Update the offset positions for the highlight region */
  if (state->hl_rect.top != -1) {
    state->hl_ctrl_i.top = state->hl_rect.top;
    state->hl_ctrl_i.bottom = state->hl_rect.bottom;
    state->hl_ctrl_i.n_changes = 3;
    state->hl_ctrl_i.pix_ctrl_i[0].left = 0;
    state->hl_ctrl_i.pix_ctrl_i[1].left = state->hl_rect.left;
    state->hl_ctrl_i.pix_ctrl_i[2].left = state->hl_rect.right + 1;
  }

  if (state->line_ctrl_i_pal_dirty) {
    GST_LOG_OBJECT (dvdspu, "Updating chg-col-con palettes");
    for (l = 0; l < state->n_line_ctrl_i; l++) {
      SpuLineCtrlI *cur_line_ctrl = state->line_ctrl_i + l;

      for (c = 0; c < cur_line_ctrl->n_changes; c++) {
        SpuPixCtrlI *cur = cur_line_ctrl->pix_ctrl_i + c;

        index[3] = (cur->palette >> 28) & 0x0f;
        index[2] = (cur->palette >> 24) & 0x0f;
        index[1] = (cur->palette >> 20) & 0x0f;
        index[0] = (cur->palette >> 16) & 0x0f;

        alpha[3] = (cur->palette >> 12) & 0x0f;
        alpha[2] = (cur->palette >> 8) & 0x0f;
        alpha[1] = (cur->palette >> 4) & 0x0f;
        alpha[0] = (cur->palette) & 0x0f;
        dvdspu_recalc_palette (dvdspu, cur->pal_cache, index, alpha);
      }
    }
    state->line_ctrl_i_pal_dirty = FALSE;
  }
}

static void
dvdspu_clear_comp_buffers (SpuState * state)
{
  /* The area to clear is the line inside the disp_rect, each entry 2 bytes,
   * of the sub-sampled UV planes. */
  gint16 left = state->disp_rect.left / 2;
  gint16 right = state->disp_rect.right / 2;
  gint16 uv_width = 2 * (right - left + 1);

  memset (state->comp_bufs[0] + left, 0, uv_width);
  memset (state->comp_bufs[1] + left, 0, uv_width);
  memset (state->comp_bufs[2] + left, 0, uv_width);

  state->comp_last_x[0] = -1;
  state->comp_last_x[1] = -1;
}

static inline guint8
dvdspu_get_nibble (SpuState * state, guint16 * rle_offset)
{
  guint8 ret;

  if (G_UNLIKELY (*rle_offset >= state->max_offset))
    return 0;                   /* Overran the buffer */

  ret = GST_BUFFER_DATA (state->pix_buf)[(*rle_offset) / 2];

  /* If the offset is even, we shift the answer down 4 bits, otherwise not */
  if (*rle_offset & 0x01)
    ret &= 0x0f;
  else
    ret = ret >> 4;

  (*rle_offset)++;
  return ret;
}

static guint16
dvdspu_get_rle_code (SpuState * state, guint16 * rle_offset)
{
  guint16 code;

  code = dvdspu_get_nibble (state, rle_offset);
  if (code < 0x4) {             /* 4 .. f */
    code = (code << 4) | dvdspu_get_nibble (state, rle_offset);
    if (code < 0x10) {          /* 1x .. 3x */
      code = (code << 4) | dvdspu_get_nibble (state, rle_offset);
      if (code < 0x40) {        /* 04x .. 0fx */
        code = (code << 4) | dvdspu_get_nibble (state, rle_offset);
      }
    }
  }
  return code;
}

static inline void
dvdspu_draw_rle_run (SpuState * state, gint16 x, gint16 end, SpuColour * colour)
{
#if 0
  GST_LOG ("Y: %d x: %d end %d col %d %d %d %d",
      state->cur_Y, x, end, colour->Y, colour->U, colour->V, colour->A);
#endif

  if (colour->A != 0) {
    guint8 inv_A = 0xf - colour->A;

    /* FIXME: This could be more efficient */
    while (x < end) {
      state->out_Y[x] = (inv_A * state->out_Y[x] + colour->Y) / 0xf;
      state->out_U[x / 2] += colour->U;
      state->out_V[x / 2] += colour->V;
      state->out_A[x / 2] += colour->A;
      x++;
    }
    /* Update the compositing buffer so we know how much to blend later */
    *(state->comp_last_x_ptr) = end;
  }
}

static inline gint16
rle_end_x (guint16 rle_code, gint16 x, gint16 end)
{
  /* run length = rle_code >> 2 */
  if (G_UNLIKELY (((rle_code >> 2) == 0)))
    return end;
  else
    return MIN (end, x + (rle_code >> 2));
}

static void dvdspu_render_line_with_chgcol (SpuState * state,
    guint8 * planes[3], guint16 * rle_offset);
static gboolean dvdspu_update_chgcol (SpuState * state);

static void
dvdspu_render_line (SpuState * state, guint8 * planes[3], guint16 * rle_offset)
{
  gint16 x, next_x, end, rle_code;
  SpuColour *colour;

  /* Check for special case of chg_col info to use (either highlight or 
   * ChgCol command */
  if (state->cur_chg_col != NULL) {
    if (dvdspu_update_chgcol (state)) {
      /* Check the top & bottom, because we might not be within the region yet */
      if (state->cur_Y >= state->cur_chg_col->top &&
          state->cur_Y <= state->cur_chg_col->bottom) {
        dvdspu_render_line_with_chgcol (state, planes, rle_offset);
        return;
      }
    }
  }

  /* No special case. Render as normal */

  /* Set up our output pointers */
  state->out_Y = planes[0];
  state->out_U = state->comp_bufs[0];
  state->out_V = state->comp_bufs[1];
  state->out_A = state->comp_bufs[2];
  /* We always need to start our RLE decoding byte_aligned */
  *rle_offset = GST_ROUND_UP_2 (*rle_offset);

  x = state->disp_rect.left;
  end = state->disp_rect.right + 1;
  while (x < end) {
    rle_code = dvdspu_get_rle_code (state, rle_offset);
    colour = &state->main_pal[rle_code & 3];
    next_x = rle_end_x (rle_code, x, end);
    /* Now draw the run between [x,next_x) */
    dvdspu_draw_rle_run (state, x, next_x, colour);
    x = next_x;
  }
}

static gboolean
dvdspu_update_chgcol (SpuState * state)
{
  if (state->cur_chg_col == NULL)
    return FALSE;

  if (state->cur_Y <= state->cur_chg_col->bottom)
    return TRUE;

  while (state->cur_chg_col < state->cur_chg_col_end) {
    if (state->cur_Y >= state->cur_chg_col->top &&
        state->cur_Y <= state->cur_chg_col->bottom) {
#if 0
      g_print ("Stopped @ entry %d with top %d bottom %d, cur_y %d",
          (gint16) (state->cur_chg_col - state->line_ctrl_i),
          state->cur_chg_col->top, state->cur_chg_col->bottom, y);
#endif
      return TRUE;
    }
    state->cur_chg_col++;
  }

  /* Finished all our cur_chg_col entries. Use the main palette from here on */
  state->cur_chg_col = NULL;
  return FALSE;
}

static void
dvdspu_render_line_with_chgcol (SpuState * state, guint8 * planes[3],
    guint16 * rle_offset)
{
  SpuLineCtrlI *chg_col = state->cur_chg_col;

  gint16 x, next_x, disp_end, rle_code, run_end;
  SpuColour *colour;
  SpuPixCtrlI *cur_pix_ctrl;
  SpuPixCtrlI *next_pix_ctrl;
  SpuPixCtrlI *end_pix_ctrl;
  SpuPixCtrlI dummy_pix_ctrl;
  gint16 cur_reg_end;
  gint i;

  state->out_Y = planes[0];
  state->out_U = state->comp_bufs[0];
  state->out_V = state->comp_bufs[1];
  state->out_A = state->comp_bufs[2];

  /* We always need to start our RLE decoding byte_aligned */
  *rle_offset = GST_ROUND_UP_2 (*rle_offset);

  /* Our run will cover the display rect */
  x = state->disp_rect.left;
  disp_end = state->disp_rect.right + 1;

  /* Work out the first pixel control info, which may point to the dummy entry if
   * the global palette/alpha need using initally */
  cur_pix_ctrl = chg_col->pix_ctrl_i;
  end_pix_ctrl = chg_col->pix_ctrl_i + chg_col->n_changes;

  if (cur_pix_ctrl->left != 0) {
    next_pix_ctrl = cur_pix_ctrl;
    cur_pix_ctrl = &dummy_pix_ctrl;
    for (i = 0; i < 4; i++)     /* Copy the main palette to our dummy entry */
      dummy_pix_ctrl.pal_cache[i] = state->main_pal[i];
  } else {
    next_pix_ctrl = cur_pix_ctrl + 1;
  }
  if (next_pix_ctrl < end_pix_ctrl)
    cur_reg_end = next_pix_ctrl->left;
  else
    cur_reg_end = disp_end;

  /* Render stuff */
  while (x < disp_end) {
    rle_code = dvdspu_get_rle_code (state, rle_offset);
    next_x = rle_end_x (rle_code, x, disp_end);

    /* Now draw the run between [x,next_x), crossing palette regions as needed */
    while (x < next_x) {
      run_end = MIN (next_x, cur_reg_end);

      if (G_LIKELY (x < run_end)) {
        colour = &cur_pix_ctrl->pal_cache[rle_code & 3];
        dvdspu_draw_rle_run (state, x, run_end, colour);
        x = run_end;
      }

      if (x >= cur_reg_end) {
        /* Advance to next region */
        cur_pix_ctrl = next_pix_ctrl;
        next_pix_ctrl++;

        if (next_pix_ctrl < end_pix_ctrl)
          cur_reg_end = next_pix_ctrl->left;
        else
          cur_reg_end = disp_end;
      }
    }
  }
}

static void
dvdspu_blend_comp_buffers (SpuState * state, guint8 * planes[3])
{
  gint16 uv_end;
  gint16 left, x;
  guint8 *out_U;
  guint8 *out_V;
  guint16 *in_U;
  guint16 *in_V;
  guint16 *in_A;
  gint16 comp_last_x = MAX (state->comp_last_x[0], state->comp_last_x[1]);

  if (comp_last_x < state->disp_rect.left)
    return;                     /* Didn't draw in the comp buffers, nothing to do... */

#if 0
  GST_LOG ("Blending comp buffers from disp_rect.left %d to x=%d",
      state->disp_rect.left, comp_last_x);
#endif

  /* Set up the output pointers */
  out_U = planes[1];            /* U plane */
  out_V = planes[2];            /* V plane */

  /* Input starts at the first pixel of the compositing buffer */
  in_U = state->comp_bufs[0];   /* U comp buffer */
  in_V = state->comp_bufs[1];   /* V comp buffer */
  in_A = state->comp_bufs[2];   /* A comp buffer */

  /* Calculate how many pixels to blend based on the maximum X value that was 
   * drawn in the render_line function, divided by 2 (rounding up) to account 
   * for UV sub-sampling */
  uv_end = (comp_last_x + 1) / 2;
  left = state->disp_rect.left / 2;

  for (x = left; x < uv_end; x++) {
    guint16 tmp;
    guint16 inv_A = (4 * 0xf) - in_A[x];

    /* Each entry in the compositing buffer is 4 summed pixels, so the
     * inverse alpha is (4 * 0x0f) - in_A[x] */
    tmp = in_U[x] + inv_A * out_U[x];
    out_U[x] = (guint8) (tmp / (4 * 0xf));

    tmp = in_V[x] + inv_A * out_V[x];
    out_V[x] = (guint8) (tmp / (4 * 0xf));
  }
}

void
gst_dvd_spu_render_spu (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  SpuState *state = &dvdspu->spu_state;
  guint8 *planes[3];            /* YUV frame pointers */
  gint y, last_y;

  /* Set up our initial state */

  /* Store the start of each plane */
  planes[0] = GST_BUFFER_DATA (buf);
  planes[1] = planes[0] + (state->Y_height * state->Y_stride);
  planes[2] = planes[1] + (state->UV_height * state->UV_stride);

  /* Sanity check */
  g_return_if_fail (planes[2] + (state->UV_height * state->UV_stride) <=
      GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf));

  GST_DEBUG ("Rendering SPU. disp_rect %d,%d to %d,%d. hl_rect %d,%d to %d,%d",
      state->disp_rect.left, state->disp_rect.top,
      state->disp_rect.right, state->disp_rect.bottom,
      state->hl_rect.left, state->hl_rect.top,
      state->hl_rect.right, state->hl_rect.bottom);

  /* We start rendering from the first line of the display rect */
  y = state->disp_rect.top;

  /* Update our plane references to the first line of the disp_rect */
  planes[0] += state->Y_stride * y;
  planes[1] += state->UV_stride * (y / 2);
  planes[2] += state->UV_stride * (y / 2);

  /* When reading RLE data, we track the offset in nibbles... */
  state->cur_offsets[0] = state->pix_data[0] * 2;
  state->cur_offsets[1] = state->pix_data[1] * 2;
  state->max_offset = GST_BUFFER_SIZE (state->pix_buf) * 2;

  /* Update all the palette caches */
  dvdspu_update_palettes (dvdspu, state);

  /* Set up HL or Change Color & Contrast rect tracking */
  if (state->hl_rect.top != -1) {
    state->cur_chg_col = &state->hl_ctrl_i;
    state->cur_chg_col_end = state->cur_chg_col + 1;
  } else if (state->n_line_ctrl_i > 0) {
    state->cur_chg_col = state->line_ctrl_i;
    state->cur_chg_col_end = state->cur_chg_col + state->n_line_ctrl_i;
  } else
    state->cur_chg_col = NULL;

  /* start_y is always an even number and we render lines in pairs from there, 
   * accumulating 2 lines of chroma then blending it. We might need to render a 
   * single line at the end if the display rect ends on an even line too. */
  last_y = (state->disp_rect.bottom - 1) & ~(0x01);
  for (state->cur_Y = y; state->cur_Y <= last_y; state->cur_Y++) {
    /* Reset the compositing buffer */
    dvdspu_clear_comp_buffers (state);
    /* Render even line */
    state->comp_last_x_ptr = state->comp_last_x;
    dvdspu_render_line (state, planes, &state->cur_offsets[0]);
    /* Advance the luminance output pointer */
    planes[0] += state->Y_stride;
    state->cur_Y++;

    /* Render odd line */
    state->comp_last_x_ptr = state->comp_last_x + 1;
    dvdspu_render_line (state, planes, &state->cur_offsets[1]);
    /* Blend the accumulated UV compositing buffers onto the output */
    dvdspu_blend_comp_buffers (state, planes);

    /* Update all the output pointers */
    planes[0] += state->Y_stride;
    planes[1] += state->UV_stride;
    planes[2] += state->UV_stride;
  }
  if (state->cur_Y == state->disp_rect.bottom) {
    g_assert ((state->disp_rect.bottom & 0x01) == 0);

    /* Render a remaining lone last even line. y already has the correct value 
     * after the above loop exited. */
    dvdspu_clear_comp_buffers (state);
    state->comp_last_x_ptr = state->comp_last_x;
    dvdspu_render_line (state, planes, &state->cur_offsets[0]);
    dvdspu_blend_comp_buffers (state, planes);
  }

  /* for debugging purposes, draw a faint rectangle at the edges of the disp_rect */
#if 0
  do {
    guint8 *cur;
    gint16 pos;

    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->disp_rect.top;
    for (pos = state->disp_rect.left + 1; pos < state->disp_rect.right; pos++)
      cur[pos] = (cur[pos] / 2) + 0x8;
    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->disp_rect.bottom;
    for (pos = state->disp_rect.left + 1; pos < state->disp_rect.right; pos++)
      cur[pos] = (cur[pos] / 2) + 0x8;
    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->disp_rect.top;
    for (pos = state->disp_rect.top; pos <= state->disp_rect.bottom; pos++) {
      cur[state->disp_rect.left] = (cur[state->disp_rect.left] / 2) + 0x8;
      cur[state->disp_rect.right] = (cur[state->disp_rect.right] / 2) + 0x8;
      cur += state->Y_stride;
    }
  } while (0);
#endif
  /* For debugging purposes, draw a faint rectangle around the highlight rect */
#if 0
  if (state->hl_rect.top != -1) {
    guint8 *cur;
    gint16 pos;

    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->hl_rect.top;
    for (pos = state->hl_rect.left + 1; pos < state->hl_rect.right; pos++)
      cur[pos] = (cur[pos] / 2) + 0x8;
    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->hl_rect.bottom;
    for (pos = state->hl_rect.left + 1; pos < state->hl_rect.right; pos++)
      cur[pos] = (cur[pos] / 2) + 0x8;
    cur = GST_BUFFER_DATA (buf) + state->Y_stride * state->hl_rect.top;
    for (pos = state->hl_rect.top; pos <= state->hl_rect.bottom; pos++) {
      cur[state->hl_rect.left] = (cur[state->hl_rect.left] / 2) + 0x8;
      cur[state->hl_rect.right] = (cur[state->hl_rect.right] / 2) + 0x8;
      cur += state->Y_stride;
    }
  }
#endif
}
