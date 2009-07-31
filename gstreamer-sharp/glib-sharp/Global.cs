// GLib.Global.cs - Global glib properties and methods.
//
// Author: Andres G. Aragoneses <aaragoneses@novell.com>
//
// Copyright (c) 2008 Novell, Inc
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


namespace GLib {

	using System;
	using System.Text;
	using System.Runtime.InteropServices;

	public class Global
	{

		//this is a static class
		private Global () {}

		public static string ProgramName {
			get {
				return GLib.Marshaller.PtrToStringGFree(g_get_prgname());
			}
			set { 
				IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (value);
				g_set_prgname (native_name);
				GLib.Marshaller.Free (native_name);
			}
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_set_prgname (IntPtr name);

		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_get_prgname ();

		public static string ApplicationName {
			get {
				return GLib.Marshaller.PtrToStringGFree(g_get_application_name());	
			}
			set {
				IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup (value);
				g_set_application_name (native_name);
				GLib.Marshaller.Free (native_name);				
			}
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_set_application_name (IntPtr name);

		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_get_application_name ();
	}
}
