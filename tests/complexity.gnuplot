set terminal postscript landscape monochrome dashed "Helvetica" 14
set xlabel "Number of Forks Per Tee"
set ylabel "Seconds"
set logscale x
set title "Complex Pipeline Performance: N Forks per Tee, 1024 Elements"
plot "complexity.data" using 1:2 title "Element creation", \
     "complexity.data" using 1:3 title "State change", \
     "complexity.data" using 1:4 title "Processing 1000 buffers", \
     "complexity.data" using 1:5 title "Element destruction"
