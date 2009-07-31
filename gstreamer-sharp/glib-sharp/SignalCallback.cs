// GLib.SignalCallback.cs - Signal callback base class implementation
//
// Authors: Mike Kestner <mkestner@ximian.com>
//
// Copyright (c) 2001 Mike Kestner
// Copyright (c) 2004 Novell, Inc.
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

	[Obsolete ("Replaced by GLib.Signal.")]
	public abstract class SignalCallback : IDisposable {

		// A counter used to produce unique keys for instances.
		protected static int _NextKey = 0;

		// Hashtable containing refs to all current instances.
		protected static Hashtable _Instances = new Hashtable ();

		// protected instance members
		protected GLib.Object _obj;
		protected Delegate _handler;
		protected int _key;
		protected System.Type _argstype;
		protected uint _HandlerID;

		protected SignalCallback (GLib.Object obj, Delegate eh, System.Type argstype)
		{
			_key = _NextKey++;
			_obj = obj;
			_handler = eh;
			_argstype = argstype;
			_Instances [_key] = this;
		}

		public void AddDelegate (Delegate d)
		{
			_handler = Delegate.Combine (_handler, d);
		}

		public void RemoveDelegate (Delegate d)
		{
			_handler = Delegate.Remove (_handler, d);
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern uint g_signal_connect_data(IntPtr obj, IntPtr name, Delegate cb, int key, IntPtr p, int flags);

		protected void Connect (string name, Delegate cb, int flags)
		{
			IntPtr native_name = Marshaller.StringToPtrGStrdup (name);
			_HandlerID = g_signal_connect_data(_obj.Handle, native_name, cb, _key, new IntPtr(0), flags);
			Marshaller.Free (native_name);
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern void g_signal_handler_disconnect (IntPtr instance, uint handler);

		[DllImport("libgobject-2.0-0.dll")]
		static extern bool g_signal_handler_is_connected (IntPtr instance, uint handler);

		protected void Disconnect ()
		{
			if (g_signal_handler_is_connected (_obj.Handle, _HandlerID))
				g_signal_handler_disconnect (_obj.Handle, _HandlerID);
		}

		public void Dispose ()
		{
			Dispose (true);
			GC.SuppressFinalize (this);
		}

		protected virtual void Dispose (bool disposing)
		{
			if (disposing) {
				_obj = null;
				_handler = null;
				_argstype = null;
			}
		}

		~SignalCallback ()
		{
			Dispose (false);
		}
	}
}

