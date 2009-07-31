// PropertyAttribute.cs
//
// Copyright (c) 2004 Novell, Inc.
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

	public sealed class PropertyAttribute : Attribute {

		string blurb;
		string nickname;
		string name;

		public PropertyAttribute (string name)
		{
			this.name = name;
		}

		public PropertyAttribute (string name, string nickname, string blurb)
		{
			this.name = name;
			this.nickname = nickname;
			this.blurb = blurb;
		}

		public string Blurb {
			get {
				return blurb;
			}
			set {
				blurb = value;
			}
		}

		public string Name {
			get {
				return name;
			}
			set {
				name = value;
			}
		}

		public string Nickname {
			get {
				return nickname;
			}
			set {
				nickname = value;
			}
		}
	}
}
