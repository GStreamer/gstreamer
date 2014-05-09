/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 *
 * m3u8.c:
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

#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <glib.h>
#include <string.h>

#include "gstfragmented.h"
#include "m3u8.h"

#define GST_CAT_DEFAULT fragmented_debug

static GstM3U8 *gst_m3u8_new (void);
static void gst_m3u8_free (GstM3U8 * m3u8);
static gboolean gst_m3u8_update (GstM3U8 * m3u8, gchar * data,
    gboolean * updated);
static GstM3U8MediaFile *gst_m3u8_media_file_new (gchar * uri,
    gchar * title, GstClockTime duration, guint sequence);
static void gst_m3u8_media_file_free (GstM3U8MediaFile * self);
gchar *uri_join (const gchar * uri, const gchar * path);

static GstM3U8 *
gst_m3u8_new (void)
{
  GstM3U8 *m3u8;

  m3u8 = g_new0 (GstM3U8, 1);

  return m3u8;
}

static void
gst_m3u8_set_uri (GstM3U8 * self, gchar * uri)
{
  g_return_if_fail (self != NULL);

  if (self->uri)
    g_free (self->uri);
  self->uri = uri;
}

static void
gst_m3u8_free (GstM3U8 * self)
{
  g_return_if_fail (self != NULL);

  g_free (self->uri);
  g_free (self->codecs);
  g_free (self->key);

  g_list_foreach (self->files, (GFunc) gst_m3u8_media_file_free, NULL);
  g_list_free (self->files);

  g_free (self->last_data);
  g_list_foreach (self->lists, (GFunc) gst_m3u8_free, NULL);
  g_list_free (self->lists);
  g_list_foreach (self->iframe_lists, (GFunc) gst_m3u8_free, NULL);
  g_list_free (self->iframe_lists);

  g_free (self);
}

static GstM3U8MediaFile *
gst_m3u8_media_file_new (gchar * uri, gchar * title, GstClockTime duration,
    guint sequence)
{
  GstM3U8MediaFile *file;

  file = g_new0 (GstM3U8MediaFile, 1);
  file->uri = uri;
  file->title = title;
  file->duration = duration;
  file->sequence = sequence;

  return file;
}

static void
gst_m3u8_media_file_free (GstM3U8MediaFile * self)
{
  g_return_if_fail (self != NULL);

  g_free (self->title);
  g_free (self->uri);
  g_free (self->key);
  g_free (self);
}

static gboolean
int_from_string (gchar * ptr, gchar ** endptr, gint * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (ret > G_MAXINT || ret < G_MININT) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gint) ret;

  return end != ptr;
}

static gboolean
int64_from_string (gchar * ptr, gchar ** endptr, gint64 * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = ret;

  return end != ptr;
}

static gboolean
double_from_string (gchar * ptr, gchar ** endptr, gdouble * val)
{
  gchar *end;
  gdouble ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtod (ptr, &end);
  if ((errno == ERANGE && (ret == HUGE_VAL || ret == -HUGE_VAL))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (!isfinite (ret)) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gdouble) ret;

  return end != ptr;
}

static gboolean
parse_attributes (gchar ** ptr, gchar ** a, gchar ** v)
{
  gchar *end = NULL, *p;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (*ptr != NULL, FALSE);
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (v != NULL, FALSE);

  /* [attribute=value,]* */

  *a = *ptr;
  end = p = g_utf8_strchr (*ptr, -1, ',');
  if (end) {
    gchar *q = g_utf8_strchr (*ptr, -1, '"');
    if (q && q < end) {
      /* special case, such as CODECS="avc1.77.30, mp4a.40.2" */
      q = g_utf8_next_char (q);
      if (q) {
        q = g_utf8_strchr (q, -1, '"');
      }
      if (q) {
        end = p = g_utf8_strchr (q, -1, ',');
      }
    }
  }
  if (end) {
    do {
      end = g_utf8_next_char (end);
    } while (end && *end == ' ');
    *p = '\0';
  }

  *v = p = g_utf8_strchr (*ptr, -1, '=');
  if (*v) {
    *v = g_utf8_next_char (*v);
    *p = '\0';
  } else {
    GST_WARNING ("missing = after attribute");
    return FALSE;
  }

  *ptr = end;
  return TRUE;
}

static gint
_m3u8_compare_uri (GstM3U8 * a, gchar * uri)
{
  g_return_val_if_fail (a != NULL, 0);
  g_return_val_if_fail (uri != NULL, 0);

  return g_strcmp0 (a->uri, uri);
}

static gint
gst_m3u8_compare_playlist_by_bitrate (gconstpointer a, gconstpointer b)
{
  return ((GstM3U8 *) (a))->bandwidth - ((GstM3U8 *) (b))->bandwidth;
}

/*
 * @data: a m3u8 playlist text data, taking ownership
 */
static gboolean
gst_m3u8_update (GstM3U8 * self, gchar * data, gboolean * updated)
{
  gint val;
  GstClockTime duration;
  gchar *title, *end;
  gboolean discontinuity = FALSE;
  GstM3U8 *list;
  gboolean have_iv = FALSE;
  guint8 iv[16] = { 0, };
  gint64 size = -1, offset = -1;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (updated != NULL, FALSE);

  *updated = TRUE;

  /* check if the data changed since last update */
  if (self->last_data && g_str_equal (self->last_data, data)) {
    GST_DEBUG ("Playlist is the same as previous one");
    *updated = FALSE;
    g_free (data);
    return TRUE;
  }

  if (!g_str_has_prefix (data, "#EXTM3U")) {
    GST_WARNING ("Data doesn't start with #EXTM3U");
    *updated = FALSE;
    g_free (data);
    return FALSE;
  }

  g_free (self->last_data);
  self->last_data = data;

  if (self->files) {
    g_list_foreach (self->files, (GFunc) gst_m3u8_media_file_free, NULL);
    g_list_free (self->files);
    self->files = NULL;
  }

  /* By default, allow caching */
  self->allowcache = TRUE;

  list = NULL;
  duration = 0;
  title = NULL;
  data += 7;
  while (TRUE) {
    gchar *r;

    end = g_utf8_strchr (data, -1, '\n');
    if (end)
      *end = '\0';

    r = g_utf8_strchr (data, -1, '\r');
    if (r)
      *r = '\0';

    if (data[0] != '#' && data[0] != '\0') {
      if (duration <= 0 && list == NULL) {
        GST_LOG ("%s: got line without EXTINF or EXTSTREAMINF, dropping", data);
        goto next_line;
      }

      data = uri_join (self->uri, data);
      if (data == NULL)
        goto next_line;

      if (list != NULL) {
        if (g_list_find_custom (self->lists, data,
                (GCompareFunc) _m3u8_compare_uri)) {
          GST_DEBUG ("Already have a list with this URI");
          gst_m3u8_free (list);
          g_free (data);
        } else {
          gst_m3u8_set_uri (list, data);
          self->lists = g_list_append (self->lists, list);
        }
        list = NULL;
      } else {
        GstM3U8MediaFile *file;
        file =
            gst_m3u8_media_file_new (data, title, duration,
            self->mediasequence++);

        /* set encryption params */
        file->key = g_strdup (self->key);
        if (file->key) {
          if (have_iv) {
            memcpy (file->iv, iv, sizeof (iv));
          } else {
            guint8 *iv = file->iv + 12;
            GST_WRITE_UINT32_BE (iv + 12, file->sequence);
          }
        }

        if (size != -1) {
          file->size = size;
          if (offset != -1) {
            file->offset = offset;
          } else {
            GstM3U8MediaFile *prev =
                self->files ? g_list_last (self->files)->data : NULL;

            if (!prev) {
              offset = 0;
            } else {
              offset = prev->offset + prev->size;
            }
          }
        } else {
          file->size = -1;
          file->offset = 0;
        }

        file->discont = discontinuity;

        duration = 0;
        title = NULL;
        discontinuity = FALSE;
        size = offset = -1;
        self->files = g_list_append (self->files, file);
      }

    } else if (g_str_has_prefix (data, "#EXT-X-ENDLIST")) {
      self->endlist = TRUE;
    } else if (g_str_has_prefix (data, "#EXT-X-VERSION:")) {
      if (int_from_string (data + 15, &data, &val))
        self->version = val;
    } else if (g_str_has_prefix (data, "#EXT-X-STREAM-INF:") ||
        g_str_has_prefix (data, "#EXT-X-I-FRAME-STREAM-INF:")) {
      gchar *v, *a;
      gboolean iframe = g_str_has_prefix (data, "#EXT-X-I-FRAME-STREAM-INF:");
      GstM3U8 *new_list;

      new_list = gst_m3u8_new ();
      new_list->iframe = iframe;
      data = data + (iframe ? 26 : 18);
      while (data && parse_attributes (&data, &a, &v)) {
        if (g_str_equal (a, "BANDWIDTH")) {
          if (!int_from_string (v, NULL, &new_list->bandwidth))
            GST_WARNING ("Error while reading BANDWIDTH");
        } else if (g_str_equal (a, "PROGRAM-ID")) {
          if (!int_from_string (v, NULL, &new_list->program_id))
            GST_WARNING ("Error while reading PROGRAM-ID");
        } else if (g_str_equal (a, "CODECS")) {
          g_free (new_list->codecs);
          new_list->codecs = g_strdup (v);
        } else if (g_str_equal (a, "RESOLUTION")) {
          if (!int_from_string (v, &v, &new_list->width))
            GST_WARNING ("Error while reading RESOLUTION width");
          if (!v || *v != 'x') {
            GST_WARNING ("Missing height");
          } else {
            v = g_utf8_next_char (v);
            if (!int_from_string (v, NULL, &new_list->height))
              GST_WARNING ("Error while reading RESOLUTION height");
          }
        } else if (iframe && g_str_equal (a, "URI")) {
          gchar *uri = g_strdup (v);
          gchar *urip = uri;
          int len = strlen (uri);

          /* handle the \"uri\" case */
          if (uri[len - 1] == '"')
            uri[len - 1] = '\0';
          if (uri[0] == '"')
            uri += 1;

          uri = uri_join (self->uri, uri);
          g_free (urip);

          if (uri == NULL)
            continue;
          gst_m3u8_set_uri (new_list, uri);
        }
      }

      if (iframe) {
        if (g_list_find_custom (self->iframe_lists, new_list->uri,
                (GCompareFunc) _m3u8_compare_uri)) {
          GST_DEBUG ("Already have a list with this URI");
          gst_m3u8_free (new_list);
        } else {
          self->iframe_lists = g_list_append (self->iframe_lists, new_list);
        }
      } else if (list != NULL) {
        GST_WARNING ("Found a list without a uri..., dropping");
        gst_m3u8_free (list);
      } else {
        list = new_list;
      }
    } else if (g_str_has_prefix (data, "#EXT-X-TARGETDURATION:")) {
      if (int_from_string (data + 22, &data, &val))
        self->targetduration = val * GST_SECOND;
    } else if (g_str_has_prefix (data, "#EXT-X-MEDIA-SEQUENCE:")) {
      if (int_from_string (data + 22, &data, &val))
        self->mediasequence = val;
    } else if (g_str_has_prefix (data, "#EXT-X-DISCONTINUITY")) {
      discontinuity = TRUE;
    } else if (g_str_has_prefix (data, "#EXT-X-PROGRAM-DATE-TIME:")) {
      /* <YYYY-MM-DDThh:mm:ssZ> */
      GST_DEBUG ("FIXME parse date");
    } else if (g_str_has_prefix (data, "#EXT-X-ALLOW-CACHE:")) {
      self->allowcache = g_ascii_strcasecmp (data + 19, "YES") == 0;
    } else if (g_str_has_prefix (data, "#EXT-X-KEY:")) {
      gchar *v, *a;

      data = data + 11;

      /* IV and KEY are only valid until the next #EXT-X-KEY */
      have_iv = FALSE;
      g_free (self->key);
      self->key = NULL;
      while (data && parse_attributes (&data, &a, &v)) {
        if (g_str_equal (a, "URI")) {
          gchar *key = g_strdup (v);
          gchar *keyp = key;
          int len = strlen (key);

          /* handle the \"key\" case */
          if (key[len - 1] == '"')
            key[len - 1] = '\0';
          if (key[0] == '"')
            key += 1;

          self->key = uri_join (self->uri, key);
          g_free (keyp);
        } else if (g_str_equal (a, "IV")) {
          gchar *ivp = v;
          gint i;

          if (strlen (ivp) < 32 + 2 || (!g_str_has_prefix (ivp, "0x")
                  && !g_str_has_prefix (ivp, "0X"))) {
            GST_WARNING ("Can't read IV");
            continue;
          }

          ivp += 2;
          for (i = 0; i < 16; i++) {
            gint h, l;

            h = g_ascii_xdigit_value (*ivp);
            ivp++;
            l = g_ascii_xdigit_value (*ivp);
            ivp++;
            if (h == -1 || l == -1) {
              i = -1;
              break;
            }
            iv[i] = (h << 4) | l;
          }

          if (i == -1) {
            GST_WARNING ("Can't read IV");
            continue;
          }
          have_iv = TRUE;
        } else if (g_str_equal (a, "METHOD")) {
          if (!g_str_equal (v, "AES-128")) {
            GST_WARNING ("Encryption method %s not supported", v);
            continue;
          }
        }
      }
    } else if (g_str_has_prefix (data, "#EXTINF:")) {
      gdouble fval;
      if (!double_from_string (data + 8, &data, &fval)) {
        GST_WARNING ("Can't read EXTINF duration");
        goto next_line;
      }
      duration = fval * (gdouble) GST_SECOND;
      if (duration > self->targetduration)
        GST_WARNING ("EXTINF duration > TARGETDURATION");
      if (!data || *data != ',')
        goto next_line;
      data = g_utf8_next_char (data);
      if (data != end) {
        g_free (title);
        title = g_strdup (data);
      }
    } else if (g_str_has_prefix (data, "#EXT-X-BYTERANGE:")) {
      gchar *v = data + 17;

      if (int64_from_string (v, &v, &size)) {
        if (*v == '@' && !int64_from_string (v + 1, &v, &offset))
          goto next_line;
      } else {
        goto next_line;
      }
    } else {
      GST_LOG ("Ignored line: %s", data);
    }

  next_line:
    if (!end)
      break;
    data = g_utf8_next_char (end);      /* skip \n */
  }

  /* reorder playlists by bitrate */
  if (self->lists) {
    gchar *top_variant_uri = NULL;
    gboolean iframe = FALSE;

    if (!self->current_variant) {
      top_variant_uri = GST_M3U8 (self->lists->data)->uri;
    } else {
      top_variant_uri = GST_M3U8 (self->current_variant->data)->uri;
      iframe = GST_M3U8 (self->current_variant->data)->iframe;
    }

    self->lists =
        g_list_sort (self->lists,
        (GCompareFunc) gst_m3u8_compare_playlist_by_bitrate);

    self->iframe_lists =
        g_list_sort (self->iframe_lists,
        (GCompareFunc) gst_m3u8_compare_playlist_by_bitrate);

    if (iframe)
      self->current_variant =
          g_list_find_custom (self->iframe_lists, top_variant_uri,
          (GCompareFunc) _m3u8_compare_uri);
    else
      self->current_variant = g_list_find_custom (self->lists, top_variant_uri,
          (GCompareFunc) _m3u8_compare_uri);
  }

  return TRUE;
}

GstM3U8Client *
gst_m3u8_client_new (const gchar * uri)
{
  GstM3U8Client *client;

  g_return_val_if_fail (uri != NULL, NULL);

  client = g_new0 (GstM3U8Client, 1);
  client->main = gst_m3u8_new ();
  client->current = NULL;
  client->sequence = -1;
  client->sequence_position = 0;
  client->update_failed_count = 0;
  g_mutex_init (&client->lock);
  gst_m3u8_set_uri (client->main, g_strdup (uri));

  return client;
}

void
gst_m3u8_client_free (GstM3U8Client * self)
{
  g_return_if_fail (self != NULL);

  gst_m3u8_free (self->main);
  g_mutex_clear (&self->lock);
  g_free (self);
}

void
gst_m3u8_client_set_current (GstM3U8Client * self, GstM3U8 * m3u8)
{
  g_return_if_fail (self != NULL);

  GST_M3U8_CLIENT_LOCK (self);
  if (m3u8 != self->current) {
    self->current = m3u8;
    self->update_failed_count = 0;
  }
  GST_M3U8_CLIENT_UNLOCK (self);
}

gboolean
gst_m3u8_client_update (GstM3U8Client * self, gchar * data)
{
  GstM3U8 *m3u8;
  gboolean updated = FALSE;
  gboolean ret = FALSE;

  g_return_val_if_fail (self != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (self);
  m3u8 = self->current ? self->current : self->main;

  if (!gst_m3u8_update (m3u8, data, &updated))
    goto out;

  if (!updated) {
    self->update_failed_count++;
    goto out;
  }

  if (self->current && !self->current->files) {
    GST_ERROR ("Invalid media playlist, it does not contain any media files");
    goto out;
  }

  /* select the first playlist, for now */
  if (!self->current) {
    if (self->main->lists) {
      self->current = self->main->current_variant->data;
    } else {
      self->current = self->main;
    }
  }

  if (m3u8->files && self->sequence == -1) {
    self->sequence =
        GST_M3U8_MEDIA_FILE (g_list_first (m3u8->files)->data)->sequence;
    self->sequence_position = 0;
    GST_DEBUG ("Setting first sequence at %u", (guint) self->sequence);
  }

  ret = TRUE;
out:
  GST_M3U8_CLIENT_UNLOCK (self);
  return ret;
}

static gboolean
_find_current (GstM3U8MediaFile * file, GstM3U8Client * client)
{
  return file->sequence != client->sequence;
}

static GList *
find_next_fragment (GstM3U8Client * client, GList * l, gboolean forward)
{
  GstM3U8MediaFile *file;

  if (!forward)
    l = g_list_last (l);

  while (l) {
    file = l->data;

    if (forward && file->sequence >= client->sequence)
      break;
    else if (!forward && file->sequence <= client->sequence)
      break;

    l = (forward ? l->next : l->prev);
  }

  return l;
}

gboolean
gst_m3u8_client_get_next_fragment (GstM3U8Client * client,
    gboolean * discontinuity, const gchar ** uri, GstClockTime * duration,
    GstClockTime * timestamp, gint64 * range_start, gint64 * range_end,
    const gchar ** key, const guint8 ** iv, gboolean forward)
{
  GList *l;
  GstM3U8MediaFile *file;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->current != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  GST_DEBUG ("Looking for fragment %" G_GINT64_FORMAT, client->sequence);
  if (client->sequence < 0) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }
  l = find_next_fragment (client, client->current->files, forward);
  if (!l) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return FALSE;
  }

  file = GST_M3U8_MEDIA_FILE (l->data);
  GST_DEBUG ("Got fragment with sequence %u (client sequence %u)",
      (guint) file->sequence, (guint) client->sequence);

  if (timestamp)
    *timestamp = client->sequence_position;

  if (discontinuity)
    *discontinuity = client->sequence != file->sequence || file->discont;
  if (uri)
    *uri = file->uri;
  if (duration)
    *duration = file->duration;
  if (range_start)
    *range_start = file->offset;
  if (range_end)
    *range_end = file->size != -1 ? file->offset + file->size - 1 : -1;
  if (key)
    *key = file->key;
  if (iv)
    *iv = file->iv;

  client->sequence = file->sequence;

  GST_M3U8_CLIENT_UNLOCK (client);
  return TRUE;
}

void
gst_m3u8_client_advance_fragment (GstM3U8Client * client, gboolean forward)
{
  GList *l;
  GstM3U8MediaFile *file;

  g_return_if_fail (client != NULL);
  g_return_if_fail (client->current != NULL);

  GST_M3U8_CLIENT_LOCK (client);
  GST_DEBUG ("Looking for fragment %" G_GINT64_FORMAT, client->sequence);
  l = g_list_find_custom (client->current->files, client,
      (GCompareFunc) _find_current);
  if (l == NULL) {
    GST_ERROR ("Could not find current fragment");
    GST_M3U8_CLIENT_UNLOCK (client);
    return;
  }

  file = GST_M3U8_MEDIA_FILE (l->data);
  GST_DEBUG ("Advancing from sequence %u", (guint) file->sequence);
  if (forward) {
    client->sequence = file->sequence + 1;
    client->sequence_position += file->duration;
  } else {
    client->sequence = file->sequence - 1;
    if (client->sequence_position > file->duration)
      client->sequence_position -= file->duration;
    else
      client->sequence_position = 0;
  }
  GST_M3U8_CLIENT_UNLOCK (client);
}

static void
_sum_duration (GstM3U8MediaFile * self, GstClockTime * duration)
{
  *duration += self->duration;
}

GstClockTime
gst_m3u8_client_get_duration (GstM3U8Client * client)
{
  GstClockTime duration = 0;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  GST_M3U8_CLIENT_LOCK (client);
  /* We can only get the duration for on-demand streams */
  if (!client->current || !client->current->endlist) {
    GST_M3U8_CLIENT_UNLOCK (client);
    return GST_CLOCK_TIME_NONE;
  }
  if (client->current->files)
    g_list_foreach (client->current->files, (GFunc) _sum_duration, &duration);
  GST_M3U8_CLIENT_UNLOCK (client);
  return duration;
}

GstClockTime
gst_m3u8_client_get_target_duration (GstM3U8Client * client)
{
  GstClockTime duration = 0;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  GST_M3U8_CLIENT_LOCK (client);
  duration = client->current->targetduration;
  GST_M3U8_CLIENT_UNLOCK (client);
  return duration;
}

const gchar *
gst_m3u8_client_get_uri (GstM3U8Client * client)
{
  const gchar *uri;

  g_return_val_if_fail (client != NULL, NULL);

  GST_M3U8_CLIENT_LOCK (client);
  uri = client->main->uri;
  GST_M3U8_CLIENT_UNLOCK (client);
  return uri;
}

const gchar *
gst_m3u8_client_get_current_uri (GstM3U8Client * client)
{
  const gchar *uri;

  g_return_val_if_fail (client != NULL, NULL);

  GST_M3U8_CLIENT_LOCK (client);
  uri = client->current->uri;
  GST_M3U8_CLIENT_UNLOCK (client);
  return uri;
}

gboolean
gst_m3u8_client_has_variant_playlist (GstM3U8Client * client)
{
  gboolean ret;

  g_return_val_if_fail (client != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  ret = (client->main->lists != NULL);
  GST_M3U8_CLIENT_UNLOCK (client);
  return ret;
}

gboolean
gst_m3u8_client_is_live (GstM3U8Client * client)
{
  gboolean ret;

  g_return_val_if_fail (client != NULL, FALSE);

  GST_M3U8_CLIENT_LOCK (client);
  if (!client->current || client->current->endlist)
    ret = FALSE;
  else
    ret = TRUE;
  GST_M3U8_CLIENT_UNLOCK (client);
  return ret;
}

GList *
gst_m3u8_client_get_playlist_for_bitrate (GstM3U8Client * client, guint bitrate)
{
  GList *list, *current_variant;

  GST_M3U8_CLIENT_LOCK (client);
  current_variant = client->main->current_variant;

  /*  Go to the highest possible bandwidth allowed */
  while (GST_M3U8 (current_variant->data)->bandwidth <= bitrate) {
    list = g_list_next (current_variant);
    if (!list)
      break;
    current_variant = list;
  }

  while (GST_M3U8 (current_variant->data)->bandwidth > bitrate) {
    list = g_list_previous (current_variant);
    if (!list)
      break;
    current_variant = list;
  }
  GST_M3U8_CLIENT_UNLOCK (client);

  return current_variant;
}

gchar *
uri_join (const gchar * uri1, const gchar * uri2)
{
  gchar *uri_copy, *tmp, *ret = NULL;

  if (gst_uri_is_valid (uri2))
    return g_strdup (uri2);

  uri_copy = g_strdup (uri1);
  if (uri2[0] != '/') {
    /* uri2 is a relative uri2 */
    /* look for query params */
    tmp = g_utf8_strchr (uri_copy, -1, '?');
    if (tmp) {
      /* find last / char, ignoring query params */
      tmp = g_utf8_strrchr (uri_copy, tmp - uri_copy, '/');
    } else {
      /* find last / char in URL */
      tmp = g_utf8_strrchr (uri_copy, -1, '/');
    }
    if (!tmp) {
      GST_WARNING ("Can't build a valid uri_copy");
      goto out;
    }

    *tmp = '\0';
    ret = g_strdup_printf ("%s/%s", uri_copy, uri2);
  } else {
    /* uri2 is an absolute uri2 */
    char *scheme, *hostname;

    scheme = uri_copy;
    /* find the : in <scheme>:// */
    tmp = g_utf8_strchr (uri_copy, -1, ':');
    if (!tmp) {
      GST_WARNING ("Can't build a valid uri_copy");
      goto out;
    }

    *tmp = '\0';

    /* skip :// */
    hostname = tmp + 3;

    tmp = g_utf8_strchr (hostname, -1, '/');
    if (tmp)
      *tmp = '\0';

    ret = g_strdup_printf ("%s://%s%s", scheme, hostname, uri2);
  }

out:
  g_free (uri_copy);
  return ret;
}

guint64
gst_m3u8_client_get_current_fragment_duration (GstM3U8Client * client)
{
  guint64 dur;
  GList *list;

  g_return_val_if_fail (client != NULL, 0);

  GST_M3U8_CLIENT_LOCK (client);

  list = g_list_find_custom (client->current->files, client,
      (GCompareFunc) _find_current);
  if (list == NULL) {
    dur = -1;
  } else {
    dur = GST_M3U8_MEDIA_FILE (list->data)->duration;
  }

  GST_M3U8_CLIENT_UNLOCK (client);
  return dur;
}
