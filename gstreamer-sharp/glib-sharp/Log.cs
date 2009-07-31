// Log.cs - Wrapper for message logging functions
//
// Authors:
//	Gonzalo Paniagua Javier (gonzalo@ximian.com)
//	
//
// Copyright (c) 2002 Gonzalo Paniagua
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

//

namespace GLib {

	using System;
	using System.Collections;
	using System.Runtime.InteropServices;

	public delegate void LogFunc (string log_domain, LogLevelFlags log_level, string message);

	public delegate void PrintFunc (string message);

	[Flags]
	public enum LogLevelFlags : int
	{
		/* log flags */
		FlagRecursion          = 1 << 0,
		FlagFatal              = 1 << 1,

		/* GLib log levels */
		Error                  = 1 << 2,       /* always fatal */
		Critical               = 1 << 3,
		Warning                = 1 << 4,
		Message                = 1 << 5,
		Info                   = 1 << 6,
		Debug                  = 1 << 7,

		/* Convenience values */
		AllButFatal            = 253,
		AllButRecursion        = 254,
		All                    = 255,

		FlagMask               = 3,
		LevelMask              = unchecked ((int) 0xFFFFFFFC)
	}

	public class Log {

		static Hashtable handlers;

		static void EnsureHash ()
		{
			if (handlers == null)
				handlers = new Hashtable ();
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_logv (IntPtr log_domain, LogLevelFlags flags, IntPtr message);
		
		public void WriteLog (string logDomain, LogLevelFlags flags, string format, params object [] args)
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			IntPtr nmessage = Marshaller.StringToPtrGStrdup (String.Format (format, args));
			g_logv (ndom, flags, nmessage);
			Marshaller.Free (ndom);
			Marshaller.Free (nmessage);
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern uint g_log_set_handler (IntPtr log_domain, LogLevelFlags flags, LogFunc log_func, IntPtr user_data);
		
		public static uint SetLogHandler (string logDomain, LogLevelFlags flags, LogFunc logFunc)
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			uint result = g_log_set_handler (ndom, flags, logFunc, IntPtr.Zero);
			Marshaller.Free (ndom);
			EnsureHash ();
			handlers [result] = logFunc;

			return result;
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern uint g_log_remove_handler (IntPtr log_domain, uint handler_id);

		public static void RemoveLogHandler (string logDomain, uint handlerID)
		{
			if (handlers != null && handlers.ContainsKey (handlerID))
				handlers.Remove (handlerID);
			
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			g_log_remove_handler (ndom, handlerID);
			Marshaller.Free (ndom);
		}


		[DllImport("libglib-2.0-0.dll")]
		static extern PrintFunc g_set_print_handler (PrintFunc handler);

		public static PrintFunc SetPrintHandler (PrintFunc handler)
		{
			EnsureHash ();
			handlers ["PrintHandler"] = handler;

			return g_set_print_handler (handler);
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern PrintFunc g_set_printerr_handler (PrintFunc handler);

		public static PrintFunc SetPrintErrorHandler (PrintFunc handler)
		{
			EnsureHash ();
			handlers ["PrintErrorHandler"] = handler;

			return g_set_printerr_handler (handler);
		}
		
		[DllImport("libglib-2.0-0.dll")]
		static extern void g_log_default_handler (IntPtr log_domain, LogLevelFlags log_level, IntPtr message, IntPtr unused_data);

		public static void DefaultHandler (string logDomain, LogLevelFlags logLevel, string message)
						   
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			IntPtr nmess = Marshaller.StringToPtrGStrdup (message);
			g_log_default_handler (ndom, logLevel, nmess, IntPtr.Zero);
			Marshaller.Free (ndom);
			Marshaller.Free (nmess);
		}

		[DllImport("libglib-2.0-0.dll")]
		extern static LogLevelFlags g_log_set_always_fatal (LogLevelFlags fatal_mask);
		
		public static LogLevelFlags SetAlwaysFatal (LogLevelFlags fatalMask)
		{
			return g_log_set_always_fatal (fatalMask);
		}

		[DllImport("libglib-2.0-0.dll")]
		extern static LogLevelFlags g_log_set_fatal_mask (IntPtr log_domain, LogLevelFlags fatal_mask);
		
		public static LogLevelFlags SetAlwaysFatal (string logDomain, LogLevelFlags fatalMask)
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			LogLevelFlags result = g_log_set_fatal_mask (ndom, fatalMask);
			Marshaller.Free (ndom);
			return result;
		}

		/*
		 * Some common logging methods.
		 *
		 * Sample usage:
		 *
		 *	// Print the messages for the NULL domain
		 *	LogFunc logFunc = new LogFunc (Log.PrintLogFunction);
		 *	Log.SetLogHandler (null, LogLevelFlags.All, logFunc);
		 *
		 *	// Print messages and stack trace for Gtk critical messages
		 *	logFunc = new LogFunc (Log.PrintTraceLogFunction);
		 *	Log.SetLogHandler ("Gtk", LogLevelFlags.Critical, logFunc);
		 *
		 */

		public static void PrintLogFunction (string domain, LogLevelFlags level, string message)
		{
			Console.WriteLine ("Domain: '{0}' Level: {1}", domain, level);
			Console.WriteLine ("Message: {0}", message);
		}

		public static void PrintTraceLogFunction (string domain, LogLevelFlags level, string message)
		{
			PrintLogFunction (domain, level, message);
			Console.WriteLine ("Trace follows:\n{0}", new System.Diagnostics.StackTrace ());
		}
	}
}

