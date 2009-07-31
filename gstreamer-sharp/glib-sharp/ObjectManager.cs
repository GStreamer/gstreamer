// GLib.ObjectManager.cs - GLib ObjectManager class implementation
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright <c> 2001-2002 Mike Kestner
// Copyright <c> 2004-2005 Novell, Inc.
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


namespace GLib {

	using System;
	using System.Runtime.InteropServices;
	using System.Reflection;

	public class ObjectManager {

		static BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance | BindingFlags.CreateInstance;

		public static GLib.Object CreateObject (IntPtr raw)
		{
			if (raw == IntPtr.Zero)
				return null;

			Type type = GetTypeOrParent (raw);

			if (type == null)
				return null;

			GLib.Object obj;
			try {
				obj = Activator.CreateInstance (type, flags, null, new object[] {raw}, null) as GLib.Object;
			} catch (MissingMethodException) {
				throw new GLib.MissingIntPtrCtorException ("GLib.Object subclass " + type + " must provide a protected or public IntPtr ctor to support wrapping of native object handles.");
			}
			return obj;
		}

		[Obsolete ("Replaced by GType.Register (GType, Type)")]
		public static void RegisterType (string native_name, string managed_name, string assembly)
		{
			RegisterType (native_name, managed_name + "," + assembly);
		}

		[Obsolete ("Replaced by GType.Register (GType, Type)")]
		public static void RegisterType (string native_name, string mangled)
		{
			RegisterType (GType.FromName (native_name), Type.GetType (mangled));
		}

		[Obsolete ("Replaced by GType.Register (GType, Type)")]
		public static void RegisterType (GType native_type, System.Type type)
		{
			GType.Register (native_type, type);
		}

		static Type GetTypeOrParent (IntPtr obj)
		{
			IntPtr typeid = GType.ValFromInstancePtr (obj);
			if (typeid == GType.Invalid.Val)
				return null;

			Type result = GType.LookupType (typeid);
			while (result == null) {
				typeid = g_type_parent (typeid);
				if (typeid == IntPtr.Zero)
					return null;
				result = GType.LookupType (typeid);
			}
			return result;
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern IntPtr g_type_parent (IntPtr typ);
	}
}
