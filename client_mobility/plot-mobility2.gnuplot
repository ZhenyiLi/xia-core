#!/usr/bin/env gnuplot

set terminal pdf enhanced dashed size 3,2
set output "mobility-req2.pdf"

set ytics nomirror
set xtics nomirror

#set y2tics autofreq
#set my2tics
set noy2tics
set nomy2tics
set xtics 50

set xlabel "Elapsed time (milliseconds)"
#set ylabel "Processing rate (qps)"
set ylabel "Sequence # (K)"

set ytics 0.05 
set nokey
#set key outside center bottom box
#set key outside center bottom

################## begin
#set xrange [800.000000:1600.000000]
#offset = 813.960075
#server_update = 1012.556076
#client_update = 1000.000000

#set xrange [0.000000:250.000000]
#offset = 1713.960075
#server_update = 112.556076
#client_update = 100.000000

offset = 1579.738140
server_update = 513.319969
client_update = 500.000000

################## end

offset = offset + 300
server_update = server_update - 300
client_update = client_update - 300
xmin= 0
xmax= 430
ymin = 1.84
ymax = 2.04
rtt = 27	# the actual RTT was somewat longer than intended (25 ms)

##################

set xrange [xmin:xmax]
set yrange [ymin:ymax]

set label "(1) 3G->3G" left at 275, 1.864 font "Helvetica,5" 
set label "(2) 3G->3G (lost)" left at 275, 1.913 font "Helvetica,5"
set label "(3) 3G->WiFi" left at 275, 1.976 font "Helvetica,5"
set label "(4) WiFi->WiFi" left at 275, 2.020 font "Helvetica,5"


set object 1 rect from xmin,1.881 to xmax,1.951 lw 0 fc rgb "gold" 
#set style fill pattern 2 noborder 
#set object 2 rect from xmin,1.951 to xmax,2.0 linecolor rgb "grey" fc rgb "white"
set style line 1000 linewidth 3 linetype 1 linecolor rgb "grey" 
set arrow from xmin,2.0 to xmax,2.0 nohead linestyle 1000
set style line 1000 linewidth 3 linetype 1 linecolor rgb "grey" 
set arrow from xmin,1.951 to xmax,1.951 nohead linestyle 1000
set arrow from xmin,1.881 to xmax,1.881 nohead linestyle 1000

# vertical lines
#set arrow from freeze,ymin to freeze,ymax nohead linestyle 1000

set style line 1001 linewidth 4 linetype 3 linecolor rgb "#6a5acd"
set arrow from client_update,ymin to client_update,ymax nohead linestyle 1001

set style line 1002 linewidth 4 linetype 1 linecolor rgb "#00ff00"
set arrow from server_update,ymin to server_update,ymax nohead linestyle 1002


# horizontal labels


set style line 100 linewidth 2 linetype 1 linecolor rgb "#000000" 
#set arrow from freeze-rtt/2, (ymin + (ymax - ymin) * 0.85) to client_update, (ymin + (ymax - ymin) * 0.85) heads linestyle 100
#set label '(1000 ms)' center at (freeze + server_update) / 2, (ymin + (ymax - ymin) * 0.80)		# XXX: HARDCODED

set style line 102 linewidth 2 linetype 1 linecolor rgb "#000000" 
set label "Client rebind\n(3G->WiFi)" right at server_update-75, (ymin + (ymax - ymin) * 0.93) font "Helvetica Bold,5"
set arrow from server_update - 64, (ymin + (ymax - ymin) * 0.92) to client_update, (ymin + (ymax - ymin) * 0.92) head linestyle 102

set style line 101 linewidth 2 linetype 1 linecolor rgb "#000000" 
set label 'Service rebind' right at server_update-70, (ymin + (ymax - ymin) * 0.70) font "Helvetica Bold,5"
set arrow from server_update - 60, (ymin + (ymax - ymin) * 0.70) to server_update-2, (ymin + (ymax - ymin) * 0.70) head linestyle 101

set style arrow 8 heads size screen 0.008,90 ls 2
set style line 10000 linewidth 5 linetype 1 linecolor rgb "#000000"
# left RTT
set arrow from client_update-150,1.89  to client_update,1.89 heads arrowstyle 8 linestyle 10000 
set label ' 1 RTT (3G) ' right at  client_update-10,1.900 font "Helvetica,5"
#set arrow from freeze-rtt/2, (ymin + (ymax - ymin) * 0.35) to freeze, (ymin + (ymax - ymin) * 0.35) heads arrowstyle 8 linestyle 10000
#set label ' 0.5 RTT1' left at freeze-rtt/2, (ymin + (ymax - ymin) * 0.28)
#set label ' (in-flight packets)' left at freeze-rtt/2, (ymin + (ymax - ymin) * 0.20)

#set arrow from server_update, (ymin + (ymax - ymin) * 0.37) to server_update + rtt, (ymin + (ymax - ymin) * 0.37) heads arrowstyle 8 linestyle 10000
#set arrow from client_update, (ymin + (ymax - ymin) * 0.32) to client_update + rtt, (ymin + (ymax - ymin) * 0.32) heads arrowstyle 8 linestyle 10000
#set label ' 0.5 RTT2' left at client_update + rtt, (ymin + (ymax - ymin) * 0.35)
##set label '(27 ms)' left at client_update + rtt, (ymin + (ymax - ymin) * 0.27)

set label 'Req sent' font ",5.7" left at 17, (ymin + (ymax - ymin) * 0.442) textcolor rgb "blue"
set label 'Req recv' font ",5.7" left at 77, (ymin + (ymax - ymin) * 0.095) textcolor rgb "violet"

# right RTT
set arrow from client_update, 1.94 to client_update + rtt, 1.94 heads arrowstyle 8 linestyle 10000
set label ' 1 RTT (WiFi)' left at server_update + 12, 1.94 font "Helvetica,5"
#set arrow from server_update + 20, 1.90 to client_update + rtt*3/4, 1.94 linestyle 101

plot "plot.dat" \
            index 1 using ($1 * 1000 - offset):($2 / 1000) axes x1y1 with points ls 7 ps 0.20 lt 3 title "Response sent by service", \
     ""     index 4 using ($1 * 1000 - offset):($2 / 1000) axes x1y1 with points ls 7 ps 0.20 lt 4 title "Response received by client"

#            index 0 using ($1 * 1000 - 14500):($2) axes x1y1 with lines linewidth 8 lt 1 title "250 ms moving average", \


# ===============================================

set nomultiplot

