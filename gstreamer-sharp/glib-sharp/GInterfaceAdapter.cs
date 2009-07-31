// GInterfaceAdapter.cs
//
// Author:   Mike Kestner  <mkestner@novell.com>
//
// Copyright (c) 2007 Novell, Inc.
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

	public delegate void GInterfaceInitHandler (IntPtr iface_ptr, IntPtr data);

	internal delegate void GInterfaceFinalizeHandler (IntPtr iface_ptr, IntPtr data);

	internal struct GInterfaceInfo {
		internal GInterfaceInitHandler InitHandler;
		internal GInterfaceFinalizeHandler FinalizeHandler;
		internal IntPtr Data;
	}

	public abstract class GInterfaceAdapter {

		GInterfaceInfo info;

		protected GInterfaceAdapter ()
		{
		}

		protected GInterfaceInitHandler InitHandler {
			set {
				info.InitHandler = value;
			}
		}

		public abstract GType GType { get; }

		public abstract IntPtr Handle { get; }

		internal GInterfaceInfo Info {
			get {
				if (info.Data == IntPtr.Zero)
					info.Data = (IntPtr) GCHandle.Alloc (this);

				return info;
			}
		}
	}
}
