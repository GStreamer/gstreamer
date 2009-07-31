// GLib.Source.cs - Source class implementation
//
// Author: Duncan Mak  <duncan@ximian.com>
//
// Copyright (c) 2002 Mike Kestner
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
	using System.Collections;
	using System.Runtime.InteropServices;

	public delegate bool GSourceFunc ();

	//
	// Base class for IdleProxy and TimeoutProxy
	//
	internal class SourceProxy {
		internal Delegate real_handler;
		internal Delegate proxy_handler;
		internal uint ID;

		internal void Remove ()
		{
			lock (Source.source_handlers)
				Source.source_handlers.Remove (ID);
			real_handler = null;
			proxy_handler = null;
		}
	}
	
        public class Source {
		private Source () {}
		
		internal static Hashtable source_handlers = new Hashtable ();
		
		[DllImport("libglib-2.0-0.dll")]
		static extern bool g_source_remove (uint tag);

		public static bool Remove (uint tag)
		{
			lock (Source.source_handlers)
				source_handlers.Remove (tag);
			return g_source_remove (tag);
		}
	}
}
