/* Evil evil evil hack to get OSS apps to cooperate with esd
 * Copyright (C) 1998, 1999 Manish Singh <yosh@gimp.org>
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

#ifndef __GST_OSSGST_HELPER_H__
#define __GST_OSSGST_HELPER_H__

#define HELPER_MAGIC_IN		500
#define HELPER_MAGIC_OUT	501
#define HELPER_MAGIC_SNDFD	502

#define CMD_DATA 	1
#define CMD_FORMAT 	2

typedef struct {
  char id;

  union {
    unsigned int length;
    struct {
      int format;
      int stereo;
      int rate;
    } format;
  } cmd;
} command;


#endif /*  __GST_OSSGST_HELPER_H__ */
