
using System;
using System.Runtime.InteropServices;
using GLib;

namespace Gst {
	public static class Marshaller {

		public static IntPtr StringArrayToNullTermPointer (string[] strs)
		{
			if (strs == null)
				return IntPtr.Zero;

			IntPtr result = GLib.Marshaller.Malloc ((ulong) ((strs.Length + 1) * IntPtr.Size));

			for (int i = 0; i < strs.Length; i++)
				Marshal.WriteIntPtr (result, i * IntPtr.Size, GLib.Marshaller.StringToPtrGStrdup (strs [i]));

			Marshal.WriteIntPtr (result, strs.Length * IntPtr.Size, IntPtr.Zero);

			return result;
		}

		[DllImport("libglib-2.0-0.dll")]
		static extern void g_strfreev (IntPtr mem);

		public static string[] NullTermPtrToStringArray (IntPtr null_term_array, bool owned)
		{
			if (null_term_array == IntPtr.Zero)
				return new string [0];

			int count = 0;
			System.Collections.ArrayList result = new System.Collections.ArrayList ();
			IntPtr s = Marshal.ReadIntPtr (null_term_array, count++ * IntPtr.Size);
			while (s != IntPtr.Zero) {
				result.Add (GLib.Marshaller.Utf8PtrToString (s));
				s = Marshal.ReadIntPtr (null_term_array, count++ * IntPtr.Size);
			}

			if (owned)
				g_strfreev (null_term_array);

			return (string[]) result.ToArray (typeof(string));
		}
	}
}
