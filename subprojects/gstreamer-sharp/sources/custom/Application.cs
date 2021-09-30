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

	partial class Application {
		// Because of: https://bugzilla.gnome.org/show_bug.cgi?id=743062#c30
		private static uint MIN_GSTREAMER_MINOR = 14;

		static Application() {
			GLib.GType.Register(List.GType, typeof(List));
			GLib.GType.Register(Fraction.GType, typeof(Fraction));
			GLib.GType.Register(DoubleRange.GType, typeof(DoubleRange));
			GLib.GType.Register(IntRange.GType, typeof(IntRange));
			GLib.GType.Register(FractionRange.GType, typeof(FractionRange));
			GLib.GType.Register(DateTime.GType, typeof(DateTime));
			GLib.GType.Register(Gst.Array.GType, typeof(Gst.Array));
			GLib.GType.Register(Promise.GType, typeof(Promise));
			GLib.GType.Register(Gst.WebRTC.WebRTCSessionDescription.GType, typeof(Gst.WebRTC.WebRTCSessionDescription));
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_init(ref int argc, ref IntPtr[] argv);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_init(IntPtr argc, IntPtr argv);

		private static void CheckVersion() {
			if (Gst.Version.Minor < MIN_GSTREAMER_MINOR)
				throw new Exception(Gst.Version.Description + " found but GStreamer 1." +
					MIN_GSTREAMER_MINOR + " required.");
		}

		public static void Init() {
			gst_init(IntPtr.Zero, IntPtr.Zero);
			CheckVersion();
		}

		public static void Init(ref string[] argv) {
			int cnt_argv = argv == null ? 0 : argv.Length;
			System.Collections.Generic.List<IntPtr> native_arg_list = new System.Collections.Generic.List<IntPtr>();
			for (int i = 0; i < cnt_argv; i++)
				native_arg_list.Add(GLib.Marshaller.StringToPtrGStrdup(argv[i]));
			IntPtr[] native_argv = native_arg_list.ToArray();
			gst_init(ref cnt_argv, ref native_argv);
			foreach (var native_arg in native_arg_list)
				GLib.Marshaller.Free(native_arg);

			CheckVersion();
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_init_check(ref int argc, ref IntPtr[] argv, out IntPtr error);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_init_check(IntPtr argc, IntPtr argv, out IntPtr error);

		public static bool InitCheck() {
			IntPtr error = IntPtr.Zero;
			bool ret = gst_init_check(IntPtr.Zero, IntPtr.Zero, out error);

			CheckVersion();
			if (error != IntPtr.Zero) throw new GLib.GException(error);
			return ret;
		}

		public static bool InitCheck(ref string[] argv) {
			int cnt_argv = argv == null ? 0 : argv.Length;
			System.Collections.Generic.List<IntPtr> native_arg_list = new System.Collections.Generic.List<IntPtr>();
			for (int i = 0; i < cnt_argv; i++)
				native_arg_list.Add(GLib.Marshaller.StringToPtrGStrdup(argv[i]));
			IntPtr[] native_argv = native_arg_list.ToArray();
			IntPtr error = IntPtr.Zero;
			bool ret = gst_init_check(ref cnt_argv, ref native_argv, out error);
			foreach (var native_arg in native_arg_list)
				GLib.Marshaller.Free(native_arg);
			if (error != IntPtr.Zero) throw new GLib.GException(error);

			CheckVersion();
			return ret;
		}
	}
}