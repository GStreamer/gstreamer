//
// Copyright (c) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
//
// This class implements some helper functions to handle GError
//

using System;
using System.Runtime.InteropServices;

namespace Gst {
    internal static class GError
    {
        public static string GetMessage (IntPtr error)
	{
	    if (error == IntPtr.Zero)
	        return String.Empty;

	    IntPtr message = gstsharp_g_error_get_message (error);
	    if (message == IntPtr.Zero)
	      return String.Empty;
	      
	    return GLib.Marshaller.PtrToStringGFree (message);
	}

	public static void Free (IntPtr error)
	{
	    if (error != IntPtr.Zero)
	        g_error_free (error);
	}

	[DllImport("gstreamersharpglue-0.10")]
	static extern IntPtr gstsharp_g_error_get_message (IntPtr error);

	[DllImport("glib-2.0.dll")]
	static extern void g_error_free (IntPtr error);
    }
}
