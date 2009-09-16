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

namespace Gst.GLib {

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

		/* Gst.GLib log levels */
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

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate void LogFuncNative (IntPtr log_domain, LogLevelFlags flags, IntPtr message, IntPtr user_data);

		static LogFuncNative native_handler;

		static void NativeCallback (IntPtr log_domain_native, LogLevelFlags flags, IntPtr message_native, IntPtr user_data)
		{
			if (user_data == IntPtr.Zero)
				return;
			string log_domain = Marshaller.Utf8PtrToString (log_domain_native);
			string message = Marshaller.Utf8PtrToString (message_native);
			GCHandle gch = (GCHandle) user_data;
			LogFunc func = gch.Target as LogFunc;
			if (func != null)
				func (log_domain, flags, message);
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate void PrintFuncNative (IntPtr message);

		class PrintHelper {

			PrintFuncNative native;
			PrintFunc managed;

			public PrintHelper (PrintFuncNative native)
			{
				this.native = native;
			}

			public PrintHelper (PrintFunc managed)
			{
				this.managed = managed;
				GCHandle.Alloc (this);
			}

			void Callback (IntPtr nmessage)
			{
				string message = Marshaller.Utf8PtrToString (nmessage);
				managed (message);
			}

			void Invoke (string message)
			{
				IntPtr nmessage = Marshaller.StringToPtrGStrdup (message);
				native (nmessage);
				Marshaller.Free (nmessage);
			}

			public PrintFuncNative Handler {
				get { return new PrintFuncNative (Callback); }
			}

			public PrintFunc Invoker {
				get { return new PrintFunc (Invoke); }
			}
		}

		static System.Collections.Generic.Dictionary<uint, GCHandle> handlers;

		static void EnsureHash ()
		{
			if (handlers == null)
				handlers = new System.Collections.Generic.Dictionary<uint, GCHandle> ();
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_logv (IntPtr log_domain, LogLevelFlags flags, IntPtr message);
		
		public void WriteLog (string logDomain, LogLevelFlags flags, string format, params object [] args)
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			IntPtr nmessage = Marshaller.StringToPtrGStrdup (String.Format (format, args));
			g_logv (ndom, flags, nmessage);
			Marshaller.Free (ndom);
			Marshaller.Free (nmessage);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern uint g_log_set_handler (IntPtr log_domain, LogLevelFlags flags, LogFuncNative log_func, IntPtr user_data);
		
		public static uint SetLogHandler (string logDomain, LogLevelFlags flags, LogFunc logFunc)
		{
			if (native_handler == null)
				native_handler = new LogFuncNative (NativeCallback);

			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			GCHandle gch = GCHandle.Alloc (logFunc);
			uint result = g_log_set_handler (ndom, flags, native_handler, (IntPtr) gch);
			Marshaller.Free (ndom);
			EnsureHash ();
			handlers [result] = gch;
			return result;
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern uint g_log_remove_handler (IntPtr log_domain, uint handler_id);

		public static void RemoveLogHandler (string logDomain, uint handlerID)
		{
			if (handlers != null && handlers.ContainsKey (handlerID)) {
				handlers [handlerID].Free ();
				handlers.Remove (handlerID);
			}
			
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			g_log_remove_handler (ndom, handlerID);
			Marshaller.Free (ndom);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern PrintFuncNative g_set_print_handler (PrintFuncNative handler);

		public static PrintFunc SetPrintHandler (PrintFunc handler)
		{
			PrintHelper helper = new PrintHelper (handler);
			PrintFuncNative prev = g_set_print_handler (helper.Handler);
			helper = new PrintHelper (prev);
			return helper.Invoker;
		}
		
		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern PrintFuncNative g_set_printerr_handler (PrintFuncNative handler);

		public static PrintFunc SetPrintErrorHandler (PrintFunc handler)
		{
			PrintHelper helper = new PrintHelper (handler);
			PrintFuncNative prev = g_set_printerr_handler (helper.Handler);
			helper = new PrintHelper (prev);
			return helper.Invoker;
		}
		
		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern void g_log_default_handler (IntPtr log_domain, LogLevelFlags log_level, IntPtr message, IntPtr unused_data);

		public static void DefaultHandler (string logDomain, LogLevelFlags logLevel, string message)
						   
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			IntPtr nmess = Marshaller.StringToPtrGStrdup (message);
			g_log_default_handler (ndom, logLevel, nmess, IntPtr.Zero);
			Marshaller.Free (ndom);
			Marshaller.Free (nmess);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		extern static LogLevelFlags g_log_set_always_fatal (LogLevelFlags fatal_mask);
		
		public static LogLevelFlags SetAlwaysFatal (LogLevelFlags fatalMask)
		{
			return g_log_set_always_fatal (fatalMask);
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		extern static LogLevelFlags g_log_set_fatal_mask (IntPtr log_domain, LogLevelFlags fatal_mask);
		
		public static LogLevelFlags SetAlwaysFatal (string logDomain, LogLevelFlags fatalMask)
		{
			IntPtr ndom = Marshaller.StringToPtrGStrdup (logDomain);
			LogLevelFlags result = g_log_set_fatal_mask (ndom, fatalMask);
			Marshaller.Free (ndom);
			return result;
		}

		class Invoker {

			LogFuncNative native;

			public Invoker (LogFuncNative native)
			{
				this.native = native;
			}

			void Invoke (string log_domain, LogLevelFlags flags, string message)
			{
				IntPtr ndom = Marshaller.StringToPtrGStrdup (log_domain);
				IntPtr nmess = Marshaller.StringToPtrGStrdup (message);
				native (ndom, flags, nmess, IntPtr.Zero);
				Marshaller.Free (ndom);
				Marshaller.Free (nmess);
			}

			public LogFunc Handler {
				get { return new LogFunc (Invoke); }
			}
		}

		[DllImport ("libglib-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		extern static LogFuncNative g_log_set_default_handler (LogFuncNative log_func, IntPtr user_data);
		
		public static LogFunc SetDefaultHandler (LogFunc log_func)
		{
			if (native_handler == null)
				native_handler = new LogFuncNative (NativeCallback);

			LogFuncNative prev = g_log_set_default_handler (native_handler, (IntPtr) GCHandle.Alloc (log_func));
			if (prev == null)
				return null;
			Invoker invoker = new Invoker (prev);
			return invoker.Handler;
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

