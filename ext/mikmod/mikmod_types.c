/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "config.h"
#endif

#include <gst/gst.h>
#include "mikmod_types.h"
#include <string.h>		/* memcmp */
#include <ctype.h>		/* isdigit */

#define MODULEHEADERSIZE 0x438


gboolean
MOD_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf) + MODULEHEADERSIZE;

  /* Protracker and variants */
  if ((!memcmp (data, "M.K.", 4)) || (!memcmp (data, "M!K!", 4)))
    return TRUE;

  /* Star Tracker */
  if (((!memcmp (data, "FLT", 3)) || (!memcmp (data, "EXO", 3)))
      && (isdigit (data[3])))
    return TRUE;

  /* Oktalyzer (Amiga) */
  if (!memcmp (data, "OKTA", 4))
    return TRUE;

  /* Oktalyser (Atari) */
  if (!memcmp (data, "CD81", 4))
    return TRUE;

  /* Fasttracker */
  if ((!memcmp (data + 1, "CHN", 3)) && (isdigit (data[0])))
    return TRUE;

  /* Fasttracker or Taketracker */
  if (((!memcmp (data + 2, "CH", 2)) || (!memcmp (data + 2, "CN", 2)))
      && (isdigit (data[0])) && (isdigit (data[1])))
    return TRUE;

  return FALSE;
}

gboolean
Mod_669_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "if", 2) || !memcmp (data, "JN", 2))
    return TRUE;

  return FALSE;
}

gboolean
Amf_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (memcmp (data, "AMF", 3))
    return FALSE;

  data = GST_BUFFER_DATA (buf) + 3;

  if (((gint) * data >= 10) && ((gint) * data <= 14))
    return TRUE;

  return FALSE;
}

gboolean
Dsm_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "RIFF", 4) && !memcmp (data + 8, "DSMF", 4))
    return TRUE;

  return FALSE;
}

gboolean
Fam_CheckType (GstBuffer * buf)
{
  gchar *data;
  static unsigned char FARSIG[4 + 3] = { 'F', 'A', 'R', 0xfe, 13, 10, 26 };

  data = GST_BUFFER_DATA (buf);

  if ((memcmp (data, FARSIG, 4)) || (memcmp (data + 44, FARSIG + 4, 3)))
    return FALSE;

  return 1;
}

gboolean
Gdm_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "GDM\xfe", 4) && !memcmp (data + 71, "GMFS", 4))
    return TRUE;

  return FALSE;
}

gboolean
Imf_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf) + 0x3c;

  if (!memcmp (data, "IM10", 4))
    return TRUE;

  return FALSE;
}

gboolean
It_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "IMPM", 4))
    return TRUE;

  return FALSE;
}

gboolean
M15_CheckType (GstBuffer * buf)
{
  /* FIXME: M15 CheckType to do */
  return FALSE;
}

gboolean
Med_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if ((!memcmp (data, "MMD0", 4)) || (memcmp (data, "MMD1", 4)))
    return TRUE;

  return FALSE;
}

gboolean
Mtm_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "MTM", 3))
    return TRUE;

  return FALSE;
}

gboolean
Okt_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (!memcmp (data, "OKTSONG", 8))
    return TRUE;

  return FALSE;
}

gboolean
S3m_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf) + 0x2c;

  if (!memcmp (data, "SCRM", 4))
    return TRUE;

  return FALSE;
}

gboolean
Xm_CheckType (GstBuffer * buf)
{
  gchar *data;

  data = GST_BUFFER_DATA (buf);

  if (memcmp (data, "Extended Module: ", 17))
    return FALSE;

  if (data[37] == 0x1a)
    return TRUE;

  return FALSE;
}
