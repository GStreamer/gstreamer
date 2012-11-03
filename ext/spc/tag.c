/* Copyright (C) 2007 Brian Koropoff <bkoropoff at gmail com>
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

#include <tag.h>
#include <string.h>
#include <stdlib.h>

#define EXTENDED_OFFSET 0x10200
#define EXTENDED_MAGIC ((guint32) ('x' << 0 | 'i' << 8 | 'd' << 16 | '6' << 24))

#define TYPE_LENGTH 0x0
#define TYPE_STRING 0x1
#define TYPE_INTEGER 0x4

#define TAG_TITLE 0x01
#define TAG_GAME 0x02
#define TAG_ARTIST 0x03
#define TAG_DUMPER 0x04
#define TAG_DUMP_DATE 0x05
#define TAG_EMULATOR 0x06
#define TAG_COMMENT 0x07
#define TAG_ALBUM 0x10
#define TAG_DISC 0x11
#define TAG_TRACK 0x12
#define TAG_PUBLISHER 0x13
#define TAG_YEAR 0x14
#define TAG_INTRO 0x30
#define TAG_LOOP 0x31
#define TAG_END 0x32
#define TAG_FADE 0x33
#define TAG_MUTED 0x34
#define TAG_COUNT 0x35
#define TAG_AMP 0x36

#define READ_INT8(data, offset) (data[offset])
#define READ_INT16(data, offset) ((data[offset] << 0) + (data[offset+1] << 8))
#define READ_INT24(data, offset) ((data[offset] << 0) + (data[offset+1] << 8) + (data[offset+2] << 16))
#define READ_INT32(data, offset) ((data[offset] << 0) + (data[offset+1] << 8) + (data[offset+2] << 16) + (data[offset+3] << 24))

static inline gboolean
spc_tag_is_extended (guchar * data, guint length)
{
  // Extended tags come at the end of the file (at a known offset)
  // and start with "xid6"
  return (length > EXTENDED_OFFSET + 4
      && READ_INT32 (data, EXTENDED_OFFSET) == EXTENDED_MAGIC);
}

static inline gboolean
spc_tag_is_text_format (guchar * data, guint length)
{
  // Because the id666 format is brain dead, there's
  // no definite way to decide if it is in text
  // format.  This function implements a set of
  // heuristics to make a best-effort guess.

  // If the date field contains separators, it is probably text
  if (data[0xA0] == '/' || data[0xA0] == '.')
    return TRUE;
  // If the first byte of the date field is small (but not 0,
  // which could indicate an empty string), it's probably binary
  if (data[0x9E] >= 1 && data[0x9E] <= 31)
    return FALSE;

  // If all previous tests turned up nothing, assume it's text
  return TRUE;
}

static inline gboolean
spc_tag_is_present (guchar * data, guint length)
{
  return data[0x23] == 26;
}

static inline GDate *
spc_tag_unpack_date (guint32 packed)
{
  guint dump_year = packed / 10000;
  guint dump_month = (packed % 10000) / 100;
  guint dump_day = packed % 100;

  if (dump_month == 0)
    dump_month = 1;
  if (dump_day == 0)
    dump_day = 1;

  if (dump_year != 0)
    return g_date_new_dmy (dump_day, dump_month, dump_year);
  else
    return NULL;
}

void
spc_tag_clear (spc_tag_info * info)
{
  info->title = info->game = info->publisher = info->artist = info->album =
      info->comment = info->dumper = NULL;
  info->dump_date = NULL;
  info->time_seconds = 0;
  info->time_fade_milliseconds = 0;
  info->time_intro = 0;
  info->time_end = 0;
  info->time_loop = 0;
  info->time_fade = 0;
  info->loop_count = 0;
  info->muted = 0;
  info->disc = 0;
  info->amplification = 0;
}

void
spc_tag_get_info (guchar * data, guint length, spc_tag_info * info)
{
  spc_tag_clear (info);

  if (spc_tag_is_present (data, length)) {
    gboolean text_format = spc_tag_is_text_format (data, length);

    info->title = g_new0 (gchar, 0x21);
    info->game = g_new0 (gchar, 0x21);
    info->artist = g_new0 (gchar, 0x21);
    info->dumper = g_new0 (gchar, 0x10);
    info->comment = g_new0 (gchar, 0x32);

    strncpy (info->title, (gchar *) & data[0x2E], 32);
    strncpy (info->artist, (gchar *) & data[(text_format ? 0xB1 : 0xB0)], 32);
    strncpy (info->game, (gchar *) & data[0x4E], 32);
    strncpy (info->dumper, (gchar *) & data[0x6E], 16);
    strncpy (info->comment, (gchar *) & data[0x7E], 32);

    if (text_format) {
      gchar time[4];
      gchar fade[6];
      guint dump_year, dump_month, dump_day;

      strncpy (time, (gchar *) data + 0xA9, 3);
      strncpy (fade, (gchar *) data + 0xAC, 5);

      time[3] = fade[5] = 0;

      info->time_seconds = atoi (time);
      info->time_fade_milliseconds = atoi (fade);

      dump_year = (guint) atoi ((gchar *) data + 0x9E);
      dump_month = (guint) atoi ((gchar *) data + 0x9E + 3);
      dump_day = (guint) atoi ((gchar *) data + 0x9E + 3 + 3);

      if (dump_month == 0)
        dump_month = 1;
      if (dump_day == 0)
        dump_day = 1;
      if (dump_year != 0)
        info->dump_date = g_date_new_dmy (dump_day, dump_month, dump_year);

      info->muted = READ_INT8 (data, 0xD1);
      info->emulator = READ_INT8 (data, 0xD2);
    } else {
      info->time_seconds = READ_INT24 (data, 0xA9);
      info->time_fade_milliseconds = READ_INT32 (data, 0xAC);
      info->dump_date = spc_tag_unpack_date (READ_INT32 (data, 0x9E));
      info->muted = READ_INT8 (data, 0xD0);
      info->emulator = READ_INT8 (data, 0xD1);
    }
  }

  if (spc_tag_is_extended (data, length)) {
    guchar *chunk = data + EXTENDED_OFFSET + 8;
    guint32 chunk_size = *((guint32 *) (data + EXTENDED_OFFSET + 4));

    guchar *subchunk, *subchunk_next;

    for (subchunk = chunk; subchunk < chunk + chunk_size;
        subchunk = subchunk_next) {
      guint8 tag = READ_INT8 (subchunk, 0);
      guint8 type = READ_INT8 (subchunk, 1);
      guint16 length = READ_INT16 (subchunk, 2);
      guchar *value = subchunk + 4;

      switch (type) {
        case TYPE_LENGTH:
        {
          switch (tag) {
            case TAG_TRACK:
              info->track = READ_INT8 (subchunk, 2 + 1);
              break;
            case TAG_YEAR:
              info->year = READ_INT16 (subchunk, 2);
              break;
            case TAG_COUNT:
              info->loop_count = READ_INT8 (subchunk, 2);
              break;
            case TAG_EMULATOR:
              info->emulator = READ_INT8 (subchunk, 2);
              break;
            case TAG_DISC:
              info->disc = READ_INT8 (subchunk, 2);
              break;
            case TAG_MUTED:
              info->muted = READ_INT8 (subchunk, 2);
              break;
            default:
              break;
          }

          subchunk_next = subchunk + 4;
          break;
        }
        case TYPE_STRING:
        {
          gchar *dest;

          if (length <= 1)
            dest = NULL;
          else
            switch (tag) {
              case TAG_TITLE:
                dest = info->title = g_renew (gchar, info->title, length);
                break;
              case TAG_GAME:
                dest = info->game = g_renew (gchar, info->game, length);
                break;
              case TAG_ARTIST:
                dest = info->artist = g_renew (gchar, info->artist, length);
                break;
              case TAG_ALBUM:
                dest = info->album = g_renew (gchar, info->album, length);
                break;
              case TAG_DUMPER:
                dest = info->dumper = g_renew (gchar, info->dumper, length);
                break;
              case TAG_COMMENT:
                dest = info->comment = g_renew (gchar, info->comment, length);
                break;
              case TAG_PUBLISHER:
                dest = info->publisher =
                    g_renew (gchar, info->publisher, length);
                break;
              default:
                dest = NULL;
                break;
            }

          if (dest)
            strncpy (dest, (gchar *) value, length);

          subchunk_next = value + length;
          break;
        }
        case TYPE_INTEGER:
        {
          switch (tag) {
            case TAG_INTRO:
              info->time_intro = READ_INT32 (value, 0);
              break;
            case TAG_END:
              info->time_end = READ_INT32 (value, 0);
              break;
            case TAG_FADE:
              info->time_fade = READ_INT32 (value, 0);
              break;
            case TAG_LOOP:
              info->time_loop = READ_INT32 (value, 0);
              break;
            case TAG_DUMP_DATE:
              info->dump_date = spc_tag_unpack_date (READ_INT32 (value, 0));
              break;
            case TAG_AMP:
              info->amplification = READ_INT32 (value, 0);
              break;
            default:
              break;
          }
          subchunk_next = value + length;
          break;
        }
        default:
          subchunk_next = value + length;
          break;
      }
    }
  }

  if (info->title && !*info->title) {
    g_free (info->title);
    info->title = NULL;
  }
  if (info->game && !*info->game) {
    g_free (info->game);
    info->game = NULL;
  }
  if (info->artist && !*info->artist) {
    g_free (info->artist);
    info->artist = NULL;
  }
  if (info->album && !*info->album) {
    g_free (info->album);
    info->album = NULL;
  }
  if (info->publisher && !*info->publisher) {
    g_free (info->publisher);
    info->publisher = NULL;
  }
  if (info->comment && !*info->comment) {
    g_free (info->comment);
    info->comment = NULL;
  }
  if (info->dumper && !*info->dumper) {
    g_free (info->dumper);
    info->dumper = NULL;
  }
}

void
spc_tag_free (spc_tag_info * info)
{
  if (info->title)
    g_free (info->title);
  if (info->game)
    g_free (info->game);
  if (info->artist)
    g_free (info->artist);
  if (info->album)
    g_free (info->album);
  if (info->publisher)
    g_free (info->publisher);
  if (info->comment)
    g_free (info->comment);
  if (info->dumper)
    g_free (info->dumper);
  if (info->dump_date)
    g_date_free (info->dump_date);
}
