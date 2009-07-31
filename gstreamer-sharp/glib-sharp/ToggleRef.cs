// GLib.ToggleRef.cs - GLib ToggleRef class implementation
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright <c> 2007 Novell, Inc.
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

	internal class ToggleRef {

		bool hardened;
		IntPtr handle;
		object reference;
		GCHandle gch;
		Hashtable signals;

		public ToggleRef (GLib.Object target)
		{
			handle = target.Handle;
			gch = GCHandle.Alloc (this);
			reference = target;
			g_object_add_toggle_ref (target.Handle, ToggleNotifyCallback, (IntPtr) gch);
			g_object_unref (target.Handle);
		}

		public bool IsAlive {
			get {
				if (reference is WeakReference) {
					WeakReference weak = reference as WeakReference;
					return weak.IsAlive;
				} else if (reference == null)
					return false;
				return true;
			}
		}

		public IntPtr Handle {
			get {
				return handle;
			}
		}

		public Hashtable Signals {
			get {
				if (signals == null)
					signals = new Hashtable ();
				return signals;
			}
		}

		public GLib.Object Target {
			get {
				if (reference == null)
					return null;
				else if (reference is GLib.Object)
					return reference as GLib.Object;

				WeakReference weak = reference as WeakReference;
				return weak.Target as GLib.Object;
			}
		}

  		public void Free ()
  		{
			Signal[] signals = new Signal [Signals.Count];
			Signals.Values.CopyTo (signals, 0);
			foreach (Signal s in signals)
				s.Free ();
			if (hardened)
				g_object_unref (handle);
			else
				g_object_remove_toggle_ref (handle, ToggleNotifyCallback, (IntPtr) gch);
			reference = null;
			gch.Free ();
		}

		internal void Harden ()
		{
			// Added for the benefit of GnomeProgram.  It releases a final ref in
			// an atexit handler which causes toggle ref notifications to occur after 
			// our delegates are gone, so we need a mechanism to override the 
			// notifications.  This method effectively leaks all objects which invoke it, 
			// but since it is only used by Gnome.Program, which is a singleton object 
			// with program duration persistence, who cares.

			g_object_ref (handle);
			g_object_remove_toggle_ref (handle, ToggleNotifyCallback, (IntPtr) gch);
			if (reference is WeakReference)
				reference = (reference as WeakReference).Target;
			hardened = true;
		}

		void Toggle (bool is_last_ref)
		{
			if (is_last_ref && reference is GLib.Object)
				reference = new WeakReference (reference);
			else if (!is_last_ref && reference is WeakReference) {
				WeakReference weak = reference as WeakReference;
				if (weak.IsAlive)
					reference = weak.Target;
			}
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate void ToggleNotifyHandler (IntPtr data, IntPtr handle, bool is_last_ref);

		static void RefToggled (IntPtr data, IntPtr handle, bool is_last_ref)
		{
			try {
				GCHandle gch = (GCHandle) data;
				ToggleRef tref = gch.Target as ToggleRef;
				tref.Toggle (is_last_ref);
			} catch (Exception e) {
				ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		static ToggleNotifyHandler toggle_notify_callback;
		static ToggleNotifyHandler ToggleNotifyCallback {
			get {
				if (toggle_notify_callback == null)
					toggle_notify_callback = new ToggleNotifyHandler (RefToggled);
				return toggle_notify_callback;
			}
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern void g_object_add_toggle_ref (IntPtr raw, ToggleNotifyHandler notify_cb, IntPtr data);

		[DllImport("libgobject-2.0-0.dll")]
		static extern void g_object_remove_toggle_ref (IntPtr raw, ToggleNotifyHandler notify_cb, IntPtr data);

		[DllImport("libgobject-2.0-0.dll")]
		static extern IntPtr g_object_ref (IntPtr raw);

		[DllImport("libgobject-2.0-0.dll")]
		static extern void g_object_unref (IntPtr raw);

	}
}
