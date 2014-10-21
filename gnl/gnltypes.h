/* GStreamer
 * Copyright (C) 2004 Edward Hervey <bilboed@bilboed.com>
 *
 * gnltypes.h: Header for class definition
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

#ifndef __GNL_TYPES_H__
#define __GNL_TYPES_H__

#include <glib.h>

typedef struct _GnlObject GnlObject;
typedef struct _GnlObjectClass GnlObjectClass;

typedef struct _GnlComposition GnlComposition;
typedef struct _GnlCompositionClass GnlCompositionClass;

typedef struct _GnlOperation GnlOperation;
typedef struct _GnlOperationClass GnlOperationClass;

typedef struct _GnlSource GnlSource;
typedef struct _GnlSourceClass GnlSourceClass;

typedef struct _GnlURISource GnlURISource;
typedef struct _GnlURISourceClass GnlURISourceClass;

#endif
