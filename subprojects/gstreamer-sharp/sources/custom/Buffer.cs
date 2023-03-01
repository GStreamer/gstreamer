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
	using System.Collections;
	using System.Runtime.InteropServices;

	partial class Buffer {
		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern UIntPtr gst_buffer_extract(IntPtr raw, UIntPtr offset, byte[] dest, UIntPtr size);

		public ulong Extract(ulong offset, ref byte[] dest) {
			UIntPtr raw_ret = gst_buffer_extract(Handle, new UIntPtr(offset), dest, new UIntPtr((ulong)dest.Length));
			ulong ret = (ulong)raw_ret;
			return ret;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_buffer_extract_dup(IntPtr raw, UIntPtr offset, UIntPtr size, out IntPtr dest, out UIntPtr dest_size);

		public ulong ExtractDup(ulong offset, ulong size, out byte[] dest) {
			UIntPtr native_dest_size;
			IntPtr ptr;
			gst_buffer_extract_dup(Handle, new UIntPtr(offset), new UIntPtr(size), out ptr, out native_dest_size);

			byte[] bytes = new byte[(ulong)native_dest_size];
			Marshal.Copy(ptr, bytes, 0, (int)native_dest_size);

			dest = bytes;
			GLib.Marshaller.Free(ptr);

			return (ulong)native_dest_size;
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_buffer_new_wrapped(IntPtr data, UIntPtr n_length);

		public Buffer(byte[] data) : base(IntPtr.Zero) {
			IntPtr ptr = GLib.Marshaller.Malloc((ulong)data.Length);
			Marshal.Copy(data, 0, ptr, data.Length);
			Raw = gst_buffer_new_wrapped(ptr, new UIntPtr((ulong)data.Length));
		}
	}
}