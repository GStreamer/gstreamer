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

namespace Gst.FFT {

	using System;
	using System.Collections;
	using System.Collections.Generic;
	using System.Runtime.InteropServices;

	public partial class FFTF32 : GLib.Opaque {

		[DllImport("gstfft-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_fft_f32_new(int len, bool inverse);

		public FFTF32(int len, bool inverse) {
			Raw = gst_fft_f32_new(len, inverse);
		}

		[DllImport("gstfft-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_fft_f32_fft(IntPtr raw, float[] timedata, [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.Struct)] FFTF32Complex[] freqdata);

		public void Fft(float[] timedata, Gst.FFT.FFTF32Complex[] freqdata) {
			gst_fft_f32_fft(Handle, timedata, freqdata);
		}

		[DllImport("gstfft-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_fft_f32_window(IntPtr raw, float[] timedata, int window);

		public void Window(float[] timedata, Gst.FFT.FFTWindow window) {
			gst_fft_f32_window(Handle, timedata, (int)window);
		}
	}
}