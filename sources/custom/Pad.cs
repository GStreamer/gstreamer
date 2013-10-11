// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	partial class Pad 
	{
		[GLib.Property ("caps")]
		public Gst.Caps Caps {
			get {
				GLib.Value val = GetProperty ("caps");
				Gst.Caps ret = new Gst.Caps ((IntPtr)val);
				val.Dispose ();
				return ret;
			}
		}
	}
}