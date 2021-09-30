// Iterator.cs - Custom iterator wrapper for IEnumerable
//
// Authors:
//     Maarten Bosmans <mkbosmans@gmail.com>
//     Sebastian Dröge <slomo@circular-chaos.org>
//     Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (c) 2009 Maarten Bosmans
// Copyright (c) 2009 Sebastian Dröge
// Copyright (c) 2013 Stephan Sundermann
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

namespace Gst {

	using GLib;
	using System;
	using System.Collections;
	using System.Collections.Generic;
	using System.Runtime.InteropServices;

	public partial class Iterator : IEnumerable {

		[DllImport("gobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_value_reset(ref GLib.Value val);

		[DllImport("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern int gst_iterator_next(IntPtr raw, ref GLib.Value elem);

		public Gst.IteratorResult Next(ref GLib.Value elem) {
			int raw_ret = gst_iterator_next(Handle, ref elem);
			Gst.IteratorResult ret = (Gst.IteratorResult)raw_ret;
			return ret;
		}

		private class Enumerator : IEnumerator {
			Iterator iterator;
			Hashtable seen = new Hashtable();

			private object current = null;
			public object Current {
				get {
					return current;
				}
			}

			public bool MoveNext() {
				IntPtr raw_ret;
				bool retry = false;

				if (iterator.Handle == IntPtr.Zero)
					return false;

				do {
					GLib.Value value = new GLib.Value(GLib.GType.Boolean);
					value.Dispose();

					IteratorResult ret = iterator.Next(ref value);

					switch (ret) {
						case IteratorResult.Done:
							return false;
						case IteratorResult.Ok:
							if (seen.Contains(value)) {
								retry = true;
								break;
							}
							seen.Add(value, null);
							current = value.Val;
							return true;
						case IteratorResult.Resync:
							iterator.Resync();
							retry = true;
							break;
						default:
						case IteratorResult.Error:
							throw new Exception("Error while iterating");
					}
				} while (retry);

				return false;
			}

			public void Reset() {
				seen.Clear();
				if (iterator.Handle != IntPtr.Zero)
					iterator.Resync();
			}

			public Enumerator(Iterator iterator) {
				this.iterator = iterator;
			}
		}

		private Enumerator enumerator = null;

		public IEnumerator GetEnumerator() {
			if (this.enumerator == null)
				this.enumerator = new Enumerator(this);
			return this.enumerator;
		}
	}
}