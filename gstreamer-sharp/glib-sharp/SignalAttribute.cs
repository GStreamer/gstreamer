// SignalAttribute.cs
//
// Author:
//   Ricardo Fernández Pascual <ric@users.sourceforge.net>
//
// Copyright (c) Ricardo Fernández Pascual <ric@users.sourceforge.net>
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

	[Serializable]
	[AttributeUsage (AttributeTargets.Event, Inherited=false)]
	public sealed class SignalAttribute : Attribute 
	{
		private string cname;

		public SignalAttribute (string cname)
		{
			this.cname = cname;
		}

		private SignalAttribute () {}

		public string CName 
		{
			get {
				return cname;
			}
		}
	}
}
