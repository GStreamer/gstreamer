// Gst.GLib.MainContext.cs - mainContext class implementation
//
// Author: Radek Doulik <rodo@matfyz.cz>
//
// Copyright (c) 2003 Radek Doulik
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

        public class MainContext {
		IntPtr handle;

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_main_context_new ();

		public MainContext ()
		{
			handle = g_main_context_new ();
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_main_context_ref (IntPtr raw);

		internal MainContext (IntPtr raw)
		{
			handle = raw;
			g_main_context_ref (handle);
		}

		internal IntPtr Handle {
			get {
				return handle;
			}
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_main_context_unref (IntPtr raw);

		~MainContext ()
		{
			g_main_context_unref (handle);
			handle = IntPtr.Zero;
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_main_context_default ();

		public static MainContext Default {
			get {
				return new MainContext (g_main_context_default ());
			}
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_main_context_thread_default ();

		public MainContext ThreadDefault {
			get {
				IntPtr raw = g_main_context_thread_default ();
				// NULL is returned if the thread-default main context is the default context. We'd rather not adopt this strange bahaviour.
				return raw == IntPtr.Zero ? Default : new MainContext (raw);
			}
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_main_context_push_thread_default (IntPtr raw);

		public void PushThreadDefault ()
		{
			g_main_context_push_thread_default (handle);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_main_context_pop_thread_default (IntPtr raw);

		public void PopThreadDefault ()
		{
			g_main_context_pop_thread_default (handle);
		}


		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool g_main_context_iteration (IntPtr raw, bool may_block);

		public bool RunIteration (bool may_block)
		{
			return g_main_context_iteration (handle, may_block);
		}

		public bool RunIteration ()
		{
			return RunIteration (false);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern bool g_main_context_pending (IntPtr raw);

		public bool HasPendingEvents
		{
			get {
				return g_main_context_pending (handle);
			}
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_main_context_wakeup (IntPtr raw);

		public void Wakeup ()
		{
			g_main_context_wakeup (handle);
		}


		public override bool Equals (object o)
		{
			if (!(o is MainContext))
				return false;

			return Handle == (o as MainContext).Handle;
		}

		public override int GetHashCode ()
		{
			return Handle.GetHashCode ();
		}


		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern int g_main_depth ();
		public static int Depth {
			get { return g_main_depth (); }
		}


		public static bool Iteration ()
		{
			return Iteration (false);
		}

		public static bool Iteration (bool may_block)
		{
			return Default.RunIteration (may_block);
		}

		public static bool Pending ()
		{
			return Default.HasPendingEvents;
		}
	}
}
