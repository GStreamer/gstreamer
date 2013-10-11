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

namespace Gst
{
	using System;
	using System.Runtime.InteropServices;

	public partial class TagList
	{
		[DllImport("libgstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern int gst_structure_n_fields (IntPtr raw);

		public int Size {
			get {
				int raw_ret = gst_structure_n_fields (Handle);
				int ret = raw_ret;
				return ret;
			}
		}

		[DllImport("libgstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool gst_tag_list_copy_value (ref GLib.Value dest, IntPtr list, IntPtr tag);

		public object this [string tag, uint index] {
			get {
				IntPtr raw_string = GLib.Marshaller.StringToPtrGStrdup (tag);
				IntPtr raw_ret = gst_tag_list_get_value_index (Handle, raw_string, index);
				GLib.Marshaller.Free (raw_string);

				if (raw_ret == IntPtr.Zero)
					return null;

				GLib.Value v = (GLib.Value)Marshal.PtrToStructure (raw_ret, typeof(GLib.Value));

				return (object)v.Val;
			}
		}

		public object this [string tag] {
			get {
				GLib.Value v = GLib.Value.Empty;
				bool success;

				IntPtr raw_string = GLib.Marshaller.StringToPtrGStrdup (tag);
				success = gst_tag_list_copy_value (ref v, Handle, raw_string);
				GLib.Marshaller.Free (raw_string);

				if (!success)
					return null;

				object ret = (object)v.Val;
				v.Dispose ();

				return ret;
			}
		}

		public void Add (Gst.TagMergeMode mode, string tag, object value)
		{
			if (!Tag.TagExists (tag))
				throw new ArgumentException (String.Format ("Invalid tag name '{0}'", tag));

			GLib.Value v = new GLib.Value (value);
			IntPtr raw_v = GLib.Marshaller.StructureToPtrAlloc (v);

			IntPtr raw_string = GLib.Marshaller.StringToPtrGStrdup (tag);
			gst_tag_list_add_value (Handle, (int)mode, raw_string, raw_v);
			Marshal.FreeHGlobal (raw_v);
			v.Dispose ();
			GLib.Marshaller.Free (raw_string);
		}

		[DllImport("libgstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_structure_nth_field_name (IntPtr raw, uint index);

		private string NthFieldName (uint index)
		{
			IntPtr raw_ret = gst_structure_nth_field_name (Handle, index);
			string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
			return ret;
		}

		public string[] Tags {
			get {
				string[] tags = new string[Size];
				for (uint i = 0; i < Size; i++)
					tags [i] = NthFieldName (i);

				return tags;
			}
		}

		[DllImport("libgstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_structure_get_value (IntPtr raw, IntPtr fieldname);

		public GLib.List GetTag (string tag)
		{
			IntPtr raw_string = GLib.Marshaller.StringToPtrGStrdup (tag);
			IntPtr raw_ret = gst_structure_get_value (Handle, raw_string);
			GLib.Marshaller.Free (raw_string);
			GLib.Value ret = (GLib.Value)Marshal.PtrToStructure (raw_ret, typeof(GLib.Value));

			object o = ret.Val;

			if (o.GetType () == typeof(GLib.List))
				return (GLib.List)o;

			return new GLib.List (new object[] { o }, o.GetType(), true, true);
		}
	}
}