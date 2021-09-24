# 	Copyright (C) 2005 Andy Wingo
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

set terminal postscript landscape monochrome dashed "Helvetica" 14
set xlabel "Number of Forks Per Tee"
set ylabel "Seconds"
set logscale x
set title "Complex Pipeline Performance: N Forks per Tee, 1024 Elements"
plot "complexity.data" using 1:2 title "Element creation", \
     "complexity.data" using 1:3 title "State change", \
     "complexity.data" using 1:4 title "Processing 1000 buffers", \
     "complexity.data" using 1:5 title "Element destruction"
