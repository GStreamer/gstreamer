/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef GST_HGUARD_GSTARCH_H
#define GST_HGUARD_GSTARCH_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_CPU_I386
#include "gsti386.h"
#else
#ifdef HAVE_CPU_PPC
#include "gstppc.h"
#else
#error Need to know about this architecture, or have a generic implementation
#endif
#endif

#endif /* GST_HGUARD_GSTARCH_H */
