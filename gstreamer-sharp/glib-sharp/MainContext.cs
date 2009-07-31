// GLib.MainContext.cs - mainContext class implementation
//
// Author: Radek Doulik <rodo@matfyz.cz>
//
// Copyright (c) 2003 Radek Doulik
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
	using System.Runtime.InteropServices;

        public class MainContext {
		
		[DllImport("libglib-2.0-0.dll")]
		static extern int g_main_depth ();
		public static int Depth {
			get { return g_main_depth (); }
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern bool g_main_context_iteration (IntPtr Raw, bool MayBlock);

		public static bool Iteration ()
		{
			return g_main_context_iteration (IntPtr.Zero, false);
		}

		public static bool Iteration (bool MayBlock)
		{
			return g_main_context_iteration (IntPtr.Zero, MayBlock);
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern bool g_main_context_pending (IntPtr Raw);
		
		public static bool Pending ()
		{
			return g_main_context_pending (IntPtr.Zero);
		}
	}
}
