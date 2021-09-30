// Iterator.cs - Custom caps wrapper for IEnumerable
//
// Authors:
//     Sebastian Dröge <slomo@circular-chaos.org>
//     Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (c) 2013 Stephan Sundermann
// Copyright (c) 2009 Sebastian Dröge
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
	using System;
	using System.Collections;
	using System.Runtime.InteropServices;

	partial class Caps : IEnumerable {
		public Structure this[uint index] {
			get {
				if (index >= Size)
					throw new ArgumentOutOfRangeException();

				Structure structure = GetStructure((uint)index);
				return structure;
			}
		}

		private class StructureEnumerator : IEnumerator {
			Gst.Caps caps;
			long index;

			public StructureEnumerator(Gst.Caps caps) {
				this.caps = caps;
				index = -1;
			}

			public object Current {
				get {
					if (index >= caps.Size)
						throw new ArgumentOutOfRangeException();
					if (index == -1)
						throw new ArgumentException();

					return caps[(uint)index];
				}
			}

			public bool MoveNext() {
				index += 1;
				return (index < caps.Size);
			}

			public void Reset() {
				index = -1;
			}
		}

		public IEnumerator GetEnumerator() {
			return new StructureEnumerator(this);
		}
	}
}