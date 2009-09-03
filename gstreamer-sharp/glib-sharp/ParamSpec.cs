// ParamSpec.cs - GParamSpec class wrapper implementation
//
// Authors: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2008 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the Lesser GNU General 
// Public License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.


namespace Gst.GLib {

	using System;
	using System.Runtime.InteropServices;

	internal enum ParamFlags {
		None          = 0,
		Readable      = 1 << 0,
		Writable      = 1 << 1,
		Construct     = 1 << 2,
		ConstructOnly = 1 << 3,
	}

	public class ParamSpec {

		IntPtr handle;

		private static IntPtr CreateParamSpec (string name, string nick, string blurb, GType type, ParamFlags pflags) {
			int flags = (int) pflags;
			IntPtr ret;

			IntPtr p_name = GLib.Marshaller.StringToPtrGStrdup (name);
			IntPtr p_nick = GLib.Marshaller.StringToPtrGStrdup (nick);
			IntPtr p_blurb = GLib.Marshaller.StringToPtrGStrdup (blurb);

			if (type == GType.Char)
				ret = g_param_spec_char (p_name, p_nick, p_blurb, SByte.MinValue, SByte.MaxValue, 0, flags);
			else if (type == GType.UChar)
				ret = g_param_spec_uchar (p_name, p_nick, p_blurb, Byte.MinValue, Byte.MaxValue, 0, flags);
			else if (type == GType.Boolean)
				ret = g_param_spec_boolean (p_name, p_nick, p_blurb, false, flags);
			else if (type == GType.Int)
				ret = g_param_spec_int (p_name, p_nick, p_blurb, Int32.MinValue, Int32.MaxValue, 0, flags);
			else if (type == GType.UInt)
				ret = g_param_spec_uint (p_name, p_nick, p_blurb, 0, UInt32.MaxValue, 0, flags);
			else if (type == GType.Long)
				ret = g_param_spec_long (p_name, p_nick, p_blurb, IntPtr.Zero, IntPtr.Size == 4 ? new IntPtr (Int32.MaxValue) : new IntPtr (Int64.MaxValue), IntPtr.Zero, flags);
			else if (type == GType.ULong)
				ret = g_param_spec_ulong (p_name, p_nick, p_blurb, UIntPtr.Zero, UIntPtr.Size == 4 ? new UIntPtr (UInt32.MaxValue) : new UIntPtr (UInt64.MaxValue), UIntPtr.Zero, flags);
			else if (type == GType.Int64)
				ret = g_param_spec_int64 (p_name, p_nick, p_blurb, Int64.MinValue, Int64.MaxValue, 0, flags);
			else if (type == GType.UInt64)
				ret = g_param_spec_uint64 (p_name, p_nick, p_blurb, 0, UInt64.MaxValue, 0, flags);
			/*
			else if (type == GType.Enum)
			else if (type == GType.Flags)
			* TODO: 
			* Both g_param_spec_enum and g_param_spec_flags expect default property values and the members of the enum seemingly cannot be enumerated
			*/
			else if (type == GType.Float)
				ret = g_param_spec_float (p_name, p_nick, p_blurb, Single.MinValue, Single.MaxValue, 0.0f, flags);
			else if (type == GType.Double)
				ret = g_param_spec_double (p_name, p_nick, p_blurb, Double.MinValue, Double.MaxValue, 0.0, flags);
			else if (type == GType.String)
				ret = g_param_spec_string (p_name, p_nick, p_blurb, IntPtr.Zero, flags);
			else if (type == GType.Pointer)
				ret = g_param_spec_pointer (p_name, p_nick, p_blurb, flags);
			else if (type.Val == g_gtype_get_type ())
				ret = g_param_spec_gtype (p_name, p_nick, p_blurb, GType.None.Val, flags);
			else if (g_type_is_a (type.Val, GType.Boxed.Val))
				ret = g_param_spec_boxed (p_name, p_nick, p_blurb, type.Val, flags);
			else if (g_type_is_a (type.Val, GType.Object.Val))
				ret = g_param_spec_object (p_name, p_nick, p_blurb, type.Val, flags);
			else
				throw new ArgumentException ("type");

			GLib.Marshaller.Free (p_name);
			GLib.Marshaller.Free (p_nick);
			GLib.Marshaller.Free (p_blurb);

			return ret;
		}

		internal ParamSpec (string name, string nick, string blurb, GType type, ParamFlags pflags) {
			handle = CreateParamSpec (name, nick, blurb, type, pflags);
		}

		public ParamSpec (string name, string nick, string blurb, GType type, bool readable, bool writable)
		{
			ParamFlags pflags = ParamFlags.None;
			if (readable) pflags |= ParamFlags.Readable;
			if (writable) pflags |= ParamFlags.Writable;

			handle = CreateParamSpec (name, nick, blurb, type, pflags);
		}

		public ParamSpec (IntPtr native)
		{
			handle = native;
		}

		public IntPtr Handle {
			get { return handle; }
		}

		public GType ValueType {
			get {
				GParamSpec spec = (GParamSpec) Marshal.PtrToStructure (Handle, typeof (GParamSpec));
				return new GType (spec.value_type);
			}
		}

		public string Name {
			get {
				GParamSpec spec = (GParamSpec) Marshal.PtrToStructure (Handle, typeof (GParamSpec));
				return GLib.Marshaller.Utf8PtrToString (spec.name);
			}
		}

		struct GTypeInstance {
			IntPtr g_class;
		}

		struct GParamSpec {
			GTypeInstance  g_type_instance;

			public IntPtr name;
			ParamFlags flags;
			public IntPtr value_type;
			IntPtr owner_type;

			IntPtr _nick;
			IntPtr _blurb;
			IntPtr qdata;
			uint ref_count;
			uint param_id;
		}

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_char (IntPtr name, IntPtr nick, IntPtr blurb, sbyte min, sbyte max, sbyte dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_uchar (IntPtr name, IntPtr nick, IntPtr blurb, byte min, byte max, byte dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_boolean (IntPtr name, IntPtr nick, IntPtr blurb, bool dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_int (IntPtr name, IntPtr nick, IntPtr blurb, int min, int max, int dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_uint (IntPtr name, IntPtr nick, IntPtr blurb, uint min, uint max, uint dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_long (IntPtr name, IntPtr nick, IntPtr blurb, IntPtr min, IntPtr max, IntPtr dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_ulong (IntPtr name, IntPtr nick, IntPtr blurb, UIntPtr min, UIntPtr max, UIntPtr dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_int64 (IntPtr name, IntPtr nick, IntPtr blurb, long min, long max, long dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_uint64 (IntPtr name, IntPtr nick, IntPtr blurb, ulong min, ulong max, ulong dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_float (IntPtr name, IntPtr nick, IntPtr blurb, float min, float max, float dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_double (IntPtr name, IntPtr nick, IntPtr blurb, double min, double max, double dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_string (IntPtr name, IntPtr nick, IntPtr blurb, IntPtr dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_pointer (IntPtr name, IntPtr nick, IntPtr blurb, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_gtype (IntPtr name, IntPtr nick, IntPtr blurb, IntPtr dval, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_boxed (IntPtr name, IntPtr nick, IntPtr blurb, IntPtr return_type, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_param_spec_object (IntPtr name, IntPtr nick, IntPtr blurb, IntPtr return_type, int flags);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern IntPtr g_gtype_get_type ();

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = Global.CallingConvention)]
		static extern bool g_type_is_a (IntPtr a, IntPtr b);

	}
}

