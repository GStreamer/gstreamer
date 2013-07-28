/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
gstspu_vobsub_recalc_palette (GstDVDSpu * dvdspu,
    SpuColour * dest, guint8 * idx, guint8 * alpha)
{
  SpuState *state = &dvdspu->spu_state;
  gint i;

  if (state->vobsub.current_clut[idx[0]] != 0) {
    for (i = 0; i < 4; i++, dest++) {
      guint32 col = state->vobsub.current_clut[idx[i]];

      /* Convert incoming 4-bit alpha to 8 bit for blending */
      dest->A = (alpha[i] << 4) | alpha[i];
      dest->Y = ((guint16) ((col >> 16) & 0xff)) * dest->A;
      /* U/V are stored as V/U in the clut words, so switch them */
      dest->V = ((guint16) ((col >> 8) & 0xff)) * dest->A;
      dest->U = ((guint16) (col & 0xff)) * dest->A;
    }
  } else {
    int y = 240;

    /* The CLUT presumably hasn't been set, so we'll just guess some
     * values for the non-transparent colors (white, grey, black) */
    for (i = 0; i < 4; i++, dest++) {
      dest->A = (alpha[i] << 4) | alpha[i];
      if (alpha[i] != 0) {
        dest[0].Y = y * dest[0].A;
        y -= 112;
        if (y < 0)
          y = 0;
      }
      dest[0].U = 128 * dest[0].A;
      dest[0].V = 128 * dest[0].A;
    }
  }
}

/* Recalculate the main, HL & ChgCol palettes */
static void
gstspu_vobsub_update_palettes (GstDVDSpu * dvdspu, SpuState * state)
{
  guint8 index[4];              /* Indices for the palette */
  guint8 alpha[4];              /* Alpha values the palette */

  if (state->vobsub.main_pal_dirty) {
    gstspu_vobsub_recalc_palette (dvdspu, state->vobsub.main_pal,
        state->vobsub.main_idx, state->vobsub.main_alpha);

    /* Need to refresh the hl_ctrl info copies of the main palette too */
    memcpy (state->vobsub.hl_ctrl_i.pix_ctrl_i[0].pal_cache,
        state->vobsub.main_pal, 4 * sizeof (SpuColour));
    memcpy (state->vobsub.hl_ctrl_i.pix_ctrl_i[2].pal_cache,
        state->vobsub.main_pal, 4 * sizeof (SpuColour));

    state->vobsub.main_pal_dirty = FALSE;
  }

  if (state->vobsub.hl_pal_dirty) {
    gstspu_vobsub_recalc_palette (dvdspu,
        state->vobsub.hl_ctrl_i.pix_ctrl_i[1].pal_cache, state->vobsub.hl_idx,
        state->vobsub.hl_alpha);
    state->vobsub.hl_pal_dirty = FALSE;
  }

  /* Update the offset positions for the highlight region */
  if (state->vobsub.hl_rect.top != -1) {
    state->vobsub.hl_ctrl_i.top = state->vobsub.hl_rect.top;
    state->vobsub.hl_ctrl_i.bottom = state->vobsub.hl_rect.bottom;
    state->vobsub.hl_ctrl_i.n_changes = 3;
    state->vobsub.hl_ctrl_i.pix_ctrl_i[0].left = 0;
    state->vobsub.hl_ctrl_i.pix_ctrl_i[1].left = state->vobsub.hl_rect.left;
    state->vobsub.hl_ctrl_i.pix_ctrl_i[2].left =
        state->vobsub.hl_rect.right + 1;
  }

  if (state->vobsub.line_ctrl_i_pal_dirty) {
    gint16 l, c;
    GST_LOG_OBJECT (dvdspu, "Updating chg-col-con palettes");
    for (l = 0; l < state->vobsub.n_line_ctrl_i; l++) {
      SpuVobsubLineCtrlI *cur_line_ctrl = state->vobsub.line_ctrl_i + l;

      for (c = 0; c < cur_line_ctrl->n_changes; c++) {
        SpuVobsubPixCtrlI *cur = cur_line_ctrl->pix_ctrl_i + c;

        index[3] = (cur->palette >> 28) & 0x0f;
        index[2] = (cur->palette >> 24) & 0x0f;
        index[1] = (cur->palette >> 20) & 0x0f;
        index[0] = (cur->palette >> 16) & 0x0f;

        alpha[3] = (cur->palette >> 12) & 0x0f;
        alpha[2] = (cur->palette >> 8) & 0x0f;
        alpha[1] = (cur->palette >> 4) & 0x0f;
        alpha[0] = (cur->palette) & 0x0f;
        gstspu_vobsub_recalc_palette (dvdspu, cur->pal_cache, index, alpha);
      }
    }
    state->vobsub.line_ctrl_i_pal_dirty = FALSE;
  }
}

static inline guint8
gstspu_vobsub_get_nibble (SpuState * state, guint16 * rle_offset)
{
  guint8 ret;

  if (G_UNLIKELY (*rle_offset >= state->vobsub.max_offset))
    return 0;                   /* Overran the buffer */

  gst_buffer_extract (state->vobsub.pix_buf, (*rle_offset) / 2, &ret, 1);

  /* If the offset is even, we shift the answer down 4 bits, otherwise not */
  if (*rle_offset & 0x01)
    ret &= 0x0f;
  else
    ret = ret >> 4;

  (*rle_offset)++;
  return ret;
}

static guint16
gstspu_vobsub_get_rle_code (SpuState * state, guint16 * rle_offset)
{
  guint16 code;

  code = gstspu_vobsub_get_nibble (state, rle_offset);
  if (code < 0x4) {             /* 4 .. f */
    code = (code << 4) | gstspu_vobsub_get_nibble (state, rle_offset);
    if (code < 0x10) {          /* 1x .. 3x */
      code = (code << 4) | gstspu_vobsub_get_nibble (state, rle_offset);
      if (code < 0x40) {        /* 04x .. 0fx */
        code = (code << 4) | gstspu_vobsub_get_nibble (state, rle_offset);
      }
    }
  }
  return code;
}

static inline void
gstspu_vobsub_draw_rle_run (SpuState * state, gint16 x, gint16 end,
    SpuColour * colour)
{
#if 0
  GST_LOG ("Y: %d x: %d end %d col %d %d %d %d",
      state->vobsub.cur_Y, x, end, colour->Y, colour->U, colour->V, colour->A);
#endif

  if (colour->A != 0) {
    guint32 inv_A = 0xff - colour->A;

    /* FIXME: This could be more efficient */
    while (x < end) {
      state->vobsub.out_Y[x] =
          (inv_A * state->vobsub.out_Y[x] + colour->Y) / 0xff;
      state->vobsub.out_U[x / 2] += colour->U;
      state->vobsub.out_V[x / 2] += colour->V;
      state->vobsub.out_A[x / 2] += colour->A;
      x++;
    }
    /* Update the compositing buffer so we know how much to blend later */
    *(state->vobsub.comp_last_x_ptr) = end - 1; /* end is the start of the *next* run */
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

static void gstspu_vobsub_render_line_with_chgcol (SpuState * state,
    guint8 * planes[3], guint16 * rle_offset);
static gboolean gstspu_vobsub_update_chgcol (SpuState * state);

static void
gstspu_vobsub_render_line (SpuState * state, guint8 * planes[3],
    guint16 * rle_offset)
{
  gint16 x, next_x, end, rle_code, next_draw_x;
  SpuColour *colour;

  /* Check for special case of chg_col info to use (either highlight or
   * ChgCol command */
  if (state->vobsub.cur_chg_col != NULL) {
    if (gstspu_vobsub_update_chgcol (state)) {
      /* Check the top & bottom, because we might not be within the region yet */
      if (state->vobsub.cur_Y >= state->vobsub.cur_chg_col->top &&
          state->vobsub.cur_Y <= state->vobsub.cur_chg_col->bottom) {
        gstspu_vobsub_render_line_with_chgcol (state, planes, rle_offset);
        return;
      }
    }
  }

  /* No special case. Render as normal */

  /* Set up our output pointers */
  state->vobsub.out_Y = planes[0];
  state->vobsub.out_U = state->comp_bufs[0];
  state->vobsub.out_V = state->comp_bufs[1];
  state->vobsub.out_A = state->comp_bufs[2];
  /* We always need to start our RLE decoding byte_aligned */
  *rle_offset = GST_ROUND_UP_2 (*rle_offset);

  x = state->vobsub.disp_rect.left;
  end = state->vobsub.disp_rect.right + 1;
  while (x < end) {
    rle_code = gstspu_vobsub_get_rle_code (state, rle_offset);
    colour = &state->vobsub.main_pal[rle_code & 3];
    next_x = rle_end_x (rle_code, x, end);
    next_draw_x = next_x;
    if (next_draw_x > state->vobsub.clip_rect.right)
      next_draw_x = state->vobsub.clip_rect.right;      /* ensure no overflow */
    /* Now draw the run between [x,next_x) */
    if (state->vobsub.cur_Y >= state->vobsub.clip_rect.top &&
        state->vobsub.cur_Y <= state->vobsub.clip_rect.bottom)
      gstspu_vobsub_draw_rle_run (state, x, next_draw_x, colour);
    x = next_x;
  }
}

static gboolean
gstspu_vobsub_update_chgcol (SpuState * state)
{
  if (state->vobsub.cur_chg_col == NULL)
    return FALSE;

  if (state->vobsub.cur_Y <= state->vobsub.cur_chg_col->bottom)
    return TRUE;

  while (state->vobsub.cur_chg_col < state->vobsub.cur_chg_col_end) {
    if (state->vobsub.cur_Y >= state->vobsub.cur_chg_col->top &&
        state->vobsub.cur_Y <= state->vobsub.cur_chg_col->bottom) {
#if 0
      g_print ("Stopped @ entry %d with top %d bottom %d, cur_y %d",
          (gint16) (state->vobsub.cur_chg_col - state->vobsub.line_ctrl_i),
          state->vobsub.cur_chg_col->top, state->vobsub.cur_chg_col->bottom, y);
#endif
      return TRUE;
    }
    state->vobsub.cur_chg_col++;
  }

  /* Finished all our cur_chg_col entries. Use the main palette from here on */
  state->vobsub.cur_chg_col = NULL;
  return FALSE;
}

static void
gstspu_vobsub_render_line_with_chgcol (SpuState * state, guint8 * planes[3],
    guint16 * rle_offset)
{
  SpuVobsubLineCtrlI *chg_col = state->vobsub.cur_chg_col;

  gint16 x, next_x, disp_end, rle_code, run_end, run_draw_end;
  SpuColour *colour;
  SpuVobsubPixCtrlI *cur_pix_ctrl;
  SpuVobsubPixCtrlI *next_pix_ctrl;
  SpuVobsubPixCtrlI *end_pix_ctrl;
  SpuVobsubPixCtrlI dummy_pix_ctrl;
  gint16 cur_reg_end;
  gint i;

  state->vobsub.out_Y = planes[0];
  state->vobsub.out_U = state->comp_bufs[0];
  state->vobsub.out_V = state->comp_bufs[1];
  state->vobsub.out_A = state->comp_bufs[2];

  /* We always need to start our RLE decoding byte_aligned */
  *rle_offset = GST_ROUND_UP_2 (*rle_offset);

  /* Our run will cover the display rect */
  x = state->vobsub.disp_rect.left;
  disp_end = state->vobsub.disp_rect.right + 1;

  /* Work out the first pixel control info, which may point to the dummy entry if
   * the global palette/alpha need using initally */
  cur_pix_ctrl = chg_col->pix_ctrl_i;
  end_pix_ctrl = chg_col->pix_ctrl_i + chg_col->n_changes;

  if (cur_pix_ctrl->left != 0) {
    next_pix_ctrl = cur_pix_ctrl;
    cur_pix_ctrl = &dummy_pix_ctrl;
    for (i = 0; i < 4; i++)     /* Copy the main palette to our dummy entry */
      dummy_pix_ctrl.pal_cache[i] = state->vobsub.main_pal[i];
  } else {
    next_pix_ctrl = cur_pix_ctrl + 1;
  }
  if (next_pix_ctrl < end_pix_ctrl)
    cur_reg_end = next_pix_ctrl->left;
  else
    cur_reg_end = disp_end;

  /* Render stuff */
  while (x < disp_end) {
    rle_code = gstspu_vobsub_get_rle_code (state, rle_offset);
    next_x = rle_end_x (rle_code, x, disp_end);

    /* Now draw the run between [x,next_x), crossing palette regions as needed */
    while (x < next_x) {
      run_end = MIN (next_x, cur_reg_end);

      run_draw_end = run_end;
      if (run_draw_end > state->vobsub.clip_rect.right)
        run_draw_end = state->vobsub.clip_rect.right;   /* ensure no overflow */

      if (G_LIKELY (x < run_end)) {
        colour = &cur_pix_ctrl->pal_cache[rle_code & 3];
        gstspu_vobsub_draw_rle_run (state, x, run_draw_end, colour);
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
gstspu_vobsub_blend_comp_buffers (SpuState * state, guint8 * planes[3])
{
  state->comp_left = state->vobsub.disp_rect.left;
  state->comp_right =
      MAX (state->vobsub.comp_last_x[0], state->vobsub.comp_last_x[1]);

  state->comp_left = MAX (state->comp_left, state->vobsub.clip_rect.left);
  state->comp_right = MIN (state->comp_right, state->vobsub.clip_rect.right);

  gstspu_blend_comp_buffers (state, planes);
}

static void
gstspu_vobsub_clear_comp_buffers (SpuState * state)
{
  state->comp_left = state->vobsub.clip_rect.left;
  state->comp_right = state->vobsub.clip_rect.right;

  gstspu_clear_comp_buffers (state);

  state->vobsub.comp_last_x[0] = -1;
  state->vobsub.comp_last_x[1] = -1;
}

static void
gstspu_vobsub_draw_highlight (SpuState * state,
    GstVideoFrame * frame, SpuRect * rect)
{
  guint8 *cur;
  gint16 pos;
  gint ystride;

  ystride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);

  cur = GST_VIDEO_FRAME_COMP_DATA (frame, 0) + ystride * rect->top;
  for (pos = rect->left + 1; pos < rect->right; pos++)
    cur[pos] = (cur[pos] / 2) + 0x8;
  cur = GST_VIDEO_FRAME_COMP_DATA (frame, 0) + ystride * rect->bottom;
  for (pos = rect->left + 1; pos < rect->right; pos++)
    cur[pos] = (cur[pos] / 2) + 0x8;
  cur = GST_VIDEO_FRAME_COMP_DATA (frame, 0) + ystride * rect->top;
  for (pos = rect->top; pos <= rect->bottom; pos++) {
    cur[rect->left] = (cur[rect->left] / 2) + 0x8;
    cur[rect->right] = (cur[rect->right] / 2) + 0x8;
    cur += ystride;
  }
}

void
gstspu_vobsub_render (GstDVDSpu * dvdspu, GstVideoFrame * frame)
{
  SpuState *state = &dvdspu->spu_state;
  guint8 *planes[3];            /* YUV frame pointers */
  gint y, last_y;
  gint width, height;
  gint strides[3];

  /* Set up our initial state */
  if (G_UNLIKELY (state->vobsub.pix_buf == NULL))
    return;

  /* Store the start of each plane */
  planes[0] = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  planes[1] = GST_VIDEO_FRAME_COMP_DATA (frame, 1);
  planes[2] = GST_VIDEO_FRAME_COMP_DATA (frame, 2);

  strides[0] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  strides[1] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 1);
  strides[2] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 2);

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  GST_DEBUG_OBJECT (dvdspu,
      "Rendering SPU. disp_rect %d,%d to %d,%d. hl_rect %d,%d to %d,%d",
      state->vobsub.disp_rect.left, state->vobsub.disp_rect.top,
      state->vobsub.disp_rect.right, state->vobsub.disp_rect.bottom,
      state->vobsub.hl_rect.left, state->vobsub.hl_rect.top,
      state->vobsub.hl_rect.right, state->vobsub.hl_rect.bottom);

  GST_DEBUG_OBJECT (dvdspu, "video size %d,%d", width, height);

  /* When reading RLE data, we track the offset in nibbles... */
  state->vobsub.cur_offsets[0] = state->vobsub.pix_data[0] * 2;
  state->vobsub.cur_offsets[1] = state->vobsub.pix_data[1] * 2;
  state->vobsub.max_offset = gst_buffer_get_size (state->vobsub.pix_buf) * 2;

  /* Update all the palette caches */
  gstspu_vobsub_update_palettes (dvdspu, state);

  /* Set up HL or Change Color & Contrast rect tracking */
  if (state->vobsub.hl_rect.top != -1) {
    state->vobsub.cur_chg_col = &state->vobsub.hl_ctrl_i;
    state->vobsub.cur_chg_col_end = state->vobsub.cur_chg_col + 1;
  } else if (state->vobsub.n_line_ctrl_i > 0) {
    state->vobsub.cur_chg_col = state->vobsub.line_ctrl_i;
    state->vobsub.cur_chg_col_end =
        state->vobsub.cur_chg_col + state->vobsub.n_line_ctrl_i;
  } else
    state->vobsub.cur_chg_col = NULL;

  state->vobsub.clip_rect.left = state->vobsub.disp_rect.left;
  state->vobsub.clip_rect.right = state->vobsub.disp_rect.right;

  /* center the image when display rectangle exceeds the video width */
  if (width <= state->vobsub.disp_rect.right) {
    gint left, disp_width;

    disp_width = state->vobsub.disp_rect.right - state->vobsub.disp_rect.left
        + 1;
    left = (width - disp_width) / 2;
    state->vobsub.disp_rect.left = left;
    state->vobsub.disp_rect.right = left + disp_width - 1;

    /* if it clips to the right, shift it left, but only till zero */
    if (state->vobsub.disp_rect.right >= width) {
      gint shift = state->vobsub.disp_rect.right - width - 1;
      if (shift > state->vobsub.disp_rect.left)
        shift = state->vobsub.disp_rect.left;
      state->vobsub.disp_rect.left -= shift;
      state->vobsub.disp_rect.right -= shift;
    }

    /* init clip to disp */
    state->vobsub.clip_rect.left = state->vobsub.disp_rect.left;
    state->vobsub.clip_rect.right = state->vobsub.disp_rect.right;

    /* clip right after the shift */
    if (state->vobsub.clip_rect.right >= width)
      state->vobsub.clip_rect.right = width - 1;

    GST_DEBUG_OBJECT (dvdspu,
        "clipping width to %d,%d", state->vobsub.clip_rect.left,
        state->vobsub.clip_rect.right);
  }

  /* for the height, bring it up till it fits as well as it can. We
   * assume the picture is in the lower part. We should better check where it
   * is and do something more clever. */
  state->vobsub.clip_rect.top = state->vobsub.disp_rect.top;
  state->vobsub.clip_rect.bottom = state->vobsub.disp_rect.bottom;
  if (height <= state->vobsub.disp_rect.bottom) {

    /* shift it up, but only till zero */
    gint shift = state->vobsub.disp_rect.bottom - height - 1;
    if (shift > state->vobsub.disp_rect.top)
      shift = state->vobsub.disp_rect.top;
    state->vobsub.disp_rect.top -= shift;
    state->vobsub.disp_rect.bottom -= shift;

    /* start on even line */
    if (state->vobsub.disp_rect.top & 1) {
      state->vobsub.disp_rect.top--;
      state->vobsub.disp_rect.bottom--;
    }

    /* init clip to disp */
    state->vobsub.clip_rect.top = state->vobsub.disp_rect.top;
    state->vobsub.clip_rect.bottom = state->vobsub.disp_rect.bottom;

    /* clip right after the shift */
    if (state->vobsub.clip_rect.bottom >= height)
      state->vobsub.clip_rect.bottom = height - 1;

    GST_DEBUG_OBJECT (dvdspu,
        "clipping height to %d,%d", state->vobsub.clip_rect.top,
        state->vobsub.clip_rect.bottom);
  }

  /* We start rendering from the first line of the display rect */
  y = state->vobsub.disp_rect.top;
  /* start_y is always an even number and we render lines in pairs from there,
   * accumulating 2 lines of chroma then blending it. We might need to render a
   * single line at the end if the display rect ends on an even line too. */
  last_y = (state->vobsub.disp_rect.bottom - 1) & ~(0x01);

  /* Update our plane references to the first line of the disp_rect */
  planes[0] += strides[0] * y;
  planes[1] += strides[1] * (y / 2);
  planes[2] += strides[2] * (y / 2);

  for (state->vobsub.cur_Y = y; state->vobsub.cur_Y <= last_y;
      state->vobsub.cur_Y++) {
    gboolean clip;

    clip = (state->vobsub.cur_Y < state->vobsub.clip_rect.top
        || state->vobsub.cur_Y > state->vobsub.clip_rect.bottom);

    /* Reset the compositing buffer */
    gstspu_vobsub_clear_comp_buffers (state);
    /* Render even line */
    state->vobsub.comp_last_x_ptr = state->vobsub.comp_last_x;
    gstspu_vobsub_render_line (state, planes, &state->vobsub.cur_offsets[0]);

    /* Advance the luminance output pointer */
    planes[0] += strides[0];

    state->vobsub.cur_Y++;

    /* Render odd line */
    state->vobsub.comp_last_x_ptr = state->vobsub.comp_last_x + 1;
    gstspu_vobsub_render_line (state, planes, &state->vobsub.cur_offsets[1]);

    if (!clip) {
      /* Blend the accumulated UV compositing buffers onto the output */
      gstspu_vobsub_blend_comp_buffers (state, planes);
    }

    /* Update all the output pointers */
    planes[0] += strides[0];
    planes[1] += strides[1];
    planes[2] += strides[2];
  }

  if (state->vobsub.cur_Y == state->vobsub.disp_rect.bottom) {
    gboolean clip;

    clip = (state->vobsub.cur_Y < state->vobsub.clip_rect.top
        || state->vobsub.cur_Y > state->vobsub.clip_rect.bottom);

    g_assert ((state->vobsub.disp_rect.bottom & 0x01) == 0);

    if (!clip) {
      /* Render a remaining lone last even line. y already has the correct value
       * after the above loop exited. */
      gstspu_vobsub_clear_comp_buffers (state);
      state->vobsub.comp_last_x_ptr = state->vobsub.comp_last_x;
      gstspu_vobsub_render_line (state, planes, &state->vobsub.cur_offsets[0]);
      gstspu_vobsub_blend_comp_buffers (state, planes);
    }
  }

  /* for debugging purposes, draw a faint rectangle at the edges of the disp_rect */
  if ((dvdspu_debug_flags & GST_DVD_SPU_DEBUG_RENDER_RECTANGLE) != 0) {
    gstspu_vobsub_draw_highlight (state, frame, &state->vobsub.disp_rect);
  }
  /* For debugging purposes, draw a faint rectangle around the highlight rect */
  if ((dvdspu_debug_flags & GST_DVD_SPU_DEBUG_HIGHLIGHT_RECTANGLE) != 0
      && state->vobsub.hl_rect.top != -1) {
    gstspu_vobsub_draw_highlight (state, frame, &state->vobsub.hl_rect);
  }
}
