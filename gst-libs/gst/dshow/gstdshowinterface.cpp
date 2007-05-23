/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowinterface.cpp:
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

#include "gstdshowinterface.h"


//{6A780808-9725-4d0b-8695-A4DD8D210773}
const GUID CLSID_DshowFakeSink
                    = { 0x6a780808, 0x9725, 0x4d0b, { 0x86, 0x95,  0xa4,  0xdd,  0x8d,  0x21,  0x7,  0x73 } };

// {1E38DAED-8A6E-4DEA-A482-A878761D11CB}
const GUID CLSID_DshowFakeSrc = 
{ 0x1e38daed, 0x8a6e, 0x4dea, { 0xa4, 0x82, 0xa8, 0x78, 0x76, 0x1d, 0x11, 0xcb } };

// {FC36764C-6CD4-4C73-900F-3F40BF3F191A}
static const GUID IID_IGstDshowInterface = 
{ 0xfc36764c, 0x6cd4, 0x4c73, { 0x90, 0xf, 0x3f, 0x40, 0xbf, 0x3f, 0x19, 0x1a } };
