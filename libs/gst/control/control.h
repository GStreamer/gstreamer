/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstcontrol.h: GStreamer control utility library
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_CONTROL_H__
#define __GST_CONTROL_H__

#include <gst/control/dparammanager.h>
#include <gst/control/dparam.h>
#include <gst/control/dparam_smooth.h>
#include <gst/control/dplinearinterp.h>
#include <gst/control/unitconvert.h>

G_BEGIN_DECLS void gst_control_init (int *argc, char **argv[]);

G_END_DECLS
#endif /* __GST_CONTROL_H__ */
