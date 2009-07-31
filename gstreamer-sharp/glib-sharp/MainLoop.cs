// GLib.MainLoop.cs - g_main_loop class implementation
//
// Author: Jeroen Zwartepoorte <jeroen@xs4all.nl>
//
// Copyright (c) 2004 Jeroen Zwartepoorte
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

using System;
using System.Runtime.InteropServices;

namespace GLib {
	public class MainLoop {
		private IntPtr handle;
	
		[DllImport("libglib-2.0-0.dll")]
		static extern IntPtr g_main_loop_new (IntPtr context, bool isRunning);

		public MainLoop ()
		{
			handle = g_main_loop_new (IntPtr.Zero, false);
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern void g_main_loop_unref (IntPtr loop);

		~MainLoop ()
		{
			g_main_loop_unref (handle);
			handle = IntPtr.Zero;
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern bool g_main_loop_is_running (IntPtr loop);

		public bool IsRunning {
			get {
				return g_main_loop_is_running (handle);
			}
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_main_loop_run (IntPtr loop);

		public void Run ()
		{
			g_main_loop_run (handle);
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_main_loop_quit (IntPtr loop);

		public void Quit ()
		{
			g_main_loop_quit (handle);
		}
	}
}

