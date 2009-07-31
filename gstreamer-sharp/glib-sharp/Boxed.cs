// GtkSharp.Boxed.cs - Base class for deriving marshallable structures.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2002 Mike Kestner
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

	[Obsolete]
	public class Boxed {
		object obj;
		IntPtr raw; 

		public Boxed (object o)
		{
			this.obj = o;
		}

		public Boxed (IntPtr ptr)
		{
			this.raw = ptr;
		}

		public virtual IntPtr Handle {
			get {
				return raw;
			}
			set {
				raw = value;
			}
		}

		public static explicit operator System.IntPtr (Boxed boxed) {
			return boxed.Handle;
		}

		public virtual object Obj {
			get {
				return obj;
			}
			set {
				obj = value;
			}
		}
	}
}
