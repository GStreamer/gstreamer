//
// Element.cs
//
// Authors:
//   Khaled Mohammed <khaled.mohammed@gmail.com>
//   Sebastian Dröge <sebastian.droege@collabora.co.uk>
//   Stephan Sundermann <stephansundermann@gmail.com>
//
// Copyright (C) 2006 Khaled Mohammed
// Copyright (C) 2006 Novell, Inc.
// Copyright (C) 2009 Sebastian Dröge
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

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	partial class Element 
	{
		public static bool Link (params Element [] elements) {
			for (int i = 0; i < elements.Length - 1; i++) {
				if (!elements[i].Link (elements[i+1]))
					return false;
			}
			return true;
		}

		public static void Unlink (params Element [] elements) {
			for (int i = 0; i < elements.Length - 1; i++) {
				elements[i].Unlink (elements[i+1]);
			}
		}
	}
}