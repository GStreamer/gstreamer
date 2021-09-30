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

	partial class Message {
		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_message_parse_error(IntPtr msg, out IntPtr err, out IntPtr debug);

		public void ParseError(out GLib.GException error, out string debug) {

			IntPtr err;
			IntPtr dbg;

			gst_message_parse_error(Handle, out err, out dbg);

			if (dbg != IntPtr.Zero)
				debug = GLib.Marshaller.Utf8PtrToString(dbg);
			else
				debug = null;

			if (err == IntPtr.Zero)
				throw new Exception();

			error = new GLib.GException(err);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_message_get_stream_status_object(IntPtr raw);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_message_set_stream_status_object(IntPtr raw, IntPtr value);

		public GLib.Value StreamStatusObject {
			get {
				IntPtr raw_ret = gst_message_get_stream_status_object(Handle);
				GLib.Value ret = (GLib.Value)Marshal.PtrToStructure(raw_ret, typeof(GLib.Value));
				return ret;
			}
			set {
				IntPtr native_value = GLib.Marshaller.StructureToPtrAlloc(value);
				gst_message_set_stream_status_object(Handle, native_value);
				value = (GLib.Value)Marshal.PtrToStructure(native_value, typeof(GLib.Value));
				Marshal.FreeHGlobal(native_value);
			}
		}
	}
}