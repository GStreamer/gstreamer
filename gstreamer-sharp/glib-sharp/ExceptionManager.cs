// GLib.Application.cs - static Application class
//
// Authors: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2007 Novell, Inc.
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

	public delegate void UnhandledExceptionHandler (UnhandledExceptionArgs args);

	public class UnhandledExceptionArgs : System.UnhandledExceptionEventArgs {

		bool exit_app = false;

		public UnhandledExceptionArgs (Exception e, bool is_terminal) : base (e, is_terminal) {}

		public bool ExitApplication {
			get {
				return exit_app;
			}
			set {
				if (value)
					exit_app = value;
			}
		}
	}

	public class ExceptionManager {


		private ExceptionManager () {}

		public static event UnhandledExceptionHandler UnhandledException;

		public static void RaiseUnhandledException (Exception e, bool is_terminal)
		{
			if (UnhandledException == null) {
				Console.Error.WriteLine ("Exception in Gtk# callback delegate");
				Console.Error.WriteLine ("  Note: Applications can use GLib.ExceptionManager.UnhandledException to handle the exception.");
				Console.Error.WriteLine (e);
				Console.Error.WriteLine (new System.Diagnostics.StackTrace (true));
				Environment.Exit (1);
			}

			UnhandledExceptionArgs args = new UnhandledExceptionArgs (e, is_terminal);
			try {
				UnhandledException (args);
			} catch (Exception ex) {
				Console.Error.WriteLine (ex);
				Environment.Exit (1);
			}

			if (is_terminal || args.ExitApplication)
				Environment.Exit (1);
		}
	}
}
