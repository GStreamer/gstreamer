/*
 * y4mreader.c - Y4M parser
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include "y4mreader.h"

/* format documentation:
 * http://wiki.multimedia.cx/index.php?title=YUV4MPEG2 */

static inline gboolean
parse_int (const gchar * str, guint * out_value_ptr)
{
  gint saved_errno;
  glong value;
  gboolean ret;

  if (!str)
    return FALSE;
  str += 1;
  if (*str == '\0')
    return FALSE;

  saved_errno = errno;
  errno = 0;
  value = strtol (str, NULL, 0);
  ret = (errno == 0);
  errno = saved_errno;
  if (value > 0 && value <= G_MAXUINT)
    *out_value_ptr = value;
  else
    ret = FALSE;

  return ret;
}

static gboolean
parse_header (Y4MReader * file)
{
  gint i, j, b;
  guint8 header[BUFSIZ];
  size_t s;
  gchar *str;

  memset (header, 0, BUFSIZ);
  s = fread (header, 1, 9, file->fp);
  if (s < 9)
    return FALSE;

  if (memcmp (header, "YUV4MPEG2", 9) != 0)
    return FALSE;

  for (i = 9; i < BUFSIZ - 1; i++) {
    b = fgetc (file->fp);
    if (b == EOF)
      return FALSE;
    if (b == 0xa)
      break;
    header[i] = b;
  }

  if (i == BUFSIZ - 1)
    return FALSE;

  j = 9;
  while (j < i) {
    if ((header[j] != 0x20) && (header[j - 1] == 0x20)) {
      switch (header[j]) {
        case 'W':
          if (!parse_int ((gchar *) & header[j], &file->width))
            return FALSE;
          break;
        case 'H':
          if (!parse_int ((gchar *) & header[j], &file->height))
            return FALSE;
          break;
        case 'C':
          str = (char *) &header[j + 1];
          if (strncmp (str, "420", 3) != 0) {
            g_warning ("Unsupported chroma subsampling.");
            return FALSE;       /* unsupported chroma subsampling */
          }
          break;
        case 'I':
          str = (char *) &header[j + 1];
          if (*str != 'p' && *str != '?') {
            g_warning ("Interlaced content are not supported.");
            return FALSE;       /* interlaced is unsupported */
          }
          break;
        case 'F':              /* frame rate ratio */
        {
          guint num, den;

          if (!parse_int ((gchar *) & header[j], &num))
            return FALSE;
          while ((header[j] != ':') && (j < i))
            j++;
          if (!parse_int ((gchar *) & header[j], &den))
            return FALSE;

          if (num <= 0 || den <= 0) {
            file->fps_n = 30;   /* default to 30 fps */
            file->fps_d = 1;
          } else {
            file->fps_n = num;
            file->fps_d = den;
          }
          break;
        }
        case 'A':              /* sample aspect ration */
          break;
        case 'X':              /* metadata */
          break;
        default:
          break;
      }
    }
    j++;
  }

  return TRUE;
}

Y4MReader *
y4m_reader_open (const gchar * filename)
{
  Y4MReader *imagefile;

  imagefile = g_new0 (Y4MReader, 1);

  if (filename) {
    imagefile->fp = fopen (filename, "r");
    if (!imagefile->fp) {
      g_warning ("open file %s error", filename);
      goto bail;
    }
  } else {
    imagefile->fp = stdin;
  }

  if (!parse_header (imagefile))
    goto bail;

  return imagefile;

bail:
  if (imagefile->fp && imagefile->fp != stdin)
    fclose (imagefile->fp);

  g_free (imagefile);
  return NULL;
}

void
y4m_reader_close (Y4MReader * file)
{
  g_return_if_fail (file);

  if (file->fp && file->fp != stdin)
    fclose (file->fp);

  g_free (file);
}

static gboolean
skip_frame_header (Y4MReader * file)
{
  gint i, b;
  guint8 header[BUFSIZ];
  size_t s;

  memset (header, 0, BUFSIZ);
  s = fread (header, 1, 5, file->fp);
  if (s < 5)
    return FALSE;

  if (memcmp (header, "FRAME", 5) != 0)
    return FALSE;

  for (i = 5; i < BUFSIZ - 1; i++) {
    b = fgetc (file->fp);
    if (b == EOF)
      return FALSE;
    if (b == 0xa)
      break;
    header[i] = b;
  }

  return (i < BUFSIZ - 1);
}

gboolean
y4m_reader_load_image (Y4MReader * file, GstVaapiImage * image)
{
  guint8 *plane;
  size_t s;
  guint frame_size, stride, i;

  g_return_val_if_fail (gst_vaapi_image_is_mapped (image), FALSE);
  g_return_val_if_fail (file && file->fp, FALSE);

  /* only valid for I420 */
  frame_size = file->height * file->width * 3 / 2;
  if (gst_vaapi_image_get_data_size (image) < frame_size)
    return FALSE;
  if (gst_vaapi_image_get_plane_count (image) != 3)
    return FALSE;

  if (!skip_frame_header (file))
    return FALSE;

  /* Y plane */
  plane = gst_vaapi_image_get_plane (image, 0);
  stride = gst_vaapi_image_get_pitch (image, 0);
  for (i = 0; i < file->height; i++) {
    s = fread (plane, 1, file->width, file->fp);
    if (s != file->width)
      return FALSE;
    plane += stride;
  }

  /* U plane */
  plane = gst_vaapi_image_get_plane (image, 1);
  stride = gst_vaapi_image_get_pitch (image, 1);
  for (i = 0; i < file->height / 2; i++) {
    s = fread (plane, 1, file->width / 2, file->fp);
    if (s != file->width / 2)
      return FALSE;
    plane += stride;
  }

  /* V plane */
  plane = gst_vaapi_image_get_plane (image, 2);
  stride = gst_vaapi_image_get_pitch (image, 2);
  for (i = 0; i < file->height / 2; i++) {
    s = fread (plane, 1, file->width / 2, file->fp);
    if (s != file->width / 2)
      return FALSE;
    plane += stride;
  }

  return TRUE;
}
