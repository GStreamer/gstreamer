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

namespace Gst.Base	 {
	using System;
	using System.Runtime.InteropServices;

	partial class Adapter 
	{
		[DllImport("libgstbase-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_adapter_copy(IntPtr raw, out IntPtr dest, int offset, int size);

		public byte[] Copy(int offset, int size) {

			IntPtr mem = Marshal.AllocHGlobal (size);

			gst_adapter_copy(Handle, out mem, offset, size);

			byte[] bytes = new byte[size];
			Marshal.Copy (mem, bytes, 0, size);

			return bytes;
		}

		[DllImport("libgstbase-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_adapter_map(IntPtr raw, out int size);

		public byte[] Map() {
			int size;

			IntPtr mem = gst_adapter_map (Handle, out size);
			byte[] ret = new byte[size];
			Marshal.Copy (mem, ret , 0, size);

			return ret;
		}
	}
}