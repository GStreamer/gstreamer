/*
    $Id$

    Copyright (C) 2001 Andrew Stevens <andrew.stevens@planet-interkom.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __FORMAT_CODES_H__
#define __FORMAT_CODES_H__

#define MPEG_FORMAT_MPEG1   0
#define MPEG_FORMAT_VCD     1
#define MPEG_FORMAT_VCD_NSR 2
#define MPEG_FORMAT_MPEG2   3
#define MPEG_FORMAT_SVCD     4
#define MPEG_FORMAT_SVCD_NSR 5
#define MPEG_FORMAT_VCD_STILL 6
#define MPEG_FORMAT_SVCD_STILL 7
#define MPEG_FORMAT_DVD      8

#define MPEG_FORMAT_FIRST 0
#define MPEG_FORMAT_LAST 8

#define MPEG_STILLS_FORMAT(x) (x==MPEG_FORMAT_VCD_STILL||x==MPEG_FORMAT_SVCD_STILL)
#endif /* __FORMAT_CODES_H__ */
