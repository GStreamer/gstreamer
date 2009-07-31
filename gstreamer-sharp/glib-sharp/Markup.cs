// Markup.cs: Wrapper for the Markup code in Glib
//
// Authors:
//    Miguel de Icaza (miguel@ximian.com)
//
// Copyright (c) 2003 Ximian, Inc.
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


	public class Markup {
		private Markup () {}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_markup_escape_text (IntPtr text, int len);
		
		static public string EscapeText (string s)
		{
			if (s == null)
				return String.Empty;

			IntPtr native = Marshaller.StringToPtrGStrdup (s);
			string result = Marshaller.PtrToStringGFree (g_markup_escape_text (native, -1));
			Marshaller.Free (native);
			return result;
		}
	}
}
