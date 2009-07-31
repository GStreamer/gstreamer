// DelegateWrapper.cs - Delegate wrapper implementation
//
// Authors:
// 	Rachel Hestilow <hestilow@ximian.com>
// 	Gonzalo Panigua Javier <gonzalo@ximian.com>
//
// Copyright (c) 2002 Rachel Hestilow
// Copyright (c) 2003 Ximian, Inc.
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

	public class DelegateWrapper
	{
		// Keys in the hashtable are instances of classes derived from this one.
		// Values are each instance's destroy notification delegate
		static Hashtable instances = new Hashtable ();

		// This list holds references to wrappers for static
		// methods. These will never expire.
		static ArrayList static_instances = new ArrayList ();

		static int notify_count = 0;
		
		// The object 'o' is the object that creates the instance of the DelegateWrapper
		// derived class or null if created from a static method.
		// Note that the instances will never be disposed if they are created in a static
		// method.
		[Obsolete ("Callback wrappers should be manually managed for persistence.")]
		protected DelegateWrapper (object o)
		{
			if (o != null) {
				// If o is a GObject, we can get
				// destroy notification. Otherwise
				// no additional references to
				// the wrapper are kept.
				// FIXME: This should work because
				// currently only GObjects store
				// callbacks over the long-term

				if (o is GLib.Object) {
					AddDestroyNotify ((GLib.Object) o);
				}
			} else {
				// If o is null, we cannot ask for a destroy
				// notification, so the wrapper never expires.

				lock (typeof (DelegateWrapper)) {
					static_instances.Add (this);
				}
			}
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		private delegate void DestroyNotify (IntPtr data);

		[DllImport("libgobject-2.0-0.dll")]
		private static extern void g_object_set_data_full (IntPtr obj, IntPtr name, IntPtr data, DestroyNotify destroy);
		
		private void AddDestroyNotify (GLib.Object o) {
			// This is a bit of an ugly hack. There is no
			// way of getting a destroy notification
			// explicitly, so we set some data and ask
			// for notification when it is removed

			IntPtr name = Marshaller.StringToPtrGStrdup (String.Format ("_GtkSharpDelegateWrapper_{0}", notify_count));
			DestroyNotify destroy = new DestroyNotify (this.OnDestroy);

			g_object_set_data_full (o.Handle, name, IntPtr.Zero, destroy);
			Marshaller.Free (name);
			lock (typeof (DelegateWrapper)) {
				instances[this] = destroy;
				notify_count++;
			}
		}

		// This callback is invoked by GLib to indicate that the
		// object that owned the native delegate wrapper no longer
		// exists and the instance of the delegate itself is removed from the hash table.
		private void OnDestroy (IntPtr data) {
			try {
				lock (typeof (DelegateWrapper)) {
					if (instances.ContainsKey (this)) {
						instances.Remove (this);
					}
				}
			} catch (Exception e) {
				ExceptionManager.RaiseUnhandledException (e, false);
			}
		}
	}
}

