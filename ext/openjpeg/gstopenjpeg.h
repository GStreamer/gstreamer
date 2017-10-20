/* 
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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

#ifndef __GST_OPENJPEG_H__
#define __GST_OPENJPEG_H__

#include <openjpeg.h>

#ifdef HAVE_OPENJPEG_1
#define OPJ_CLRSPC_UNKNOWN CLRSPC_UNKNOWN
#define OPJ_CLRSPC_SRGB CLRSPC_SRGB
#define OPJ_CLRSPC_GRAY CLRSPC_GRAY
#define OPJ_CLRSPC_SYCC CLRSPC_SYCC

#define OPJ_CODEC_J2K CODEC_J2K
#define OPJ_CODEC_JP2 CODEC_JP2

#define OPJ_LRCP LRCP
#define OPJ_RLCP RLCP
#define OPJ_RPCL RPCL
#define OPJ_PCRL PCRL
#define OPJ_CPRL CPRL
#endif

#endif /* __GST_OPENJPEG_H__ */
