/* GStreamer
 * Copyright (C) <2008> Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
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

#ifndef __FTYP_CC_H__
#define __FTYP_CC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define FOURCC_ftyp	GST_MAKE_FOURCC('f','t','y','p')
#define FOURCC_isom     GST_MAKE_FOURCC('i','s','o','m')
#define FOURCC_iso2     GST_MAKE_FOURCC('i','s','o','2')
#define FOURCC_mp41     GST_MAKE_FOURCC('m','p','4','1')
#define FOURCC_mp42     GST_MAKE_FOURCC('m','p','4','2')
#define FOURCC_mjp2     GST_MAKE_FOURCC('m','j','p','2')
#define FOURCC_3gg7     GST_MAKE_FOURCC('3','g','g','7')
#define FOURCC_avc1     GST_MAKE_FOURCC('a','v','c','1')
#define FOURCC_qt__     GST_MAKE_FOURCC('q','t',' ',' ')

G_END_DECLS

#endif /* __FTYP_CC_H__ */
