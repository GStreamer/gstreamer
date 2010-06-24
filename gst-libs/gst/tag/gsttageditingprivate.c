/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#include "gsttageditingprivate.h"

#include <string.h>

gint
gst_tag_image_orientation_to_exif_value (const gchar * str)
{
  if (str == NULL)
    goto end;

  if (strcmp (str, "rotate-0") == 0)
    return 1;
  else if (strcmp (str, "flip-rotate-0") == 0)
    return 2;
  else if (strcmp (str, "rotate-180") == 0)
    return 3;
  else if (strcmp (str, "flip-rotate-180") == 0)
    return 4;
  else if (strcmp (str, "flip-rotate-270") == 0)
    return 5;
  else if (strcmp (str, "rotate-90") == 0)
    return 6;
  else if (strcmp (str, "flip-rotate-90") == 0)
    return 7;
  else if (strcmp (str, "rotate-270") == 0)
    return 8;

end:
  GST_WARNING ("Invalid image orientation tag: %s", str);
  return -1;
}

const gchar *
gst_tag_image_orientation_from_exif_value (gint value)
{
  switch (value) {
    case 1:
      return "rotate-0";
    case 2:
      return "flip-rotate-0";
    case 3:
      return "rotate-180";
    case 4:
      return "flip-rotate-180";
    case 5:
      return "flip-rotate-270";
    case 6:
      return "rotate-90";
    case 7:
      return "flip-rotate-90";
    case 8:
      return "rotate-270";
    default:
      GST_WARNING ("Invalid tiff orientation tag value: %d", value);
      return NULL;
  }
}
