//
// Authors:
//     Andrés G. Aragoneses <knocte@gmail.com>
//     Sebastian Dröge <slomo@circular-chaos.org>
//     Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (c) 2009 Sebastian Dröge
// Copyright (c) 2013 Andrés G. Aragoneses
// Copyright (C) 2013 Stephan Sundermann
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301  USA

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Gst {

	public class PropertyNotFoundException : Exception { }

	[StructLayout(LayoutKind.Sequential)]
	struct GstObject {
		IntPtr _lock;
		public string name;
		public Object parent;
		public uint flags;
		IntPtr controlBindings;
		public int control_rate;
		public int last_sync;

		private IntPtr[] _gstGstReserved;
	}

	partial class Object {
		private Dictionary<string, bool> PropertyNameCache = new Dictionary<string, bool>();

		[DllImport("gobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_object_class_find_property(IntPtr klass, IntPtr name);

		bool PropertyExists(string name) {
			if (PropertyNameCache.ContainsKey(name))
				return PropertyNameCache[name];

			IntPtr native_name = GLib.Marshaller.StringToPtrGStrdup(name);
			var ptr = g_object_class_find_property(Marshal.ReadIntPtr(Handle), native_name);
			var result = ptr != IntPtr.Zero;

			// just cache the positive results because there might
			// actually be new properties getting installed
			if (result)
				PropertyNameCache[name] = result;

			GLib.Marshaller.Free(native_name);
			return result;
		}

		public object this[string property] {
			get {
				if (PropertyExists(property)) {
					using (GLib.Value v = GetProperty(property)) {
						return v.Val;
					}
				}
				else
					throw new PropertyNotFoundException();
			}
			set {
				if (PropertyExists(property)) {
					if (value == null) {
						throw new ArgumentNullException();
					}
					var type = value.GetType();
					var gtype = (GLib.GType)type;
					if (gtype == null) {
						throw new Exception("Could not find a GType for type " + type.FullName);
					}
					GLib.Value v = new GLib.Value((GLib.GType)value.GetType());
					v.Val = value;
					SetProperty(property, v);
					v.Dispose();
				}
				else
					throw new PropertyNotFoundException();
			}
		}

		public void Connect(string signal, SignalHandler handler) {
			DynamicSignal.Connect(this, signal, handler);
		}

		public void Disconnect(string signal, SignalHandler handler) {
			DynamicSignal.Disconnect(this, signal, handler);
		}

		public void Connect(string signal, Delegate handler) {
			DynamicSignal.Connect(this, signal, handler);
		}

		public void Disconnect(string signal, Delegate handler) {
			DynamicSignal.Disconnect(this, signal, handler);
		}

		public object Emit(string signal, params object[] parameters) {
			return DynamicSignal.Emit(this, signal, parameters);
		}
	}
}