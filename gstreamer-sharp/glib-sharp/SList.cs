// SList.cs - GSList class wrapper implementation
//
// Authors: Mike Kestner <mkestner@speakeasy.net>
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
	using System.Runtime.InteropServices;

	public class SList : ListBase {

		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_slist_copy (IntPtr l);
		
		public override object Clone ()
		{
			return new SList (g_slist_copy (Handle));
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern int g_slist_length (IntPtr l);
		
		internal override int Length (IntPtr list)
		{
			return g_slist_length (list);
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern void g_slist_free(IntPtr l);

		internal override void Free (IntPtr list)
		{
			if (list != IntPtr.Zero)
				g_slist_free (list);
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_slist_append (IntPtr l, IntPtr raw);

		internal override IntPtr Append (IntPtr list, IntPtr raw)
		{
			return g_slist_append (list, raw);
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_slist_prepend (IntPtr l, IntPtr raw);

		internal override IntPtr Prepend (IntPtr list, IntPtr raw)
		{
			return g_slist_prepend (list, raw);
		}


		[DllImport("libglib-2.0-0.dll")]
	        static extern IntPtr g_slist_nth_data (IntPtr l, uint n);

		internal override IntPtr NthData (uint n)
		{
			return g_slist_nth_data (Handle, n);
		}

		public SList (IntPtr raw) : this (raw, null) {}

		public SList (System.Type element_type) : this (IntPtr.Zero, element_type) {}

		public SList (IntPtr raw, System.Type element_type) : this (raw, element_type, false, false) {}

		public SList (IntPtr raw, System.Type element_type, bool owned, bool elements_owned) : base (raw, element_type, false, false) {}

		public SList (object[] members, System.Type element_type, bool owned, bool elements_owned) : this (IntPtr.Zero, element_type, owned, elements_owned)
		{
			foreach (object o in members)
				Append (o);
		}

		public SList (Array members, System.Type element_type, bool owned, bool elements_owned) : this (IntPtr.Zero, element_type, owned, elements_owned)
		{
			foreach (object o in members)
				Append (o);
		}
	}
}
