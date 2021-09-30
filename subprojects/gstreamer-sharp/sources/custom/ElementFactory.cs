//
// ElementFactory.cs
//
// Authors:
//   Jakub Adam <jakub.adam@collabora.com>
//
// Copyright (C) 2020 Collabora Ltd.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301  USA

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	public partial class ElementFactory {
		public static IntPtr MakeRaw(string factoryname, string name) {
			IntPtr native_factoryname = GLib.Marshaller.StringToPtrGStrdup(factoryname);
			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup(name);
			IntPtr raw_ret = gst_element_factory_make(native_factoryname, native_name);
			GLib.Marshaller.Free(native_factoryname);
			GLib.Marshaller.Free(native_name);
			return raw_ret;
		}
	}
}