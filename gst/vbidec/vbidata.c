/**
 * Copyright (c) 2002 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (c) 2002 Doug Bell <drbell@users.sourceforge.net>
 *
 * CC code from Nathan Laredo's ccdecode, used under the GPL.
 * Lots of 'hey what does this mean?' code from
 * Billy Biggs and Doug Bell, like all the crap with
 * XDS and stuff.  Some help from Zapping's vbi library by
 * Michael H. Schimek and others, released under the GPL.
 *
 * Modified and adapted to GStreamer by
 * David I. Lehn <dlehn@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include "vbidata.h"
#include "vbiscreen.h"
/*#include "tvtimeosd.h"*/

#define DO_LINE 11
static int pll = 0;

struct vbidata_s
{
  int fd;
  vbiscreen_t *vs;
  /*tvtime_osd_t *osd; */
  char buf[65536];
  int wanttop;
  int wanttext;

  unsigned int colour;
  int row, ital;
  int indent, ul;
  int chan;

  unsigned int current_colour;
  int current_row, current_ital;
  int current_indent, current_ul;
  int current_chan;
  int current_istext;

  int initialised;
  int enabled;
  int lastcode;
  int lastcount;
  int verbose;

  /* XDS data */
  char xds_packet[2048];
  int xds_cursor;

  char *program_name;
  char *network_name;
  char *call_letters;
  const char *rating;
  const char *program_type;
  int start_day;
  int start_month;
  int start_min;
  int start_hour;
  int length_hour;
  int length_min;
  int length_elapsed_hour;
  int length_elapsed_min;
  int length_elapsed_sec;
  char *program_desc[8];
};


/* this is NOT exactly right */
//static char *ccode = " !\"#$%&'()\0341+,-./0123456789:;<=>?@"
static char *ccode = " !\"#$%&'()a+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//                     "abcdefghijklmnopqrstuvwxyz"
//                     "[\0351]\0355\0363\0372abcdefghijklmnopqr"
    "[e]iouabcdefghijklmnopqr"
//                     "stuvwxyz\0347\0367\0245\0244\0240";
    "stuvwxyzcoNn ";
static char *wccode = "\0256\0260\0275\0277T\0242\0243#\0340 "
    "\0350\0354\0362\0371";

static char *extcode1 = "\0301\0311\0323\0332\0334\0374"
    "`\0241*'-\0251S*\"\"\0300\0302"
    "\0307\0310\0312\0313\0353\0316\0317\0357" "\0324\0331\0371\0333\0253\0273";

static char *extcode2 = "\0303\0343\0315\0314\0354\0322\0362\0325"
    "{}\\^_|~\0304\0344\0326\0366\0337\0245\0244|" "\0305\0345\0330\0370++++";

int
parityok (int n)
{                               /* check parity for 2 bytes packed in n */
  int j, k;

  for (k = 0, j = 0; j < 7; j++)
    if (n & (1 << j))
      k++;
  if ((k & 1) && (n & 0x80))
    return 0;
  for (k = 0, j = 8; j < 15; j++)
    if (n & (1 << j))
      k++;
  if ((k & 1) && (n & 0x8000))
    return 0;
  return 1;
}

int
decodebit (unsigned char *data, int threshold)
{
  return ((data[0] + data[1] + data[2] + data[3] + data[4] + data[5] +
          data[6] + data[7] + data[8] + data[9] + data[10] + data[11] +
          data[12] + data[13] + data[14] + data[15] + data[16] + data[17] +
          data[18] + data[19] + data[20] + data[21] + data[22] + data[23] +
          data[24] + data[25] + data[26] + data[27] + data[28] + data[29] +
          data[30] + data[31]) >> 5 > threshold);
}


int
ccdecode (unsigned char *vbiline)
{
  int max = 0, maxval = 0, minval = 255, i = 0, clk = 0, tmp = 0;
  int sample, packedbits = 0;

  for (i = 0; i < 250; i++) {
    sample = vbiline[i];
    if (sample - maxval > 10)
      (maxval = sample, max = i);
    if (sample < minval)
      minval = sample;
    if (maxval - sample > 40)
      break;
  }
  sample = ((maxval + minval) >> 1);
  pll = max;

  /* found clock lead-in, double-check start */
#ifndef PAL_DECODE
  i = max + 478;
#else
  i = max + 538;
#endif
  if (!decodebit (&vbiline[i], sample))
    return 0;
#ifndef PAL_DECODE
  tmp = i + 57;                 /* tmp = data bit zero */
#else
  tmp = i + 71;
#endif
  for (i = 0; i < 16; i++) {
#ifndef PAL_DECODE
    clk = tmp + i * 57;
#else
    clk = tmp + i * 71;
#endif
    if (decodebit (&vbiline[clk], sample)) {
      packedbits |= 1 << i;
    }
  }
  if (parityok (packedbits))
    return packedbits;
  return 0;
}                               /* ccdecode */

const char *movies[] = { "N/A", "G", "PG", "PG-13", "R",
  "NC-17", "X", "Not Rated"
};
const char *usa_tv[] = { "Not Rated", "TV-Y", "TV-Y7", "TV-G",
  "TV-PG", "TV-14", "TV-MA", "Not Rated"
};
const char *cane_tv[] = { "Exempt", "C", "C8+", "G", "PG",
  "14+", "18+", "Reserved"
};
const char *canf_tv[] = { "Exempt", "G", "8 ans +", "13 ans +",
  "16 ans +", "18 ans +", "Reserved",
  "Reserved"
};

const char *months[] = { 0, "Jan", "Feb", "Mar", "Apr", "May",
  "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *eia608_program_type[96] = {
  "education", "entertainment", "movie", "news", "religious", "sports",
  "other", "action", "advertisement", "animated", "anthology",
  "automobile", "awards", "baseball", "basketball", "bulletin", "business",
  "classical", "college", "combat", "comedy", "commentary", "concert",
  "consumer", "contemporary", "crime", "dance", "documentary", "drama",
  "elementary", "erotica", "exercise", "fantasy", "farm", "fashion",
  "fiction", "food", "football", "foreign", "fund raiser", "game/quiz",
  "garden", "golf", "government", "health", "high school", "history",
  "hobby", "hockey", "home", "horror", "information", "instruction",
  "international", "interview", "language", "legal", "live", "local",
  "math", "medical", "meeting", "military", "miniseries", "music", "mystery",
  "national", "nature", "police", "politics", "premiere", "prerecorded",
  "product", "professional", "public", "racing", "reading", "repair", "repeat",
  "review", "romance", "science", "series", "service", "shopping",
  "soap opera", "special", "suspense", "talk", "technical", "tennis",
  "travel", "variety", "video", "weather", "western"
};


static void
parse_xds_packet (vbidata_t * vbi, char *packet, int length)
{
  int sum = 0;
  int i;

  if (!vbi)
    return;

  /* Check the checksum for validity of the packet. */
  for (i = 0; i < length - 1; i++) {
    sum += packet[i];
  }
  if ((((~sum) & 0x7f) + 1) != packet[length - 1]) {
    return;
  }

  /* Stick a null at the end, and cut off the last two characters. */
  packet[length - 2] = '\0';
  length -= 2;

  if (packet[0] == 0x01 && packet[1] == 0x03) {
    if (vbi->program_name && !strcmp (vbi->program_name, packet + 2)) {
      return;
    }
    if (vbi->verbose)
      fprintf (stderr, "Current program name: '%s'\n", packet + 2);
    if (vbi->program_name)
      free (vbi->program_name);
    vbi->program_name = strdup (packet + 2);
    /*tvtime_osd_set_show_name( vbi->osd, vbi->program_name ); */
  } else if (packet[0] == 0x03 && packet[1] == 0x03) {
    if (vbi->verbose)
      fprintf (stderr, "Future program name: '%s'\n", packet + 2);
  } else if (packet[0] == 0x05 && packet[1] == 0x01) {
    if (vbi->network_name && !strcmp (vbi->network_name, packet + 2)) {
      return;
    }

    if (vbi->verbose)
      fprintf (stderr, "Network name: '%s'\n", packet + 2);
    if (vbi->network_name)
      free (vbi->network_name);
    vbi->network_name = strdup (packet + 2);
    /*tvtime_osd_set_network_name( vbi->osd, vbi->network_name ); */
  } else if (packet[0] == 0x01 && packet[1] == 0x05) {
    int movie_rating = packet[2] & 7;
    int scheme = (packet[2] & 56) >> 3;
    int tv_rating = packet[3] & 7;
    int VSL = packet[3] & 56;
    const char *str;

    switch (VSL | scheme) {
      case 3:                  /* Canadian English TV */
        str = cane_tv[tv_rating];
        break;
      case 7:                  /* Canadian French TV */
        str = canf_tv[tv_rating];
        break;
      case 19:                 /* Reserved */
      case 31:
        str = "";
        break;
      default:
        if (((VSL | scheme) & 3) == 1) {
          /* USA TV */
          str = usa_tv[tv_rating];
        } else {
          /* MPAA Movie Rating */
          str = movies[movie_rating];
        }
        break;
    }

    if (vbi->rating && !strcmp (vbi->rating, str)) {
      return;
    }

    if (vbi->verbose)
      fprintf (stderr, "Show rating: %s", str);
    if (((VSL | scheme) & 3) == 1 || ((VSL | scheme) & 3) == 0) {
      /* show VSLD for the americans */
      if ((VSL | scheme) & 32) {
        if (vbi->verbose)
          fprintf (stderr, " V");
      }
      if ((VSL | scheme) & 16) {
        if (vbi->verbose)
          fprintf (stderr, " S");
      }
      if ((VSL | scheme) & 8) {
        if (vbi->verbose)
          fprintf (stderr, " L");
      }
      if ((VSL | scheme) & 4) {
        if (vbi->verbose)
          fprintf (stderr, " D");
      }
    }
    if (vbi->verbose)
      fprintf (stderr, "\n");
    vbi->rating = str;
    /*tvtime_osd_set_show_rating( vbi->osd, vbi->rating ); */
  } else if (packet[0] == 0x05 && packet[1] == 0x02) {
    if (vbi->call_letters && !strcmp (vbi->call_letters, packet + 2)) {
      return;
    }

    if (vbi->verbose)
      fprintf (stderr, "Network call letters: '%s'\n", packet + 2);
    if (vbi->call_letters)
      free (vbi->call_letters);
    vbi->call_letters = strdup (packet + 2);
    /*tvtime_osd_set_network_call( vbi->osd, vbi->call_letters ); */
  } else if (packet[0] == 0x01 && packet[1] == 0x01) {
    int month = packet[5];      // & 15;
    int day = packet[4];        // & 31;
    int hour = packet[3];       // & 31;
    int min = packet[2];        // & 63;
    char str[33];

    if (vbi->verbose)
      fprintf (stderr, "Program Start: %02d %s, %02d:%02d\n",
          day & 31, months[month & 15], hour & 31, min & 63);
    // packet[ 3 ], packet[ 4 ], packet[ 5 ], packet[ 6 ] );
    //packet[ 5 ] & 31, packet[ 6 ], packet[ 4 ] & 31, packet[ 3 ] & 63 );
    vbi->start_month = month & 15;
    vbi->start_day = day & 31;
    vbi->start_hour = hour & 31;
    vbi->start_min = hour & 63;
    snprintf (str, 32, "%02d %s, %02d:%02d",
        day & 31, months[month & 15], hour & 31, min & 63);
    /*tvtime_osd_set_show_start( vbi->osd, str ); */
  } else if (packet[0] == 0x01 && packet[1] == 0x04) {
    if (vbi->verbose)
      fprintf (stderr, "Program type: ");
    for (i = 0; i < length - 2; i++) {
      int cur = packet[2 + i] - 0x20;

      if (cur >= 0 && cur < 96) {
        if (vbi->verbose)
          fprintf (stderr, "%s%s", i ? ", " : "", eia608_program_type[cur]);
        /* this will cause us to keep only the last type we check */
        vbi->program_type = eia608_program_type[cur];
      }
    }
    if (vbi->verbose)
      fprintf (stderr, "\n");
  } else if (packet[0] < 0x03 && packet[1] >= 0x10 && packet[1] <= 0x17) {

    if (vbi->program_desc[packet[1] & 0xf] &&
        !strcmp (vbi->program_desc[packet[1] & 0xf], packet + 2)) {
      return;
    }

    if (vbi->verbose)
      fprintf (stderr, "Program Description: Line %d", packet[1] & 0xf);
    if (vbi->verbose)
      fprintf (stderr, "%s\n", packet + 2);
    if (vbi->program_desc[packet[1] & 0xf])
      free (vbi->program_desc[packet[1] & 0xf]);
    vbi->program_desc[packet[1] & 0xf] = strdup (packet + 2);
  } else if (packet[0] == 0x01 && packet[1] == 0x02) {
    char str[33];

    str[0] = 0;
    if (vbi->verbose)
      fprintf (stderr, "Program Length: %02d:%02d",
          packet[3] & 63, packet[2] & 63);
    vbi->length_hour = packet[3] & 63;
    vbi->length_min = packet[2] & 63;
    snprintf (str, 32, "%02d:%02d", packet[3] & 63, packet[2] & 63);
    if (length > 4) {
      if (vbi->verbose)
        fprintf (stderr, " Elapsed: %02d:%02d", packet[5] & 63, packet[4] & 63);
      vbi->length_elapsed_hour = packet[5] & 63;
      vbi->length_elapsed_min = packet[4] & 63;
      snprintf (str, 32, "%02d:%02d/%02d:%02d",
          packet[5] & 63, packet[4] & 63, packet[3] & 63, packet[2] & 63);
    } else {
      vbi->length_elapsed_hour = 0;
      vbi->length_elapsed_min = 0;
    }

    if (length > 6) {
      if (vbi->verbose)
        fprintf (stderr, ".%02d", packet[6] & 63);
      vbi->length_elapsed_hour = packet[6] & 63;
      snprintf (str, 32, "%02d:%02d.%02d/%02d:%02d",
          packet[5] & 63, packet[4] & 63, packet[6] & 63,
          packet[3] & 63, packet[2] & 63);
    } else {
      vbi->length_elapsed_hour = 0;
    }
    /*tvtime_osd_set_show_length( vbi->osd, str ); */
    if (vbi->verbose)
      fprintf (stderr, "\n");
  } else if (packet[0] == 0x05 && packet[1] == 0x04) {
    if (vbi->verbose)
      fprintf (stderr, "Transmission Signal Identifier (TSID): 0x%04x\n",
          packet[2] << 24 | packet[3] << 16 | packet[4] << 8 | packet[5]);
  } else {
    /* unknown */

    if (vbi->verbose)
      fprintf (stderr, "Unknown XDS packet, class ");
    switch (packet[0]) {
      case 0x1:
        if (vbi->verbose)
          fprintf (stderr, "CURRENT start\n");
        break;
      case 0x2:
        if (vbi->verbose)
          fprintf (stderr, "CURRENT continue\n");
        break;

      case 0x3:
        if (vbi->verbose)
          fprintf (stderr, "FUTURE start\n");
        break;
      case 0x4:
        if (vbi->verbose)
          fprintf (stderr, "FUTURE continue\n");
        break;

      case 0x5:
        if (vbi->verbose)
          fprintf (stderr, "CHANNEL start\n");
        break;
      case 0x6:
        if (vbi->verbose)
          fprintf (stderr, "CHANNEL continue\n");
        break;

      case 0x7:
        if (vbi->verbose)
          fprintf (stderr, "MISC start\n");
        break;
      case 0x8:
        if (vbi->verbose)
          fprintf (stderr, "MISC continue\n");
        break;

      case 0x9:
        if (vbi->verbose)
          fprintf (stderr, "PUB start\n");
        break;
      case 0xa:
        if (vbi->verbose)
          fprintf (stderr, "PUB continue\n");
        break;

      case 0xb:
        if (vbi->verbose)
          fprintf (stderr, "RES start\n");
        break;
      case 0xc:
        if (vbi->verbose)
          fprintf (stderr, "RES continue\n");
        break;

      case 0xd:
        if (vbi->verbose)
          fprintf (stderr, "UNDEF start\n");
        break;
      case 0xe:
        if (vbi->verbose)
          fprintf (stderr, "UNDEF continue\n");
        break;
    }
    for (i = 0; i < length; i++) {
      if (vbi->verbose)
        fprintf (stderr, "0x%02x ", packet[i]);
    }
    if (vbi->verbose)
      fprintf (stderr, "\n");
  }
}

static int
xds_decode (vbidata_t * vbi, int b1, int b2)
{
  if (!vbi)
    return 0;
  if (vbi->xds_cursor > 2046) {
    vbi->xds_cursor = 0;
  }

  if (!vbi->xds_cursor && b1 > 0xf) {
    return 0;
  }


  if (b1 < 0xf && (b1 & 0x2)) {
    /* ignore the continue and thus 'support' continuation of
       a single packet */
    return 1;
  } else if (b1 < 0xf) {
    /* kill old packet cause we got a new one */
    vbi->xds_cursor = 0;
  }

  vbi->xds_packet[vbi->xds_cursor] = b1;
  vbi->xds_packet[vbi->xds_cursor + 1] = b2;
  vbi->xds_cursor += 2;

  if (b1 == 0xf) {
    parse_xds_packet (vbi, vbi->xds_packet, vbi->xds_cursor);
    vbi->xds_cursor = 0;
  }

  return 1;
}

#define NOMODE  0

#define CC1     1
#define CC2     2
#define T1      3
#define T2      4

#define CC3     1
#define CC4     2
#define T3      3
#define T4      4

const unsigned int colours[] = {
  0xFFFFFFFFU,                  /* white */
  0xFF00FF00U,                  /* green */
  0xFF0000FFU,                  /* blue */
  0xFF00C7C7U,                  /* cyan */
  0xFFFF0000U,                  /* red */
  0xFFFFFF00U,                  /* yellow */
  0xFFC700C7U                   /* magenta */
};

const int rows[] = {
  11,
  0,                            /* unused */
  1,
  2,
  3,
  4,
  12,
  13,
  14,
  15,
  5,
  6,
  7,
  8,
  9,
  10
};

#define ROLL_2      6
#define ROLL_3      7
#define ROLL_4      8
#define POP_UP      9
#define PAINT_ON    10


static int
Process16b (vbidata_t * vbi, int bottom, int w1)
{
  int b1, b2;

  b1 = w1 & 0x7f;
  b2 = (w1 >> 8) & 0x7f;

  if (!b1 && !b2) {
    return 0;
  }

  if (vbi->enabled && b1 >= 0x10 && b1 <= 0x1F && b2 >= 0x20 && b2 <= 0x7F) {
    int code;

    if ((b2 & 64)) {
      /* Preamble Code */
      /* This sets up colors and indenting */

      if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
        vbi->lastcount = (vbi->lastcount + 1) % 2;
        return 1;
      }

      vbi->current_chan = (b1 & 8) >> 3;
      if (!bottom == vbi->wanttop) {
        if (vbi->chan != vbi->current_chan)
          return 0;
      } else
        return 0;

      vbi->current_ital = (b2 & 1);
      if (!(b2 & 16)) {
        vbi->current_colour = colours[(b2 & 30) >> 1];
        vbi->current_indent = 0;
      } else {
        vbi->current_colour = 0xFFFFFFFFU;      /* white */
        vbi->current_indent = 4 * ((b2 & 14) >> 1);
      }
      vbi->current_row = rows[((b1 & 7) << 1) | ((b2 & 32) >> 5)];
      vbi->current_ul = b2 & 1;

      if (vbi->verbose)
        fprintf (stderr, "field: %d chan %d, ital %d, ul %d, colour 0x%x, "
            "indent %d, row %d\n", bottom, vbi->current_chan,
            vbi->current_ital, vbi->current_ul, vbi->current_colour,
            vbi->current_indent, vbi->current_row);

      if (!bottom == vbi->wanttop &&
          vbi->current_chan == vbi->chan &&
          vbi->current_istext == vbi->wanttext) {

        vbi->indent = vbi->current_indent;
        vbi->ital = vbi->current_ital;
        vbi->colour = vbi->current_colour;
        vbi->row = vbi->current_row;
        vbi->current_istext = 0;

        vbiscreen_new_caption (vbi->vs, vbi->indent, vbi->ital,
            vbi->colour, vbi->row);

      }

      vbi->lastcode = (b1 << 8) | b2;
      vbi->lastcount = 0;
      return 1;
    }

    if ((b1 & 8) == 1) {
      /* Midrow code */
      if (!vbi->initialised)
        return 0;

      if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
        vbi->lastcount = (vbi->lastcount + 1) % 2;
        return 1;
      }

      if (vbi->verbose)
        fprintf (stderr, "Midrow TODO: Add me.\n");

      vbi->lastcode = (b1 << 8) | b2;
      return 1;
    }

    if ((b1 & 2) && !(b2 & 64)) {
      if (!vbi->initialised)
        return 0;

      if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
        vbi->lastcount = (vbi->lastcount + 1) % 2;
        return 1;
      }

      if (vbi->verbose)
        fprintf (stderr, "Tab Offset: %d columns\n", b2 & 3);
      if (vbi->wanttext && vbi->current_istext &&
          vbi->current_chan == vbi->chan && !bottom == vbi->wanttop) {
        vbiscreen_tab (vbi->vs, b2 & 3);
      }
      vbi->lastcode = (b1 << 8) | b2;
      return 1;
    }

    switch ((code = b2 & 15)) {
      case 0:                  /* POP-UP */
      case 5:                  /* ROLL UP 2 */
      case 6:                  /* ROLL UP 3 */
      case 7:                  /* ROLL UP 4 */
      case 9:                  /* PAINT-ON */
      case 10:                 /* TEXT */
      case 11:                 /* TEXT */
        vbi->initialised = 1;
        if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
          /* This is the repeated Control Code */
          vbi->lastcount = (vbi->lastcount + 1) % 2;
          return 1;
        }
        switch (code) {
          case 0:              /* POP-UP */
            if (!vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Pop-Up\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 0;
              vbiscreen_set_mode (vbi->vs, 1, POP_UP);
            }
            break;
          case 5:              /* ROLL UP 2 */
            if (!vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Roll-Up 2 (RU2)\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 0;
              vbiscreen_set_mode (vbi->vs, 1, ROLL_2);
            }
            break;
          case 6:              /* ROLL UP 3 */
            if (!vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Roll-Up 3 (RU3)\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 0;
              vbiscreen_set_mode (vbi->vs, 1, ROLL_3);
            }
            break;
          case 7:              /* ROLL UP 4 */
            if (!vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Roll-Up 4 (RU4)\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 0;
              vbiscreen_set_mode (vbi->vs, 1, ROLL_4);
            }
            break;
          case 9:              /* PAINT-ON */
            if (!vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Paint-On\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 0;
              vbiscreen_set_mode (vbi->vs, 1, PAINT_ON);
            }
            break;
          case 10:             /* TEXT */
            if (vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Text Restart\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 1;
              vbiscreen_set_mode (vbi->vs, 0, 0);
            }
            break;
          case 11:             /* TEXT */
            if (vbi->wanttext && vbi->current_chan == vbi->chan &&
                !bottom == vbi->wanttop) {
              if (vbi->verbose)
                fprintf (stderr, "Resume Text Display\n");
              vbi->indent = vbi->current_indent;
              vbi->ital = vbi->current_ital;
              vbi->colour = vbi->current_colour;
              vbi->row = vbi->current_row;
              vbi->current_istext = 1;
              vbiscreen_set_mode (vbi->vs, 0, 0);
            }
            break;
          default:             /* impossible */
            break;
        }
        break;
      case 1:
        if (!vbi->initialised)
          return 0;
        if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
          vbi->lastcount = (vbi->lastcount + 1) % 2;
        }
        if (!bottom == vbi->wanttop && vbi->current_chan == vbi->chan &&
            vbi->current_istext == vbi->wanttext) {
          if (vbi->verbose)
            fprintf (stderr, "Backspace\n");
          vbiscreen_backspace (vbi->vs);
        }
        break;
      case 2:
      case 3:
        if (!vbi->initialised)
          return 0;
        fprintf (stderr, "Reserved\n");
        break;
      case 4:
        if (!vbi->initialised)
          return 0;
        if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
          vbi->lastcount = (vbi->lastcount + 1) % 2;
        }
        if (!bottom == vbi->wanttop && vbi->current_chan == vbi->chan &&
            vbi->current_istext == vbi->wanttext) {
          if (vbi->verbose)
            fprintf (stderr, "Delete to End of Row\n");
          vbiscreen_delete_to_end (vbi->vs);
        }
        break;
      case 8:
        if (!vbi->initialised)
          return 0;
        if (vbi->verbose)
          fprintf (stderr, "Flash On\n");
        break;
      case 12:
      case 13:
      case 14:
      case 15:
        if (!vbi->initialised)
          return 0;
        if (!bottom && vbi->lastcode == ((b1 << 8) | b2)) {
          vbi->lastcount = (vbi->lastcount + 1) % 2;
          return 1;
        }

        switch (code) {
          case 12:
            /* Show buffer 1, Fill buffer 2 */
            if (!bottom == vbi->wanttop &&
                vbi->current_chan == vbi->chan &&
                vbi->current_istext == vbi->wanttext) {
              if (vbi->verbose)
                fprintf (stderr, "Erase Displayed Memory\n");
              vbiscreen_erase_displayed (vbi->vs);
            }
            break;
          case 13:
            if (!bottom == vbi->wanttop &&
                vbi->current_chan == vbi->chan &&
                vbi->current_istext == vbi->wanttext) {
              if (vbi->verbose)
                fprintf (stderr, "Carriage Return\n");
              vbiscreen_carriage_return (vbi->vs);
            }
            break;
          case 14:
            if (!bottom == vbi->wanttop &&
                vbi->current_chan == vbi->chan &&
                vbi->current_istext == vbi->wanttext) {
              if (vbi->verbose)
                fprintf (stderr, "Erase Non-Displayed\n");
              vbiscreen_erase_non_displayed (vbi->vs);
            }
            break;
          case 15:
            /* Show buffer 2, Fill Buffer 1 */
            if (!bottom == vbi->wanttop &&
                vbi->current_chan == vbi->chan &&
                vbi->current_istext == vbi->wanttext) {
              if (vbi->verbose)
                fprintf (stderr, "End Of Caption\n");
              vbiscreen_end_of_caption (vbi->vs);
            }
            break;
          default:             /* impossible */
            return 0;
            break;
        }
        break;
      default:                 /* Impossible */
        return 0;
        break;
    }

    if (vbi->lastcode != ((b1 << 8) | b2)) {
      vbi->lastcount = 0;
    }

    vbi->lastcode = (b1 << 8) | b2;
    return 1;
  }

  if (bottom && xds_decode (vbi, b1, b2)) {
    return 1;
  }

  if (!vbi->enabled)
    return 0;

  vbi->lastcode = 0;
  vbi->lastcount = 0;

  if (!vbi->initialised)
    return 0;

  if (!bottom != vbi->wanttop || vbi->current_chan != vbi->chan ||
      vbi->current_istext != vbi->wanttext) {
    return 0;
  }

  if (b1 == 0x11 || b1 == 0x19 ||
      b1 == 0x12 || b1 == 0x13 || b1 == 0x1A || b1 == 0x1B) {
    switch (b1) {
      case 0x1A:
      case 0x12:
        /* use extcode1 */
        if (b1 > 31 && b2 > 31 && b1 <= 0x3F && b2 <= 0x3F)
          if (vbi->verbose)
            fprintf (stderr, "char %d (%c),  char %d (%c)\n", b1,
                extcode1[b1 - 32], b2, extcode1[b2 - 32]);

        break;
      case 0x13:
      case 0x1B:
        /* use extcode2 */
        if (b1 > 31 && b2 > 31 && b1 <= 0x3F && b2 <= 0x3F)
          if (vbi->verbose)
            fprintf (stderr, "char %d (%c),  char %d (%c)\n", b1,
                extcode2[b1 - 32], b2, extcode2[b2 - 32]);

        break;
      case 0x11:
      case 0x19:
        /* use wcode */
        if (b1 > 31 && b2 > 31 && b1 <= 0x3F && b2 <= 0x3F)
          if (vbi->verbose)
            fprintf (stderr, "char %d (%c),  char %d (%c)\n", b1,
                wccode[b1 - 32], b2, wccode[b2 - 32]);

        break;
      default:
        break;
    }
  } else if (b1) {
    /* use ccode */
    if (b1 < 32)
      b1 = 32;
    if (b2 < 32)
      b2 = 32;
    if (vbi->verbose)
      fprintf (stderr, "vbidata: data: %c %c\n", ccode[b1 - 32],
          ccode[b2 - 32]);
    vbiscreen_print (vbi->vs, ccode[b1 - 32], ccode[b2 - 32]);
  }


  return 1;
}                               /* Process16b */

int
ProcessLine (vbidata_t * vbi, unsigned char *s, int bottom)
{
  int w1;

  /*char *outbuf = NULL; */

  if (!vbi)
    return 0;

  w1 = ccdecode (s);

  return Process16b (vbi, bottom, w1);
}                               /* ProcessLine */



vbidata_t *
vbidata_new_file (const char *filename, vbiscreen_t * vs,
    /*tvtime_osd_t* osd, */ int verbose)
{
  vbidata_t *vbi = (vbidata_t *) malloc (sizeof (vbidata_t));

  if (!vbi) {
    return 0;
  }

  vbi->fd = open (filename, O_RDONLY);
  if (vbi->fd < 0) {
    fprintf (stderr, "vbidata: Can't open %s: %s\n",
        filename, strerror (errno));
    free (vbi);
    return 0;
  }

  vbi->vs = vs;
  /*vbi->osd = osd; */
  vbi->verbose = verbose;

  vbidata_reset (vbi);

  return vbi;
}

vbidata_t *
vbidata_new_line (vbiscreen_t * vs, int verbose)
{
  vbidata_t *vbi = (vbidata_t *) malloc (sizeof (vbidata_t));

  if (!vbi) {
    return 0;
  }

  vbi->vs = vs;
  /*vbi->osd = osd; */
  vbi->verbose = verbose;

  vbidata_reset (vbi);

  return vbi;
}

void
vbidata_delete (vbidata_t * vbi)
{
  if (!vbi)
    return;
  if (vbi->fd != 0) {
    close (vbi->fd);
  }
  free (vbi);
}

void
vbidata_reset (vbidata_t * vbi)
{
  if (!vbi)
    return;

  vbi->wanttop = 0;
  vbi->wanttext = 0;
  vbi->colour = 0xFFFFFFFFU;
  vbi->row = 0;

  vbi->ital = 0;
  vbi->indent = 0;
  vbi->ul = 0;

  vbi->chan = 0;

  vbi->initialised = 0;
  vbi->enabled = 0;

  memset (vbi->program_desc, 0, 8 * sizeof (char *));
  vbi->program_name = NULL;
  vbi->network_name = NULL;
  vbi->call_letters = NULL;
  vbi->rating = NULL;
  vbi->program_type = NULL;

  vbi->start_day = 0;
  vbi->start_month = 0;
  vbi->start_min = 0;
  vbi->start_hour = 0;
  vbi->length_hour = 0;
  vbi->length_min = 0;
  vbi->length_elapsed_hour = 0;
  vbi->length_elapsed_min = 0;
  vbi->length_elapsed_sec = 0;

  /*
     tvtime_osd_set_network_call( vbi->osd, "" );
     tvtime_osd_set_network_name( vbi->osd, "" );
     tvtime_osd_set_show_name( vbi->osd, "" );
     tvtime_osd_set_show_rating( vbi->osd, "" );
     tvtime_osd_set_show_start( vbi->osd, "" );
     tvtime_osd_set_show_length( vbi->osd, "" );
   */



  vbi->lastcode = 0;
  vbi->lastcount = 0;
  vbi->xds_packet[0] = 0;
  vbi->xds_cursor = 0;
  vbiscreen_reset (vbi->vs);
}

void
vbidata_set_verbose (vbidata_t * vbi, int verbose)
{
  vbi->verbose = verbose;
}

void
vbidata_capture_mode (vbidata_t * vbi, int mode)
{
  if (!vbi)
    return;
  switch (mode) {
    case CAPTURE_OFF:
      vbi->enabled = 0;
      break;
    case CAPTURE_CC1:
      vbi->wanttop = 1;
      vbi->wanttext = 0;
      vbi->chan = 0;
      vbi->enabled = 1;
      break;
    case CAPTURE_CC2:
      vbi->wanttop = 1;
      vbi->wanttext = 0;
      vbi->chan = 1;
      vbi->enabled = 1;
      break;
    case CAPTURE_CC3:
      vbi->wanttop = 0;
      vbi->wanttext = 0;
      vbi->chan = 0;
      vbi->enabled = 1;
      break;
    case CAPTURE_CC4:
      vbi->wanttop = 0;
      vbi->wanttext = 0;
      vbi->chan = 1;
      vbi->enabled = 1;
      break;
    case CAPTURE_T1:
      vbi->wanttop = 1;
      vbi->wanttext = 1;
      vbi->chan = 0;
      vbi->enabled = 1;
      break;
    case CAPTURE_T2:
      vbi->wanttop = 1;
      vbi->wanttext = 1;
      vbi->chan = 1;
      vbi->enabled = 1;
      break;
    case CAPTURE_T3:
      vbi->wanttop = 0;
      vbi->wanttext = 1;
      vbi->chan = 0;
      vbi->enabled = 1;
      break;
    case CAPTURE_T4:
      vbi->wanttop = 0;
      vbi->wanttext = 1;
      vbi->chan = 1;
      vbi->enabled = 1;
      break;
    default:
      vbi->enabled = 0;
      break;
  }
}

void
vbidata_process_frame (vbidata_t * vbi, int printdebug)
{
  if (read (vbi->fd, vbi->buf, 65536) < 65536) {
    fprintf (stderr, "error, can't read vbi data.\n");
    return;
  }

  ProcessLine (vbi, &vbi->buf[DO_LINE * 2048], 0);
  ProcessLine (vbi, &vbi->buf[(16 + DO_LINE) * 2048], 1);
}

void
vbidata_process_line (vbidata_t * vbi, unsigned char *s, int bottom)
{
  ProcessLine (vbi, s, bottom);
}

void
vbidata_process_16b (vbidata_t * vbi, int bottom, int w)
{
  Process16b (vbi, bottom, w);
}
