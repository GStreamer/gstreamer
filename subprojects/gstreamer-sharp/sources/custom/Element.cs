//
// Element.cs
//
// Authors:
//   Khaled Mohammed <khaled.mohammed@gmail.com>
//   Sebastian Dröge <sebastian.droege@collabora.co.uk>
//   Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (C) 2006 Khaled Mohammed
// Copyright (C) 2006 Novell, Inc.
// Copyright (C) 2009 Sebastian Dröge
// Copyright (C) 2013 Stephan Sundermann
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

	partial class Element {
		public static bool Link(params Element[] elements) {
			for (int i = 0; i < elements.Length - 1; i++) {
				if (!elements[i].Link(elements[i + 1]))
					return false;
			}
			return true;
		}

		public static void Unlink(params Element[] elements) {
			for (int i = 0; i < elements.Length - 1; i++) {
				elements[i].Unlink(elements[i + 1]);
			}
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_element_class_add_metadata(IntPtr klass, IntPtr key, IntPtr value);

		public void AddMetadata(string key, string value) {
			IntPtr native_key = GLib.Marshaller.StringToPtrGStrdup(key);
			IntPtr native_value = GLib.Marshaller.StringToPtrGStrdup(value);
			gst_element_class_add_metadata(LookupGType().GetClassPtr(), native_key, native_value);
			GLib.Marshaller.Free(native_key);
			GLib.Marshaller.Free(native_value);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_element_class_add_pad_template(IntPtr klass, IntPtr templ);

		public void AddPadTemplate(Gst.PadTemplate templ) {
			gst_element_class_add_pad_template(LookupGType().GetClassPtr(), templ == null ? IntPtr.Zero : templ.OwnedHandle);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_element_class_add_static_metadata(IntPtr klass, IntPtr key, IntPtr value);

		public void AddStaticMetadata(string key, string value) {
			IntPtr native_key = GLib.Marshaller.StringToPtrGStrdup(key);
			IntPtr native_value = GLib.Marshaller.StringToPtrGStrdup(value);
			gst_element_class_add_static_metadata(LookupGType().GetClassPtr(), native_key, native_value);
			GLib.Marshaller.Free(native_key);
			GLib.Marshaller.Free(native_value);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_element_class_set_metadata(IntPtr klass, IntPtr longname, IntPtr classification, IntPtr description, IntPtr author);

		public void SetMetadata(string longname, string classification, string description, string author) {
			IntPtr native_longname = GLib.Marshaller.StringToPtrGStrdup(longname);
			IntPtr native_classification = GLib.Marshaller.StringToPtrGStrdup(classification);
			IntPtr native_description = GLib.Marshaller.StringToPtrGStrdup(description);
			IntPtr native_author = GLib.Marshaller.StringToPtrGStrdup(author);
			gst_element_class_set_metadata(LookupGType().GetClassPtr(), native_longname, native_classification, native_description, native_author);
			GLib.Marshaller.Free(native_longname);
			GLib.Marshaller.Free(native_classification);
			GLib.Marshaller.Free(native_description);
			GLib.Marshaller.Free(native_author);
		}

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void gst_element_class_set_static_metadata(IntPtr klass, IntPtr longname, IntPtr classification, IntPtr description, IntPtr author);

		public void SetStaticMetadata(string longname, string classification, string description, string author) {
			IntPtr native_longname = GLib.Marshaller.StringToPtrGStrdup(longname);
			IntPtr native_classification = GLib.Marshaller.StringToPtrGStrdup(classification);
			IntPtr native_description = GLib.Marshaller.StringToPtrGStrdup(description);
			IntPtr native_author = GLib.Marshaller.StringToPtrGStrdup(author);
			gst_element_class_set_static_metadata(LookupGType().GetClassPtr(), native_longname, native_classification, native_description, native_author);
			GLib.Marshaller.Free(native_longname);
			GLib.Marshaller.Free(native_classification);
			GLib.Marshaller.Free(native_description);
			GLib.Marshaller.Free(native_author);
		}

	}
}