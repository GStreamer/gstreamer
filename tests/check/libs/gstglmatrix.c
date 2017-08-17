/* GStreamer
 *
 * Copyright (C) 2014 Matthew Waters <ystreet00@gmail.com>
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
#  include "config.h"
#endif

#include <gst/gl/gstglutils.c>
#undef GST_CAT_DEFAULT
#include <gst/check/gstcheck.h>

#define EPSILON 0.0001f
#define FEQ(a,b) (fabs(a-b) < EPSILON)

static void
debug_matrix (const float *m)
{
  int i;
  for (i = 0; i < 4; i++) {
    GST_DEBUG ("%10.4f %10.4f %10.4f %10.4f", m[i * 4 + 0], m[i * 4 + 1],
        m[i * 4 + 2], m[i * 4 + 3]);
  }
}

GST_START_TEST (test_matrix_multiply)
{
  /* A * B == C */
  const float A[] = {
    1., 1., 2., 5.,
    0., 3., 0., 1.,
    2., 0., 3., 1.,
    3., 2., 1., 0.,
  };

  const float B[] = {
    3., 1., 0., 2.,
    1., 0., 3., 2.,
    0., 1., 2., 3.,
    3., 2., 1., 0.,
  };

  const float C[] = {
    19., 13., 12., 10.,
    6., 2., 10., 6.,
    9., 7., 7., 13.,
    11., 4., 8., 13.,
  };

  float res[16];
  int i;

  gst_gl_multiply_matrix4 (A, B, res);
  GST_DEBUG ("result");
  debug_matrix (res);

  for (i = 0; i < G_N_ELEMENTS (res); i++) {
    fail_unless (FEQ (res[i], C[i]), "value %f at index %u does not match "
        "expected value %f", res[i], i, C[i]);
  }
}

GST_END_TEST;

GST_START_TEST (test_matrix_ndc)
{
  GstBuffer *buffer = gst_buffer_new ();
  GstVideoAffineTransformationMeta *aff_meta;
  float res[16];
  int i;

  const float m[] = {
    1., 0., 0., 0.,
    0., 1., 0., 0.,
    0., 0., 1., 0.,
    0., 0., 0., 1.,
  };

  const float n[] = {
    4., 6., 4., 9.,
    1., 5., 8., 2.,
    9., 3., 5., 8.,
    3., 7., 9., 1.,
  };

  aff_meta = gst_buffer_add_video_affine_transformation_meta (buffer);

  /* test default identity matrix */
  gst_gl_get_affine_transformation_meta_as_ndc (aff_meta, res);
  GST_DEBUG ("result");
  debug_matrix (res);

  for (i = 0; i < G_N_ELEMENTS (res); i++) {
    fail_unless (FEQ (res[i], m[i]), "value %f at index %u does not match "
        "expected value %f", res[i], i, m[i]);
  }

  /* test setting and receiving the same values */
  gst_gl_set_affine_transformation_meta_from_ndc (aff_meta, n);
  gst_gl_get_affine_transformation_meta_as_ndc (aff_meta, res);

  GST_DEBUG ("result");
  debug_matrix (res);

  for (i = 0; i < G_N_ELEMENTS (res); i++) {
    fail_unless (FEQ (res[i], n[i]), "value %f at index %u does not match "
        "expected value %f", res[i], i, n[i]);
  }

  gst_buffer_unref (buffer);
}

GST_END_TEST;
#if 0
static void
transpose_matrix4 (float *m, float *res)
{
  int i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      int idx = i + (j * 4);
      int swapped_idx = j + (i * 4);

      if (i == j)
        fail_unless (idx == swapped_idx);

      res[swapped_idx] = m[idx];
    }
  }
}

static float
dot4 (float *v1, float *v2)
{
  GST_TRACE ("%.4f * %.4f + %.4f * %.4f + %.4f * %.4f + %.4f * %.4f",
      v1[0], v2[0], v1[1], v2[1], v1[2], v2[2], v1[3], v2[3]);
  return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2] + v1[3] * v2[3];
}

/* m * v */
static void
_matrix_mult_vertex4 (float *m, float *v, float *res)
{
  res[0] = dot4 (&m[0], v);
  res[1] = dot4 (&m[4], v);
  res[2] = dot4 (&m[8], v);
  res[3] = dot4 (&m[12], v);
}

/* v * m */
static void
_vertex_mult_matrix4 (float *v, float *m, float *res)
{
  float tmp[16] = { 0., };

  transpose_matrix4 (m, tmp);
  _matrix_mult_vertex4 (tmp, v, res);
}

GST_START_TEST (test_matrix_vertex_identity)
{
  float identity[] = {
    1., 0., 0., 0.,
    0., 1., 0., 0.,
    0., 0., 1., 0.,
    0., 0., 0., 1.,
  };

  float v[] = { 1., 1., 1., 1. };
  float res[4] = { 0., };
  int i;

  _vertex_mult_matrix4 (v, identity, res);
  GST_DEBUG ("vertex: %.4f %.4f %.4f %.4f", v[0], v[1], v[2], v[3]);
  GST_DEBUG ("result: %.4f %.4f %.4f %.4f", res[0], res[1], res[2], res[3]);

  for (i = 0; i < 4; i++) {
    fail_unless (FEQ (res[i], v[i]), "value %f at index %u does not match "
        "expected value %f", res[i], i, v[i]);
  }

  _matrix_mult_vertex4 (identity, v, res);
  GST_DEBUG ("vertex: %.4f %.4f %.4f %.4f", v[0], v[1], v[2], v[3]);
  GST_DEBUG ("result: %.4f %.4f %.4f %.4f", res[0], res[1], res[2], res[3]);

  for (i = 0; i < 4; i++) {
    fail_unless (FEQ (res[i], v[i]), "value %f at index %u does not match "
        "expected value %f", res[i], i, v[i]);
  }
}

GST_END_TEST;

GST_START_TEST (test_matrix_vertex_scale)
{
  float scale[] = {
    1.5, 0., 0., 0.,
    0., 2.5, 0., 0.,
    0., 0., 3., 0.,
    0., 0., 0., 1.,
  };

  float v[] = { 1., 1., 1., 1. };
  float expected[] = { 1.5, 2.5, 3., 1. };
  float res[4] = { 0., };
  int i;

  _vertex_mult_matrix4 (v, scale, res);
  GST_DEBUG ("vertex: %.4f %.4f %.4f %.4f", v[0], v[1], v[2], v[3]);
  GST_DEBUG ("result: %.4f %.4f %.4f %.4f", res[0], res[1], res[2], res[3]);

  for (i = 0; i < 4; i++) {
    fail_unless (FEQ (res[i], expected[i]),
        "value %f at index %u does not match " "expected value %f", res[i], i,
        expected[i]);
  }

  _matrix_mult_vertex4 (scale, v, res);
  GST_DEBUG ("vertex: %.4f %.4f %.4f %.4f", v[0], v[1], v[2], v[3]);
  GST_DEBUG ("result: %.4f %.4f %.4f %.4f", res[0], res[1], res[2], res[3]);

  for (i = 0; i < 4; i++) {
    fail_unless (FEQ (res[i], expected[i]),
        "value %f at index %u does not match " "expected value %f", res[i], i,
        expected[i]);
  }
}

GST_END_TEST;
#endif

static Suite *
gst_gl_upload_suite (void)
{
  Suite *s = suite_create ("GstGLMatrix");
  TCase *tc_chain = tcase_create ("matrix");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_matrix_multiply);
  tcase_add_test (tc_chain, test_matrix_ndc);

  return s;
}

GST_CHECK_MAIN (gst_gl_upload);
