// GLib.TypeFundamentals.cs : Standard Types enumeration 
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001 Mike Kestner
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

	public enum TypeFundamentals {
		TypeInvalid	= 0 << 2,
		TypeNone	= 1 << 2,
		TypeInterface	= 2 << 2,
		TypeChar	= 3 << 2,
		TypeUChar	= 4 << 2,
		TypeBoolean	= 5 << 2,
		TypeInt		= 6 << 2,
		TypeUInt	= 7 << 2,
		TypeLong	= 8 << 2,
		TypeULong	= 9 << 2,
		TypeInt64	= 10 << 2,
		TypeUInt64	= 11 << 2,
		TypeEnum	= 12 << 2,
		TypeFlags	= 13 << 2,
		TypeFloat	= 14 << 2,
		TypeDouble	= 15 << 2,
		TypeString	= 16 << 2,
		TypePointer	= 17 << 2,
		TypeBoxed	= 18 << 2,
		TypeParam	= 19 << 2,
		TypeObject	= 20 << 2,
	}
}
