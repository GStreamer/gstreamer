
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>

typedef struct
{
  double comp[3];
} Color;

typedef struct
{
  Color pre_offset;
  double matrix[3][3];
  Color post_offset;
} ColorMatrix;


/* convert uint8 RGB values to float */
ColorMatrix rgb255_to_rgb = {
  {{0, 0, 0}},
  {{(1 / 255.0), 0, 0},
      {0, (1 / 255.0), 0},
      {0, 0, (1 / 255.0)}},
  {{0, 0, 0}}
};
ColorMatrix rgb_to_rgb255;

/* convert uint8 YUV values to float as per ITU-R.601
 * technically, Y, Cr, Cb to E_Y, E_C_B, E_C_R */
ColorMatrix ycbcr601_to_yuv = {
  {{-16, -128, -128}},
  {{(1 / 219.0), 0, 0},
      {0, (1 / 224.0), 0},
      {0, 0, (1 / 224.0)}},
  {{0, 0, 0}}
};
ColorMatrix yuv_to_ycbcr601;

/* convert RGB to YUV as per ITU-R.601
 * technically, E_R, E_G, E_B to E_Y, E_C_B, E_C_R */
ColorMatrix rgb_to_yuv = {
  {{0, 0, 0}},
  {{0.299, 0.587, 0.114},
      {0.500, -0.419, -0.081},
      {-0.169, -0.331, 0.500}},
  {{0, 0, 0}}
};
ColorMatrix yuv_to_rgb;

ColorMatrix compress = {
  {{0, 0, 0}},
  {{0.50, 0, 0},
      {0, 0.5, 0},
      {0, 0, 0.500}},
  {{0.25, 0.25, 0.25}}
};

/* red mask */
ColorMatrix red_mask = {
  {{0, 0, 0}},
  {{1, 1, 1},
      {0, 0, 0},
      {0, 0, 0}},
  {{0, 0, 0}}
};

double colors[][3] = {
  {0, 0, 0},
  {255, 0, 0},
  {0, 255, 0},
  {0, 0, 255}
};


void
color_dump (const double *a)
{
  printf (" %g, %g, %g\n", a[0], a[1], a[2]);
}

void
color_matrix_dump (ColorMatrix * m)
{
  printf ("pre: %g, %g, %g\n",
      m->pre_offset.comp[0], m->pre_offset.comp[1], m->pre_offset.comp[2]);
  printf ("  %g, %g, %g\n", m->matrix[0][0], m->matrix[0][1], m->matrix[0][2]);
  printf ("  %g, %g, %g\n", m->matrix[1][0], m->matrix[1][1], m->matrix[1][2]);
  printf ("  %g, %g, %g\n", m->matrix[2][0], m->matrix[2][1], m->matrix[2][2]);
  printf ("post: %g, %g, %g\n",
      m->post_offset.comp[0], m->post_offset.comp[1], m->post_offset.comp[2]);
}

void
color_matrix_apply_color (Color * a, const ColorMatrix * b)
{
  Color d;
  int i;

  a->comp[0] += b->pre_offset.comp[0];
  a->comp[1] += b->pre_offset.comp[1];
  a->comp[2] += b->pre_offset.comp[2];

  for (i = 0; i < 3; i++) {
    d.comp[i] = a->comp[0] * b->matrix[i][0];
    d.comp[i] += a->comp[1] * b->matrix[i][1];
    d.comp[i] += a->comp[2] * b->matrix[i][2];
  }

  d.comp[0] += b->post_offset.comp[0];
  d.comp[1] += b->post_offset.comp[1];
  d.comp[2] += b->post_offset.comp[2];

  *a = d;
}

void
color_matrix_init (ColorMatrix * a)
{
  memset (a, 0, sizeof (*a));
  a->matrix[0][0] = 1.0;
  a->matrix[1][1] = 1.0;
  a->matrix[2][2] = 1.0;
}

void
color_matrix_apply (ColorMatrix * a, ColorMatrix * b)
{
  ColorMatrix d;
  int i, j;

  d.pre_offset = a->pre_offset;

  d.post_offset = a->post_offset;
  color_matrix_apply_color (&d.post_offset, b);

  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      d.matrix[i][j] =
          a->matrix[i][0] * b->matrix[0][j] +
          a->matrix[i][1] * b->matrix[1][j] + a->matrix[i][2] * b->matrix[2][j];
    }
  }

  *a = d;
}

void
color_matrix_invert (ColorMatrix * a, ColorMatrix * b)
{
  int i, j;
  double det;

  a->post_offset.comp[0] = -b->pre_offset.comp[0];
  a->post_offset.comp[1] = -b->pre_offset.comp[1];
  a->post_offset.comp[2] = -b->pre_offset.comp[2];

  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      a->matrix[j][i] =
          b->matrix[(i + 1) % 3][(j + 1) % 3] * b->matrix[(i + 2) % 3][(j +
              2) % 3] - b->matrix[(i + 1) % 3][(j + 2) % 3] * b->matrix[(i +
              2) % 3][(j + 1) % 3];
    }
  }

  det = a->matrix[0][0] * b->matrix[0][0];
  det += a->matrix[0][1] * b->matrix[1][0];
  det += a->matrix[0][2] * b->matrix[2][0];

  for (j = 0; j < 3; j++) {
    for (i = 0; i < 3; i++) {
      a->matrix[j][i] /= det;
    }
  }

  a->pre_offset.comp[0] = -b->post_offset.comp[0];
  a->pre_offset.comp[1] = -b->post_offset.comp[1];
  a->pre_offset.comp[2] = -b->post_offset.comp[2];
}

void
init (void)
{
  color_matrix_invert (&yuv_to_rgb, &rgb_to_yuv);
  color_matrix_invert (&yuv_to_ycbcr601, &ycbcr601_to_yuv);
  color_matrix_invert (&rgb_to_rgb255, &rgb255_to_rgb);
#if 0
  color_matrix_dump (&yuv_to_rgb);
  color_matrix_dump (&yuv_to_ycbcr601);
  color_matrix_dump (&rgb_to_rgb255);
#endif
}

int
main (int argc, char *argv[])
{
  ColorMatrix want;
  ColorMatrix actual;
  ColorMatrix actual_inv;
  ColorMatrix a;

  init ();

#if 0
  int i;

  for (i = 0; i < 4; i++) {
    double color[3];

    printf ("%d:\n", i);

    color_copy (color, colors[i]);
    color_matrix_apply_color (color, &rgb255_to_rgb);
    color_matrix_apply_color (color, &rgb_to_yuv);
    color_dump (color);
  }
#endif

  color_matrix_init (&want);
  color_matrix_apply (&want, &ycbcr601_to_yuv);
  color_matrix_apply (&want, &yuv_to_rgb);
  color_matrix_apply (&want, &compress);
  color_matrix_apply (&want, &compress);
  //color_matrix_apply (&want, &compress);

  color_matrix_init (&actual);
  color_matrix_apply (&actual, &rgb255_to_rgb);

  /* calc X such that actual * X = want */

  color_matrix_invert (&actual_inv, &actual);

  a = actual_inv;
  color_matrix_apply (&a, &want);

  color_matrix_dump (&a);


  return 0;
}
