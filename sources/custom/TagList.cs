// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

namespace Gst
{
	using System;
	using System.Runtime.InteropServices;

	public partial class TagList
	{
		public object this [string tag, uint index] {
		 	get { return GetValueIndex (tag, index).Val; }
		}

		public object this [string tag] {
			get {
				GLib.Value v;
				bool success;

				success = CopyValue (out v, this, tag);

				if (!success)
					return null;

				object ret = (object)v.Val;
				v.Dispose ();

				return ret;
			}
		}

		public void Add (Gst.TagMergeMode mode, string tag, object value)
		{
			if (!Tag.Exists (tag))
				throw new ArgumentException (String.Format ("Invalid tag name '{0}'", tag));

			GLib.Value v = new GLib.Value (value);

			AddValue (mode, tag, v);
			v.Dispose ();
		}
	}
}