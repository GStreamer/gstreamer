/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstarch.h: Architecture-specific inclusions
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

#ifndef __GST_GSTARCH_H__
#define __GST_GSTARCH_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(HAVE_CPU_I386)
#include "gsti386.h"
#elif defined (HAVE_CPU_PPC)
#include "gstppc.h"
#elif defined(HAVE_CPU_ALPHA)
#include "gstalpha.h"
#elif defined(HAVE_CPU_ARM)
#include "gstarm.h"
#elif defined(HAVE_CPU_SPARC)
#include "gstsparc.h"
#else
#error Need to know about this architecture, or have a generic implementation
#endif

#endif /* __GST_GSTARCH_H__ */
