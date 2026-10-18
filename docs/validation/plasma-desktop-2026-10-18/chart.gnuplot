# Run from this directory: gnuplot chart.gnuplot
set terminal svg size 1200,740 enhanced font 'DejaVu Sans,12' background rgb '#ffffff'
set output 'overview.svg'
set multiplot layout 2,3 title "Real KDE6 / Wayland: 2560x1440 at 180 Hz; target 60 FPS\n2 x 120 s per state. Bars: means. Whiskers: range of the two run means." font ",14" margins 0.07,0.98,0.09,0.87 spacing 0.08,0.16
set boxwidth 0.55
set style fill solid 0.85 border -1
set palette defined (0 '#8793a1', 1 '#169c94', 2 '#8770c9')
set cbrange [0:2]
unset colorbox
unset key
set xrange [-0.6:2.6]
set yrange [0:*]
set xtics ('Static' 0, 'OpenGL' 1, 'Vulkan' 2)
set grid ytics lc rgb '#e5e9ef'
set border 3
set tics nomirror
set format y '%.0f'
array titles[6] = ['Plasma Shell CPU', 'KWin CPU', 'Plasma Shell PSS', 'Plasma Shell VRAM', 'Whole GPU utilization', 'Whole GPU power']
array units[6] = ['% of one logical CPU', '% of one logical CPU', 'MiB', 'MiB', '%', 'W']
do for [i=1:6] {
    c = 2+(i-1)*3
    set title titles[i]
    set ylabel units[i]
    plot 'chart.tsv' using 0:(column(c)):0 with boxes lc palette, \
         '' using 0:(column(c)):(column(c+1)):(column(c+2)) with yerrorbars lc rgb '#263340' lw 1.5 pt 7 ps 0.4
}
unset multiplot
