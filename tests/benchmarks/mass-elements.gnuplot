set terminal postscript landscape monochrome dashed "Helvetica" 14
set xlabel "Number of Identity Elements"
set ylabel "Seconds"
set title "Mass Pipeline Performance: fakesrc ! N * identity ! fakesink"
plot "mass_elements.data" using 1:2 title "Element creation", \
     "mass_elements.data" using 1:3 title "State change", \
     "mass_elements.data" using 1:4 title "Processing 1000 buffers", \
     "mass_elements.data" using 1:5 title "Element destruction"
