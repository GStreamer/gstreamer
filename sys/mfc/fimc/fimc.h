/*
 * Copyright (c) 2012 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * two licenses are the Apache License 2.0 and the LGPL.
 *
 * Apache License 2.0:
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LGPL:
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
 *
 */

#ifndef __FIMC_H__
#define __FIMC_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Fimc Fimc;

typedef enum {
  FIMC_COLOR_FORMAT_YUV420SPT,
  FIMC_COLOR_FORMAT_YUV420SP,
  FIMC_COLOR_FORMAT_YUV420P,
  FIMC_COLOR_FORMAT_RGB32
} FimcColorFormat;

void fimc_init_debug (void);

Fimc * fimc_new (void);
void fimc_free (Fimc * fimc);

int fimc_set_src_format (Fimc *fimc, FimcColorFormat format, int width, int height, int stride[3], int crop_left, int crop_top, int crop_width, int crop_height);
int fimc_request_src_buffers (Fimc *fimc);
int fimc_release_src_buffers (Fimc *fimc);

int fimc_set_dst_format (Fimc *fimc, FimcColorFormat format, int width, int height, int stride[3], int crop_left, int crop_top, int crop_width, int crop_height);
int fimc_request_dst_buffers (Fimc *fimc);
int fimc_request_dst_buffers_mmap (Fimc *fimc, void *dst[3], int stride[3]);
int fimc_release_dst_buffers (Fimc *fimc);

int fimc_convert (Fimc *fimc, void *src[3], void *dst[3]);

#ifdef __cplusplus
}
#endif

#endif /* __FIMC_H__ */
