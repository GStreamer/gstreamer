// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
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

	partial class Application 
	{
		static Application () {
			GLib.GType.Register (List.GType, typeof(List));
			GLib.GType.Register (Fraction.GType, typeof(Fraction));
			GLib.GType.Register (DoubleRange.GType, typeof(DoubleRange));
			GLib.GType.Register (IntRange.GType, typeof(IntRange));
			GLib.GType.Register (FractionRange.GType, typeof(FractionRange));
			GLib.GType.Register (DateTime.GType, typeof(DateTime));
			GLib.GType.Register (Gst.Array.GType, typeof(Gst.Array));

		}

		public static void Init() {
			gst_init (0, null);
		}
	}
}
