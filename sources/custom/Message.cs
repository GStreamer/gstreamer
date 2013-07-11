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

	partial class Message
	{
		[DllImport ("gstreamer-1.0") ]
		static extern void gst_message_parse_error (IntPtr msg, out IntPtr err, out IntPtr debug);

		public void ParseError (out GLib.GException error, out string debug) {
			if (Type != MessageType.Error)
				throw new ArgumentException ();

			IntPtr err;
			IntPtr dbg;

			gst_message_parse_error (Handle, out err, out dbg);

			if (dbg != IntPtr.Zero)
				debug = GLib.Marshaller.Utf8PtrToString (dbg);
			else
				debug = null;

			if (err == IntPtr.Zero)
				throw new Exception ();

			error = new GLib.GException (err);
		}

		[DllImport("gstreamer-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_message_get_stream_status_object(IntPtr raw);

		[DllImport("gstreamer-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_message_set_stream_status_object(IntPtr raw, IntPtr value);

		public GLib.Value StreamStatusObject { 
			get {
				Update ();
				if(Type != MessageType.StreamStatus)
					throw new ArgumentException ();
				IntPtr raw_ret = gst_message_get_stream_status_object(Handle);
				GLib.Value ret = (GLib.Value) Marshal.PtrToStructure (raw_ret, typeof (GLib.Value));
				return ret;
			}
			set {
				Update ();
				if(Type != MessageType.StreamStatus)
					throw new ArgumentException ();
				IntPtr native_value = GLib.Marshaller.StructureToPtrAlloc (value);
				gst_message_set_stream_status_object(Handle, native_value);
				value = (GLib.Value) Marshal.PtrToStructure (native_value, typeof (GLib.Value));
				Marshal.FreeHGlobal (native_value);
			}
		}
	}
}