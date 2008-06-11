/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifndef TVTIME_PLUGINS_H_INCLUDED
#define TVTIME_PLUGINS_H_INCLUDED

deinterlace_method_t* dscaler_tomsmocomp_get_method( void );
deinterlace_method_t* dscaler_greedyh_get_method( void );
deinterlace_method_t* dscaler_greedyl_get_method( void );
deinterlace_method_t* dscaler_vfir_get_method( void );

//void linear_plugin_init( void );
//void scalerbob_plugin_init( void );
//void linearblend_plugin_init( void );
//void weave_plugin_init( void );
//void weavetff_plugin_init( void );
//void weavebff_plugin_init( void );

#endif /* TVTIME_PLUGINS_H_INCLUDED */
