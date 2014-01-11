// Copyright (C) 2013  Bertrand Lorentz <bertrand.lorentz@gmail.com>
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

using System;

namespace Gst
{
	partial class Bus
	{
		public uint AddWatch (Gst.BusFunc func) {
			// https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#G-PRIORITY-DEFAULT:CAPS
			int G_PRIORITY_DEFAULT = 0;
			return AddWatchFull (G_PRIORITY_DEFAULT, func);
		}
	}
}

