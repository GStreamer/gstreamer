/*
 *  glibcompat.h - System-dependent definitions
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GLIB_COMPAT_H
#define GLIB_COMPAT_H

#include <glib.h>
#include <glib-object.h>

#define G_COMPAT_DEFINE(new_api, new_args, old_api, old_args)   \
static inline void                                              \
new_api new_args                                                \
{                                                               \
    old_api old_args;                                           \
}

#undef G_COMPAT_DEFINE

#endif /* GLIB_COMPAT_H */
