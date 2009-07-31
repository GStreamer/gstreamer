// GLib.ManagedValue.cs : Managed types boxer
//
// Author: Rachel Hestilow <hestilow@ximian.com>
//
// Copyright (c) 2002 Rachel Hestilow
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
	using System.Collections;
	using System.Runtime.InteropServices;
	using GLib;
	
	internal class ManagedValue {

		GCHandle gch;
		object instance;
		int ref_count = 1;
		
		private ManagedValue (object instance)
		{
			this.instance = instance;
			gch = GCHandle.Alloc (this);
		}

		IntPtr Handle {
			get { return (IntPtr) gch; }
		}

		object Instance {
			get { return instance; }
		}

		void Ref ()
		{
			ref_count++;
		}

		void Unref ()
		{
			if (--ref_count == 0) {
				instance = null;
				gch.Free ();
			}
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate IntPtr CopyFunc (IntPtr gch);
		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate void FreeFunc (IntPtr gch);
		
		static CopyFunc copy;
		static FreeFunc free;
		static GType boxed_type = GType.Invalid;

		[DllImport("libgobject-2.0-0.dll")]
		static extern IntPtr g_boxed_type_register_static (IntPtr typename, CopyFunc copy_func, FreeFunc free_func);
		
		public static GType GType {
			get {
				if (boxed_type == GType.Invalid) {
					copy = new CopyFunc (Copy);
					free = new FreeFunc (Free);
				
					IntPtr name = Marshaller.StringToPtrGStrdup ("GtkSharpValue");
					boxed_type = new GLib.GType (g_boxed_type_register_static (name, copy, free));
					Marshaller.Free (name);
				}

				return boxed_type;
			}
		}
		
		static ManagedValue FromHandle (IntPtr ptr)
		{
			GCHandle gch = (GCHandle) ptr;
			ManagedValue val = gch.Target as ManagedValue;
			if (val == null)
				throw new Exception ("Unexpected GCHandle received.");
			return val;
		}

		static IntPtr Copy (IntPtr ptr)
		{
			try {
				if (ptr == IntPtr.Zero)
					return ptr;
				ManagedValue val = FromHandle (ptr);
				val.Ref ();
				return ptr;
			} catch (Exception e) {
				ExceptionManager.RaiseUnhandledException (e, false);
			}

			return IntPtr.Zero;
		}

		static void Free (IntPtr ptr)
		{
			try {
				if (ptr == IntPtr.Zero)
					return;
				ManagedValue val = FromHandle (ptr);
				val.Unref ();
			} catch (Exception e) {
				ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		public static IntPtr WrapObject (object obj)
		{
			if (obj == null)
				return IntPtr.Zero;
			return new ManagedValue (obj).Handle;
		}

		public static object ObjectForWrapper (IntPtr ptr)
		{
			if (ptr == IntPtr.Zero)
				return null;
			ManagedValue val = FromHandle (ptr);
			return val == null ? null : val.Instance;
		}

		public static void ReleaseWrapper (IntPtr ptr)
		{
			if (ptr == IntPtr.Zero)
				return;

			ManagedValue val = FromHandle (ptr);
			val.Unref ();
		}
	}
}

