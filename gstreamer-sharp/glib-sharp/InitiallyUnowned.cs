// InitiallyUnowned.cs - GInitiallyUnowned class wrapper implementation
//
// Authors: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2004-2005 Novell, Inc.
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

#if GTK_SHARP_2_10

namespace GLib {

	using System;
	using System.Collections;
	using System.ComponentModel;
	using System.Runtime.InteropServices;

	public class InitiallyUnowned : Object {

		protected InitiallyUnowned (IntPtr raw) : base (raw) {}

		[Obsolete]
		protected InitiallyUnowned (GType gtype) : base (gtype) {}

		public new static GLib.GType GType {
			get {
				return GType.Object;
			}
		}

	}
}

#endif
