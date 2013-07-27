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