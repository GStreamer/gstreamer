// Format.cs: Wrapper for the g_format code in Glib
//
// Authors:
//    Stephane Delcroix (stephane@delcroix.org)
//
// Copyright (c) 2008 Novell, Inc.
//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the Lesser GNU General 
// Public License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.

using System;
using System.Runtime.InteropServices;

namespace GLib {
#if GTK_SHARP_2_14
	public class Format {
		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_format_size_for_display (long size);
		
		static public string SizeForDisplay (long size)
		{
			string result = Marshaller.PtrToStringGFree (g_format_size_for_display (size));
			return result;
		}
	}
#endif
}
