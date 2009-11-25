// Gst.GLib.Value.cs - Gst.GLib Value class implementation
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001 Mike Kestner
// Copyright (c) 2003-2004 Novell, Inc.
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
	using System.Collections;
	using System.Reflection;
	using System.Runtime.InteropServices;

	[StructLayout (LayoutKind.Sequential)]
	public struct Value : IDisposable {

		[StructLayout(LayoutKind.Explicit)]
		struct Padding {
			[FieldOffset (0)] int v_int;
			[FieldOffset (0)] uint v_uint;
			[FieldOffset (0)] int v_long;
			[FieldOffset (0)] uint v_ulong;
			[FieldOffset (0)] long v_int64;
			[FieldOffset (0)] ulong v_uint64;
			[FieldOffset (0)] float v_float;
			[FieldOffset (0)] double v_double;
			[FieldOffset (0)] IntPtr v_pointer;
		}

		IntPtr type;
		Padding pad1;
		Padding pad2;

		public static Value Empty;

		public Value (Gst.GLib.GType gtype)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			g_value_init (ref this, gtype.Val);
		}

		public Value (object obj)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();

			GType gtype = (GType) obj.GetType ();
			g_value_init (ref this, gtype.Val);
			Val = obj;
		}

		public Value (bool val) : this (GType.Boolean)
		{
			g_value_set_boolean (ref this, val);
		}

		public Value (byte val) : this (GType.UChar)
		{
			g_value_set_uchar (ref this, val);
		}

		public Value (sbyte val) : this (GType.Char)
		{
			g_value_set_char (ref this, val);
		}

		public Value (int val) : this (GType.Int)
		{
			g_value_set_int (ref this, val);
		}

		public Value (uint val) : this (GType.UInt)
		{
			g_value_set_uint (ref this, val); 
		}

		public Value (ushort val) : this (GType.UInt)
		{
			g_value_set_uint (ref this, val); 
		}

		public Value (long val) : this (GType.Int64)
		{
			g_value_set_int64 (ref this, val);
		}

		public Value (ulong val) : this (GType.UInt64)
		{
			g_value_set_uint64 (ref this, val);
		}

		[Obsolete ("Replaced by Value(object) constructor")]
		public Value (EnumWrapper wrap, string type_name)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			g_value_init (ref this, GType.FromName (type_name).Val);
			if (wrap.flags)
				g_value_set_flags (ref this, (uint) (int) wrap); 
			else
				g_value_set_enum (ref this, (int) wrap); 
		}

		public Value (float val) : this (GType.Float)
		{
			g_value_set_float (ref this, val);
		}

		public Value (double val) : this (GType.Double)
		{
			g_value_set_double (ref this, val);
		}

		public Value (string val) : this (GType.String)
		{
			IntPtr native_val = Gst.GLib.Marshaller.StringToPtrGStrdup (val);
			g_value_set_string (ref this, native_val); 
			Gst.GLib.Marshaller.Free (native_val);
		}

		public Value (ValueArray val) : this (ValueArray.GType)
		{
			g_value_set_boxed (ref this, val.Handle);
		}

		public Value (IntPtr val) : this (GType.Pointer)
		{
			g_value_set_pointer (ref this, val); 
		}

		public Value (Opaque val, string type_name)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			g_value_init (ref this, GType.FromName (type_name).Val);
			g_value_set_boxed (ref this, val.Handle);
		}

		public Value (Gst.GLib.Object val) : this (val == null ? GType.Object : val.NativeType)
		{
			g_value_set_object (ref this, val == null ? IntPtr.Zero : val.Handle);
		}

		public Value (Gst.GLib.GInterfaceAdapter val) : this (val == null ? GType.Object : val.GType)
		{
			g_value_set_object (ref this, val == null ? IntPtr.Zero : val.Handle);
		}

		public Value (Gst.GLib.Object obj, string prop_name)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			InitForProperty (obj, prop_name);
		}

		[Obsolete]
		public Value (Gst.GLib.Object obj, string prop_name, EnumWrapper wrap)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			InitForProperty (obj.NativeType, prop_name);
			if (wrap.flags)
				g_value_set_flags (ref this, (uint) (int) wrap); 
			else
				g_value_set_enum (ref this, (int) wrap); 
		}

		[Obsolete]
		public Value (IntPtr obj, string prop_name, Opaque val)
		{
			type = IntPtr.Zero;
			pad1 = new Padding ();
			pad2 = new Padding ();
			InitForProperty (Gst.GLib.Object.GetObject (obj), prop_name);
			g_value_set_boxed (ref this, val.Handle);
		}

		public Value (string[] val) : this (new Gst.GLib.GType (g_strv_get_type ()))
		{
			if (val == null) {
				g_value_set_boxed (ref this, IntPtr.Zero);
				return;
			}

			IntPtr native_array = Marshal.AllocHGlobal ((val.Length + 1) * IntPtr.Size);
			for (int i = 0; i < val.Length; i++)
				Marshal.WriteIntPtr (native_array, i * IntPtr.Size, Gst.GLib.Marshaller.StringToPtrGStrdup (val[i]));
			Marshal.WriteIntPtr (native_array, val.Length * IntPtr.Size, IntPtr.Zero);

			g_value_set_boxed (ref this, native_array);

			for (int i = 0; i < val.Length; i++)
				Gst.GLib.Marshaller.Free (Marshal.ReadIntPtr (native_array, i * IntPtr.Size));
			Marshal.FreeHGlobal (native_array);
		}


		public void Dispose () 
		{
			g_value_unset (ref this);
		}

		public void Init (Gst.GLib.GType gtype)
		{
			g_value_init (ref this, gtype.Val);
		}


		public static explicit operator bool (Value val)
		{
			return g_value_get_boolean (ref val);
		}

		public static explicit operator byte (Value val)
		{
			return g_value_get_uchar (ref val);
		}

		public static explicit operator sbyte (Value val)
		{
			return g_value_get_char (ref val);
		}

		public static explicit operator int (Value val)
		{
			return g_value_get_int (ref val);
		}

		public static explicit operator uint (Value val)
		{
			return g_value_get_uint (ref val);
		}

		public static explicit operator ushort (Value val)
		{
			return (ushort) g_value_get_uint (ref val);
		}

		public static explicit operator long (Value val)
		{
			if (val.type == GType.Long.Val)
				return val.GetLongForPlatform ();
			else
				return g_value_get_int64 (ref val);
		}

		public static explicit operator ulong (Value val)
		{
			if (val.type == GType.ULong.Val)
				return val.GetULongForPlatform ();
			else
				return g_value_get_uint64 (ref val);
		}

		[Obsolete ("Replaced by Enum cast")]
		public static explicit operator EnumWrapper (Value val)
		{
			if (val.HoldsFlags)
				return new EnumWrapper ((int)g_value_get_flags (ref val), true);
			else
				return new EnumWrapper (g_value_get_enum (ref val), false);
		}

		public static explicit operator Enum (Value val)
		{
			if (val.HoldsFlags)
				return (Enum)Enum.ToObject (GType.LookupType (val.type), g_value_get_flags (ref val));
			else
				return (Enum)Enum.ToObject (GType.LookupType (val.type), g_value_get_enum (ref val));
		}

		public static explicit operator float (Value val)
		{
			return g_value_get_float (ref val);
		}

		public static explicit operator double (Value val)
		{
			return g_value_get_double (ref val);
		}

		public static explicit operator string (Value val)
		{
			IntPtr str = g_value_get_string (ref val);
			return str == IntPtr.Zero ? null : Gst.GLib.Marshaller.Utf8PtrToString (str);
		}

		public static explicit operator ValueArray (Value val)
		{
			return new ValueArray (g_value_get_boxed (ref val));
		}

		public static explicit operator IntPtr (Value val)
		{
			return g_value_get_pointer (ref val);
		}

		public static explicit operator Gst.GLib.Opaque (Value val)
		{
			return Gst.GLib.Opaque.GetOpaque (g_value_get_boxed (ref val), (Type) new GType (val.type), false);
		}

		public static explicit operator Gst.GLib.Boxed (Value val)
		{
			return new Gst.GLib.Boxed (g_value_get_boxed (ref val));
		}

		public static explicit operator Gst.GLib.Object (Value val)
		{
			return Gst.GLib.Object.GetObject (g_value_get_object (ref val), false);
		}

		[Obsolete ("Replaced by Gst.GLib.Object cast")]
		public static explicit operator Gst.GLib.UnwrappedObject (Value val)
		{
			return new UnwrappedObject (g_value_get_object (ref val));
		}

		public static explicit operator string[] (Value val)
		{
			IntPtr native_array = g_value_get_boxed (ref val);
			if (native_array == IntPtr.Zero)
				return null;

			int count = 0;
			while (Marshal.ReadIntPtr (native_array, count * IntPtr.Size) != IntPtr.Zero)
				count++;
			string[] strings = new string[count];
			for (int i = 0; i < count; i++)
				strings[i] = Gst.GLib.Marshaller.Utf8PtrToString (Marshal.ReadIntPtr (native_array, i * IntPtr.Size));
			return strings;
		}

		object ToRegisteredType () {
			Type t = Gst.GLib.GType.LookupType (type);
			ConstructorInfo ci = null;
			
			try {
				while (ci == null && t != null) {
					if (!t.IsAbstract)
						ci = t.GetConstructor (new Type[] { typeof (Gst.GLib.Value) });
					if (ci == null)
						t = t.BaseType;
				}
			} catch (Exception) {
				ci = null;
			}

			if (ci == null)
				throw new Exception ("Unknown type " + new GType (type).ToString ());
			
			return ci.Invoke (new object[] {this});
		}

		void FromRegisteredType (object val) {
			Type t = Gst.GLib.GType.LookupType (type);
			MethodInfo mi = null;
			
			try {
				while (mi == null && t != null) {
					mi = t.GetMethod ("SetGValue", new Type[] { Type.GetType ("Gst.GLib.Value&") });
					if (mi != null && (mi.IsAbstract || mi.ReturnType != typeof (void)))
						mi = null;
					if (mi == null)
						t = t.BaseType;
				}
			} catch (Exception) {
				mi = null;
			}
			
			if (mi == null)
				throw new Exception ("Unknown type " + new GType (type).ToString ());
			
			object[] parameters = new object[] { this };
			mi.Invoke (val, parameters);
			this = (Gst.GLib.Value) parameters[0];
		}

		long GetLongForPlatform ()
		{
			switch (Environment.OSVersion.Platform) {
			case PlatformID.Win32NT:
			case PlatformID.Win32S:
			case PlatformID.Win32Windows:
			case PlatformID.WinCE:
				return (long) g_value_get_long_as_int (ref this);
			default:
				return g_value_get_long (ref this).ToInt64 ();
			}
		}

		ulong GetULongForPlatform ()
		{
			switch (Environment.OSVersion.Platform) {
			case PlatformID.Win32NT:
			case PlatformID.Win32S:
			case PlatformID.Win32Windows:
			case PlatformID.WinCE:
				return (ulong) g_value_get_ulong_as_uint (ref this);
			default:
				return g_value_get_ulong (ref this).ToUInt64 ();
			}
		}

		void SetLongForPlatform (long val)
		{
			switch (Environment.OSVersion.Platform) {
			case PlatformID.Win32NT:
			case PlatformID.Win32S:
			case PlatformID.Win32Windows:
			case PlatformID.WinCE:
				g_value_set_long (ref this, (int) val);
				break;
			default:
				g_value_set_long (ref this, new IntPtr (val));
				break;
			}
		}

		void SetULongForPlatform (ulong val)
		{
			switch (Environment.OSVersion.Platform) {
			case PlatformID.Win32NT:
			case PlatformID.Win32S:
			case PlatformID.Win32Windows:
			case PlatformID.WinCE:
				g_value_set_ulong (ref this, (uint) val);
				break;
			default:
				g_value_set_ulong (ref this, new UIntPtr (val));
				break;
			}
		}

		object ToEnum ()
		{
			Type t = GType.LookupType (type);
			
			if (t == null) {
				if (HoldsFlags)
					return g_value_get_flags (ref this);
				else
					return g_value_get_enum (ref this);
			} else {
				return (Enum) this;
			}
		}

		object ToBoxed ()
		{
			IntPtr boxed_ptr = g_value_get_boxed (ref this);
			Type t = GType.LookupType (type);
			if (t == null)
				throw new Exception ("Unknown type " + new GType (type).ToString ());
			else if (t.IsSubclassOf (typeof (Gst.GLib.Opaque)))
				return (Gst.GLib.Opaque) this;

			MethodInfo mi = t.GetMethod ("New", BindingFlags.Static | BindingFlags.Public | BindingFlags.FlattenHierarchy);
			if (mi == null)
				return Marshal.PtrToStructure (boxed_ptr, t);
			else
				return mi.Invoke (null, new object[] {boxed_ptr});
		}

		public object Val
		{
			get {
				if (type == GType.Boolean.Val)
					return (bool) this;
				else if (type == GType.UChar.Val)
					return (byte) this;
				else if (type == GType.Char.Val)
					return (sbyte) this;
				else if (type == GType.Int.Val)
					return (int) this;
				else if (type == GType.UInt.Val)
					return (uint) this;
				else if (type == GType.Int64.Val)
					return (long) this;
				else if (type == GType.Long.Val)
					return GetLongForPlatform ();
				else if (type == GType.UInt64.Val)
					return (ulong) this;
				else if (type == GType.ULong.Val)
					return GetULongForPlatform ();
				else if (GType.Is (type, GType.Enum) ||
					 GType.Is (type, GType.Flags))
					return ToEnum ();
				else if (type == GType.Float.Val)
					return (float) this;
				else if (type == GType.Double.Val)
					return (double) this;
				else if (type == GType.String.Val)
					return (string) this;
				else if (type == GType.Pointer.Val)
					return (IntPtr) this;
				else if (type == GType.Param.Val)
					return g_value_get_param (ref this);
				else if (type == ValueArray.GType.Val)
					return new ValueArray (g_value_get_boxed (ref this));
				else if (type == ManagedValue.GType.Val)
					return ManagedValue.ObjectForWrapper (g_value_get_boxed (ref this));
				else if (GType.Is (type, GType.Object))
					return (Gst.GLib.Object) this;
				else if (GType.Is (type, GType.Boxed))
					return ToBoxed ();
				else if (GType.LookupType (type) != null)
					return ToRegisteredType ();
				else if (type == IntPtr.Zero)
					return null;
				else
					throw new Exception ("Unknown type " + new GType (type).ToString ());
			}
			set {
				if (type == GType.Boolean.Val)
					g_value_set_boolean (ref this, (bool) value);
				else if (type == GType.UChar.Val)
					g_value_set_uchar (ref this, (byte) value);
				else if (type == GType.Char.Val)
					g_value_set_char (ref this, (sbyte) value);
				else if (type == GType.Int.Val)
					g_value_set_int (ref this, (int) value);
				else if (type == GType.UInt.Val)
					g_value_set_uint (ref this, (uint) value);
				else if (type == GType.Int64.Val)
					g_value_set_int64 (ref this, (long) value);
				else if (type == GType.Long.Val)
					SetLongForPlatform ((long) value);
				else if (type == GType.UInt64.Val)
					g_value_set_uint64 (ref this, (ulong) value);
				else if (type == GType.ULong.Val)
					SetULongForPlatform (Convert.ToUInt64 (value));
				else if (GType.Is (type, GType.Enum))
					g_value_set_enum (ref this, (int)value);
				else if (GType.Is (type, GType.Flags))
					g_value_set_flags (ref this, (uint)(int)value);
				else if (type == GType.Float.Val)
					g_value_set_float (ref this, (float) value);
				else if (type == GType.Double.Val)
					g_value_set_double (ref this, (double) value);
				else if (type == GType.String.Val) {
					IntPtr native = Gst.GLib.Marshaller.StringToPtrGStrdup ((string)value);
					g_value_set_string (ref this, native);
					Gst.GLib.Marshaller.Free (native);
				} else if (type == GType.Pointer.Val) {
					if (value.GetType () == typeof (IntPtr)) {
						g_value_set_pointer (ref this, (IntPtr) value);
						return;
					} else if (value is IWrapper) {
						g_value_set_pointer (ref this, ((IWrapper)value).Handle);
						return;
					}
					IntPtr buf = Marshal.AllocHGlobal (Marshal.SizeOf (value.GetType()));
					Marshal.StructureToPtr (value, buf, false);
					g_value_set_pointer (ref this, buf);
				} else if (type == GType.Param.Val) {
					g_value_set_param (ref this, (IntPtr) value);
				} else if (type == ValueArray.GType.Val) {
					g_value_set_boxed (ref this, ((ValueArray) value).Handle);
				} else if (type == ManagedValue.GType.Val) {
					IntPtr wrapper = ManagedValue.WrapObject (value);
					g_value_set_boxed (ref this, wrapper);
					ManagedValue.ReleaseWrapper (wrapper);
				} else if (GType.Is (type, GType.Object))
					if(value is Gst.GLib.Object)
						g_value_set_object (ref this, (value as Gst.GLib.Object).Handle);
					else
						g_value_set_object (ref this, (value as Gst.GLib.GInterfaceAdapter).Handle);
				else if (GType.Is (type, GType.Boxed)) {
					if (value is IWrapper) {
						g_value_set_boxed (ref this, ((IWrapper)value).Handle);
						return;
					}
					IntPtr buf = Marshaller.StructureToPtrAlloc (value);
					g_value_set_boxed (ref this, buf);
					Marshal.FreeHGlobal (buf);
				} else if (Gst.GLib.GType.LookupType (type) != null) {
					FromRegisteredType (value);
				} else
					throw new Exception ("Unknown type " + new GType (type).ToString ());
			}
		}

		internal void Update (object val)
		{
			if (GType.Is (type, GType.Boxed) && !(val is IWrapper))
				Marshal.StructureToPtr (val, g_value_get_boxed (ref this), false);
		}

		bool HoldsFlags {
			get { return g_type_check_value_holds (ref this, GType.Flags.Val); }
		}

		void InitForProperty (Object obj, string name)
		{
			GType gtype = obj.NativeType;
			InitForProperty (gtype, name);
		}

		void InitForProperty (GType gtype, string name)
		{
			IntPtr p_name = Marshaller.StringToPtrGStrdup (name);
			IntPtr spec_ptr = g_object_class_find_property (gtype.GetClassPtr (), p_name);
			Marshaller.Free (p_name);

			if (spec_ptr == IntPtr.Zero)
				throw new Exception (String.Format ("No property with name '{0}' in type '{1}'", name, gtype.ToString()));
			
			ParamSpec spec = new ParamSpec (spec_ptr);
			g_value_init (ref this, spec.ValueType.Val);
		}

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_object_class_find_property (IntPtr klass, IntPtr name);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool g_type_check_value_holds (ref Value val, IntPtr gtype);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_init (ref Gst.GLib.Value val, IntPtr gtype);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_unset (ref Gst.GLib.Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_boolean (ref Value val, bool data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_uchar (ref Value val, byte data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_char (ref Value val, sbyte data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_boxed (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_double (ref Value val, double data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_float (ref Value val, float data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_int (ref Value val, int data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_int64 (ref Value val, long data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_long (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_long (ref Value val, int data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_uint64 (ref Value val, ulong data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_object (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_param (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_pointer (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_string (ref Value val, IntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_uint (ref Value val, uint data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_ulong (ref Value val, UIntPtr data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_ulong (ref Value val, uint data);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_enum (ref Value val, int data);
		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_value_set_flags (ref Value val, uint data);
		
		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool g_value_get_boolean (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern byte g_value_get_uchar (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern sbyte g_value_get_char (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_boxed (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern double g_value_get_double (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern float g_value_get_float (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern int g_value_get_int (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern long g_value_get_int64 (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_long (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", EntryPoint = "g_value_get_long", CallingConvention = CallingConvention.Cdecl)]
		static extern int g_value_get_long_as_int (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern ulong g_value_get_uint64 (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern UIntPtr g_value_get_ulong (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", EntryPoint = "g_value_get_ulong", CallingConvention = CallingConvention.Cdecl)]
		static extern int g_value_get_ulong_as_uint (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_object (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_param (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_pointer (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_get_string (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern uint g_value_get_uint (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern int g_value_get_enum (ref Value val);
		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern uint g_value_get_flags (ref Value val);

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_strv_get_type ();
	}
}
