// GLib.Timeout.cs - Timeout class implementation
//
// Author(s):
//	Mike Kestner <mkestner@speakeasy.net>
//	Stephane Delcroix <stephane@delcroix.org>
//
// Copyright (c) 2002 Mike Kestner
// Copyright (c) 2009 Novell, Inc.
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

	public delegate bool TimeoutHandler ();

	public class Timeout {

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate bool TimeoutHandlerInternal ();

		internal class TimeoutProxy : SourceProxy {
			public TimeoutProxy (TimeoutHandler real)
			{
				real_handler = real;
				proxy_handler = new TimeoutHandlerInternal (Handler);
			}

			public bool Handler ()
			{
				try {
					TimeoutHandler timeout_handler = (TimeoutHandler) real_handler;

					bool cont = timeout_handler ();
					if (!cont)
						Remove ();
					return cont;
				} catch (Exception e) {
					ExceptionManager.RaiseUnhandledException (e, false);
				}
				return false;
			}
		}
		
		private Timeout () {} 
		[DllImport("libglib-2.0-0.dll")]
		static extern uint g_timeout_add (uint interval, TimeoutHandlerInternal d, IntPtr data);

		public static uint Add (uint interval, TimeoutHandler hndlr)
		{
			TimeoutProxy p = new TimeoutProxy (hndlr);

			p.ID = g_timeout_add (interval, (TimeoutHandlerInternal) p.proxy_handler, IntPtr.Zero);
			lock (Source.source_handlers)
				Source.source_handlers [p.ID] = p;

			return p.ID;
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern uint g_timeout_add_full (int priority, uint interval, TimeoutHandlerInternal d, IntPtr data, DestroyNotify notify);

		public static uint Add (uint interval, TimeoutHandler hndlr, Priority priority)
		{
			TimeoutProxy p = new TimeoutProxy (hndlr);

			p.ID = g_timeout_add_full ((int)priority, interval, (TimeoutHandlerInternal) p.proxy_handler, IntPtr.Zero, null);
			lock (Source.source_handlers)
				Source.source_handlers [p.ID] = p;

			return p.ID;
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern uint g_timeout_add_seconds (uint interval, TimeoutHandlerInternal d, IntPtr data);

		public static uint AddSeconds (uint interval, TimeoutHandler hndlr)
		{
			TimeoutProxy p = new TimeoutProxy (hndlr);

			p.ID = g_timeout_add_seconds (interval, (TimeoutHandlerInternal) p.proxy_handler, IntPtr.Zero);
			lock (Source.source_handlers)
				Source.source_handlers [p.ID] = p;

			return p.ID;
		}

		public static void Remove (uint id)
		{
			Source.Remove (id);
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern bool g_source_remove_by_funcs_user_data (Delegate d, IntPtr data);
                                                                                
		public static bool Remove (TimeoutHandler hndlr)
		{
			bool result = false;
			ArrayList keys = new ArrayList ();

			lock (Source.source_handlers) {
				foreach (uint code in Source.source_handlers.Keys) {
					TimeoutProxy p = Source.source_handlers [code] as TimeoutProxy;
				
					if (p != null && p.real_handler == hndlr) {
						keys.Add (code);
						result = g_source_remove_by_funcs_user_data (p.proxy_handler, IntPtr.Zero);
					}
				}

				foreach (object key in keys)
					Source.source_handlers.Remove (key);
			}

			return result;
		}
	}
}

