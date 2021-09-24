/*
 *  test-subpicture-data.h - subpicture data
 *
 *  Copyright (C) <2011> Intel Corporation
 *  Copyright (C) <2011> Collabora Ltd.
 *  Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef TEST_SUBPICTURE_DATA
#define TEST_SUBPICTURE_DATA

#include <glib.h>
#include "test-decode.h"

typedef struct _VideoSubpictureInfo VideoSubpictureInfo;

struct _VideoSubpictureInfo {
    guint               width;
    guint               height;
    const guint32      *data;
    guint               data_size;
};

void subpicture_get_info(VideoSubpictureInfo *info);

#endif /* TEST_SUBPICTURE_DATA*/
