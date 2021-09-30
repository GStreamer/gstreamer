// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
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

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	public partial class TagList {
		public object this[string tag, uint index] {
			get { return GetValueIndex(tag, index).Val; }
		}

		public object this[string tag] {
			get {
				var v = GLib.Value.Empty;
				bool success;

				success = CopyValue(ref v, this, tag);

				if (!success)
					return null;

				object ret = (object)v.Val;
				v.Dispose();

				return ret;
			}
		}

		public string[] Tags {
			get {
				int size = NTags();
				string[] tags = new string[size];
				for (uint i = 0; i < size; i++)
					tags[i] = NthTagName(i);

				return tags;
			}
		}

		public void Add(Gst.TagMergeMode mode, string tag, object value) {
			if (!Tag.Exists(tag))
				throw new ArgumentException(String.Format("Invalid tag name '{0}'", tag));

			GLib.Value v = new GLib.Value(value);

			AddValue(mode, tag, v);
			v.Dispose();
		}
	}
}