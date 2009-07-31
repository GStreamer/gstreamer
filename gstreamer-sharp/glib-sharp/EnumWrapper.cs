// EnumWrapper.cs - Class to hold arbitrary glib enums 
//
// Author: Rachel Hestilow <hestilow@ximian.com> 
//
// Copyright (c) 2002 Rachel Hestilow
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
	
	[Obsolete ("Replaced by direct enum type casts to/from GLib.Value")]
	public class EnumWrapper {
		int val;
		public bool flags;
		
		public EnumWrapper (int val, bool flags) {
			this.val = val;
			this.flags = flags;
		}

		public static explicit operator int (EnumWrapper wrap) {
			return wrap.val;
		}
	}
}

