//
// Version.cs: Lightweight Version Object for GStreamer
//
// Authors:
//   Aaron Bockover <abockover@novell.com>
//   Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (C) 2006 Novell, Inc.
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

using System;
using System.Runtime.InteropServices;

namespace Gst {
	public static class Version {
		private static uint major;
		private static uint minor;
		private static uint micro;
		private static uint nano;
		private static string version_string;

		static Version() {
			gst_version(out major, out minor, out micro, out nano);
		}

		public static string Description {
			get {
				if (version_string == null) {
					IntPtr version_string_ptr = gst_version_string();
					version_string = GLib.Marshaller.Utf8PtrToString(version_string_ptr);
				}

				return version_string;
			}
		}

		public static uint Major {
			get {
				return major;
			}
		}

		public static uint Minor {
			get {
				return minor;
			}
		}

		public static uint Micro {
			get {
				return micro;
			}
		}

		public static uint Nano {
			get {
				return nano;
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		private static extern void gst_version(out uint major, out uint minor, out uint micro, out uint nano);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		private static extern IntPtr gst_version_string();
	}
}