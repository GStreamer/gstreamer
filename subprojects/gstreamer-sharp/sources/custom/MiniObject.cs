// Copyright (C) 2014  Stephan Sundermann <stephansundermann@gmail.com>
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

	partial class MiniObject {

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_mini_object_replace(IntPtr olddata, IntPtr newdata);

		public static bool Replace(ref Gst.MiniObject olddata, Gst.MiniObject newdata) {
			return gst_mini_object_replace(olddata.Handle, newdata == null ? IntPtr.Zero : newdata.Handle);
		}

		public static bool Replace(ref Gst.MiniObject olddata) {
			return Replace(ref olddata, null);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_mini_object_take(IntPtr olddata, IntPtr newdata);

		public static bool Take(ref Gst.MiniObject olddata, Gst.MiniObject newdata) {
			return gst_mini_object_take(olddata.Handle, newdata == null ? IntPtr.Zero : newdata.Handle);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_mini_object_make_writable(IntPtr mini_object);

		public void MakeWritable() {
			IntPtr raw = gst_mini_object_make_writable(Raw);
			if (raw == Raw)
				return;
			Raw = raw;
			if (raw != IntPtr.Zero)
				Unref(raw);
		}

	}
}