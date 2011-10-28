#!/bin/bash

prev_list=`python ~/xia-core/click-2.0/conf/geni/stats/read_stats.py`
arr=()
for x in $prev_list
do
  arr=("${arr[@]}" $x)
done
 
prev_total=${arr[0]}
prev_sid=${arr[1]}
prev_cid=${arr[2]}
prev_hid=${arr[3]}

while true;
do
        sleep 1

		cur_list=`python ~/xia-core/click-2.0/conf/geni/stats/read_stats.py`
		arr=()
		for x in $cur_list
		do
			arr=("${arr[@]}" $x)
		done
 
		cur_total=${arr[0]}
		cur_sid=${arr[1]}
		cur_cid=${arr[2]}
		cur_hid=${arr[3]}
		
		
		total=`expr $cur_total - $prev_total`
		cid=`expr $cur_cid - $prev_cid`
		sid=`expr $cur_sid - $prev_sid`
		hid=`expr $cur_hid - $prev_hid`
		
        prev_total=$cur_total
		prev_sid=$cur_sid
		prev_cid=$cur_cid
		prev_hid=$cur_hid

        echo $total $cid $sid $hid 
done;