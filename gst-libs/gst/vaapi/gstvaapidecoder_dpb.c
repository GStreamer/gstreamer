/*
 *  gstvaapidecoder_dpb.c - Decoded Picture Buffer
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#include "sysdeps.h"
#include "gstvaapidecoder_dpb.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DPB_CLASS(klass) \
    ((GstVaapiDpbClass *)(klass))

#define GST_VAAPI_DPB_GET_CLASS(obj) \
    GST_VAAPI_DPB_CLASS(GST_VAAPI_MINI_OBJECT_GET_CLASS(obj))

/**
 * GstVaapiDpb:
 *
 * A decoded picture buffer (DPB) object.
 */
struct _GstVaapiDpb
{
  /*< private > */
  GstVaapiMiniObject parent_instance;

  /*< protected > */
  GstVaapiPicture **pictures;
  guint num_pictures;
  guint max_pictures;
};

/**
 * GstVaapiDpbClass:
 *
 * The #GstVaapiDpb base class.
 */
struct _GstVaapiDpbClass
{
  /*< private > */
  GstVaapiMiniObjectClass parent_class;

  /*< protected > */
  void (*flush) (GstVaapiDpb * dpb);
    gboolean (*add) (GstVaapiDpb * dpb, GstVaapiPicture * picture);
  void (*get_neighbours) (GstVaapiDpb * dpb, GstVaapiPicture * picture,
      GstVaapiPicture ** prev_picture_ptr, GstVaapiPicture ** next_picture_ptr);
};

static const GstVaapiMiniObjectClass *gst_vaapi_dpb_class (void);

static const GstVaapiMiniObjectClass *gst_vaapi_dpb2_class (void);

/* ------------------------------------------------------------------------- */
/* --- Common utilities                                                  --- */
/* ------------------------------------------------------------------------- */

static inline GstVaapiDpb *
dpb_new (guint max_pictures)
{
  GstVaapiDpb *dpb;

  g_return_val_if_fail (max_pictures > 0, NULL);

  dpb =
      (GstVaapiDpb *) gst_vaapi_mini_object_new (max_pictures ==
      2 ? gst_vaapi_dpb2_class () : gst_vaapi_dpb_class ());
  if (!dpb)
    return NULL;

  dpb->num_pictures = 0;
  dpb->max_pictures = max_pictures;

  dpb->pictures = g_new0 (GstVaapiPicture *, max_pictures);
  if (!dpb->pictures)
    goto error;
  return dpb;

  /* ERRORS */
error:
  {
    gst_vaapi_dpb_unref (dpb);
    return NULL;
  }
}

static gint
dpb_get_oldest (GstVaapiDpb * dpb, gboolean output)
{
  gint i, lowest_pts_index;

  for (i = 0; i < dpb->num_pictures; i++) {
    if ((GST_VAAPI_PICTURE_IS_OUTPUT (dpb->pictures[i]) ^ output) == 0)
      break;
  }
  if (i == dpb->num_pictures)
    return -1;

  lowest_pts_index = i++;
  for (; i < dpb->num_pictures; i++) {
    GstVaapiPicture *const picture = dpb->pictures[i];
    if ((GST_VAAPI_PICTURE_IS_OUTPUT (picture) ^ output) != 0)
      continue;
    if (picture->poc < dpb->pictures[lowest_pts_index]->poc)
      lowest_pts_index = i;
  }
  return lowest_pts_index;
}

static void
dpb_remove_index (GstVaapiDpb * dpb, guint index)
{
  GstVaapiPicture **const pictures = dpb->pictures;
  guint num_pictures = --dpb->num_pictures;

  if (index != num_pictures)
    gst_vaapi_picture_replace (&pictures[index], pictures[num_pictures]);
  gst_vaapi_picture_replace (&pictures[num_pictures], NULL);
}

static inline gboolean
dpb_output (GstVaapiDpb * dpb, GstVaapiPicture * picture)
{
  return gst_vaapi_picture_output (picture);
}

static gboolean
dpb_bump (GstVaapiDpb * dpb)
{
  gint index;
  gboolean success;

  index = dpb_get_oldest (dpb, FALSE);
  if (index < 0)
    return FALSE;

  success = dpb_output (dpb, dpb->pictures[index]);
  if (!GST_VAAPI_PICTURE_IS_REFERENCE (dpb->pictures[index]))
    dpb_remove_index (dpb, index);
  return success;
}

static void
dpb_clear (GstVaapiDpb * dpb)
{
  guint i;

  for (i = 0; i < dpb->num_pictures; i++)
    gst_vaapi_picture_replace (&dpb->pictures[i], NULL);
  dpb->num_pictures = 0;
}

static void
dpb_flush (GstVaapiDpb * dpb)
{
  while (dpb_bump (dpb));
  dpb_clear (dpb);
}

/* ------------------------------------------------------------------------- */
/* --- Generic implementation                                            --- */
/* ------------------------------------------------------------------------- */

static gboolean
dpb_add (GstVaapiDpb * dpb, GstVaapiPicture * picture)
{
  guint i;

  // Remove all unused pictures
  i = 0;
  while (i < dpb->num_pictures) {
    GstVaapiPicture *const picture = dpb->pictures[i];
    if (GST_VAAPI_PICTURE_IS_OUTPUT (picture) &&
        !GST_VAAPI_PICTURE_IS_REFERENCE (picture))
      dpb_remove_index (dpb, i);
    else
      i++;
  }

  // Store reference decoded picture into the DPB
  if (GST_VAAPI_PICTURE_IS_REFERENCE (picture)) {
    while (dpb->num_pictures == dpb->max_pictures) {
      if (!dpb_bump (dpb))
        return FALSE;
    }
  }
  // Store non-reference decoded picture into the DPB
  else {
    if (GST_VAAPI_PICTURE_IS_SKIPPED (picture))
      return TRUE;
    while (dpb->num_pictures == dpb->max_pictures) {
      for (i = 0; i < dpb->num_pictures; i++) {
        if (!GST_VAAPI_PICTURE_IS_OUTPUT (picture) &&
            dpb->pictures[i]->poc < picture->poc)
          break;
      }
      if (i == dpb->num_pictures)
        return dpb_output (dpb, picture);
      if (!dpb_bump (dpb))
        return FALSE;
    }
  }
  gst_vaapi_picture_replace (&dpb->pictures[dpb->num_pictures++], picture);
  return TRUE;
}

static void
dpb_get_neighbours (GstVaapiDpb * dpb, GstVaapiPicture * picture,
    GstVaapiPicture ** prev_picture_ptr, GstVaapiPicture ** next_picture_ptr)
{
  GstVaapiPicture *prev_picture = NULL;
  GstVaapiPicture *next_picture = NULL;
  guint i;

  /* Find the first picture with POC > specified picture POC */
  for (i = 0; i < dpb->num_pictures; i++) {
    GstVaapiPicture *const ref_picture = dpb->pictures[i];
    if (ref_picture->poc == picture->poc) {
      if (i > 0)
        prev_picture = dpb->pictures[i - 1];
      if (i + 1 < dpb->num_pictures)
        next_picture = dpb->pictures[i + 1];
      break;
    } else if (ref_picture->poc > picture->poc) {
      next_picture = ref_picture;
      if (i > 0)
        prev_picture = dpb->pictures[i - 1];
      break;
    }
  }

  g_assert (next_picture ? next_picture->poc > picture->poc : TRUE);
  g_assert (prev_picture ? prev_picture->poc < picture->poc : TRUE);

  if (prev_picture_ptr)
    *prev_picture_ptr = prev_picture;
  if (next_picture_ptr)
    *next_picture_ptr = next_picture;
}

/* ------------------------------------------------------------------------- */
/* --- Optimized implementation for 2 reference pictures                 --- */
/* ------------------------------------------------------------------------- */

static gboolean
dpb2_add (GstVaapiDpb * dpb, GstVaapiPicture * picture)
{
  GstVaapiPicture *ref_picture;
  gint index = -1;

  g_return_val_if_fail (GST_VAAPI_IS_DPB (dpb), FALSE);
  g_return_val_if_fail (dpb->max_pictures == 2, FALSE);

  /*
   * Purpose: only store reference decoded pictures into the DPB
   *
   * This means:
   * - non-reference decoded pictures are output immediately
   * - ... thus causing older reference pictures to be output, if not already
   * - the oldest reference picture is replaced with the new reference picture
   */
  if (G_LIKELY (dpb->num_pictures == 2)) {
    index = (dpb->pictures[0]->poc > dpb->pictures[1]->poc);
    ref_picture = dpb->pictures[index];
    if (!GST_VAAPI_PICTURE_IS_OUTPUT (ref_picture)) {
      if (!dpb_output (dpb, ref_picture))
        return FALSE;
    }
  }

  if (!GST_VAAPI_PICTURE_IS_REFERENCE (picture))
    return dpb_output (dpb, picture);

  if (index < 0)
    index = dpb->num_pictures++;
  gst_vaapi_picture_replace (&dpb->pictures[index], picture);
  return TRUE;
}

static void
dpb2_get_neighbours (GstVaapiDpb * dpb, GstVaapiPicture * picture,
    GstVaapiPicture ** prev_picture_ptr, GstVaapiPicture ** next_picture_ptr)
{
  GstVaapiPicture *ref_picture, *ref_pictures[2];
  GstVaapiPicture **picture_ptr;
  guint i, index;

  g_return_if_fail (GST_VAAPI_IS_DPB (dpb));
  g_return_if_fail (dpb->max_pictures == 2);
  g_return_if_fail (GST_VAAPI_IS_PICTURE (picture));

  ref_pictures[0] = NULL;
  ref_pictures[1] = NULL;
  for (i = 0; i < dpb->num_pictures; i++) {
    ref_picture = dpb->pictures[i];
    index = ref_picture->poc > picture->poc;
    picture_ptr = &ref_pictures[index];
    if (!*picture_ptr || ((*picture_ptr)->poc > ref_picture->poc) == index)
      *picture_ptr = ref_picture;
  }

  if (prev_picture_ptr)
    *prev_picture_ptr = ref_pictures[0];
  if (next_picture_ptr)
    *next_picture_ptr = ref_pictures[1];
}

/* ------------------------------------------------------------------------- */
/* --- Interface                                                         --- */
/* ------------------------------------------------------------------------- */

static void
gst_vaapi_dpb_finalize (GstVaapiDpb * dpb)
{
  dpb_clear (dpb);
  g_free (dpb->pictures);
}

static const GstVaapiMiniObjectClass *
gst_vaapi_dpb_class (void)
{
  static const GstVaapiDpbClass GstVaapiDpbClass = {
    {sizeof (GstVaapiDpb),
        (GDestroyNotify) gst_vaapi_dpb_finalize}
    ,

    dpb_flush,
    dpb_add,
    dpb_get_neighbours
  };
  return &GstVaapiDpbClass.parent_class;
}

static const GstVaapiMiniObjectClass *
gst_vaapi_dpb2_class (void)
{
  static const GstVaapiDpbClass GstVaapiDpb2Class = {
    {sizeof (GstVaapiDpb),
        (GDestroyNotify) gst_vaapi_dpb_finalize}
    ,

    dpb_flush,
    dpb2_add,
    dpb2_get_neighbours
  };
  return &GstVaapiDpb2Class.parent_class;
}

GstVaapiDpb *
gst_vaapi_dpb_new (guint max_pictures)
{
  return dpb_new (max_pictures);
}

void
gst_vaapi_dpb_flush (GstVaapiDpb * dpb)
{
  const GstVaapiDpbClass *klass;

  g_return_if_fail (GST_VAAPI_IS_DPB (dpb));

  klass = GST_VAAPI_DPB_GET_CLASS (dpb);
  if (G_UNLIKELY (!klass || !klass->add))
    return;
  klass->flush (dpb);
}

gboolean
gst_vaapi_dpb_add (GstVaapiDpb * dpb, GstVaapiPicture * picture)
{
  const GstVaapiDpbClass *klass;

  g_return_val_if_fail (GST_VAAPI_IS_DPB (dpb), FALSE);
  g_return_val_if_fail (GST_VAAPI_IS_PICTURE (picture), FALSE);

  klass = GST_VAAPI_DPB_GET_CLASS (dpb);
  if (G_UNLIKELY (!klass || !klass->add))
    return FALSE;
  return klass->add (dpb, picture);
}

guint
gst_vaapi_dpb_size (GstVaapiDpb * dpb)
{
  g_return_val_if_fail (GST_VAAPI_IS_DPB (dpb), 0);

  return dpb->num_pictures;
}

void
gst_vaapi_dpb_get_neighbours (GstVaapiDpb * dpb, GstVaapiPicture * picture,
    GstVaapiPicture ** prev_picture_ptr, GstVaapiPicture ** next_picture_ptr)
{
  const GstVaapiDpbClass *klass;

  g_return_if_fail (GST_VAAPI_IS_DPB (dpb));
  g_return_if_fail (GST_VAAPI_IS_PICTURE (picture));

  klass = GST_VAAPI_DPB_GET_CLASS (dpb);
  if (G_UNLIKELY (!klass || !klass->get_neighbours))
    return;
  klass->get_neighbours (dpb, picture, prev_picture_ptr, next_picture_ptr);
}
