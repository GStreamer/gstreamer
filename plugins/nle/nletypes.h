/* GStreamer
 * Copyright (C) 2004 Edward Hervey <bilboed@bilboed.com>
 *
 * nletypes.h: Header for class definition
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

#ifndef __NLE_TYPES_H__
#define __NLE_TYPES_H__

#include <glib.h>

typedef struct _NleObject NleObject;
typedef struct _NleObjectClass NleObjectClass;

typedef struct _NleComposition NleComposition;
typedef struct _NleCompositionClass NleCompositionClass;

typedef struct _NleOperation NleOperation;
typedef struct _NleOperationClass NleOperationClass;

typedef struct _NleSource NleSource;
typedef struct _NleSourceClass NleSourceClass;

typedef struct _NleURISource NleURISource;
typedef struct _NleURISourceClass NleURISourceClass;

#endif
