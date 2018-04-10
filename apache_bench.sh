#!/bin/sh

. "../side-channels/scripts/cmd.sh"

ruser="root"
host="139.19.171.104"
time=60

threads=32
connections=128

cmd="mpstat -u -P ALL 1 > /local/sme/exp/mpstat_bench_webservers.out"
do_cmd $ruser $host "$cmd" 1 1

###################

cfg_str="NginX: Static HTML"
echo "$cfg_str"

cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 http://139.19.171.104:9000/hi.html

###################

cfg_str="NginX: SSL Static HTML"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 https://139.19.171.104:4444/hi.html
###################


connections=64

cfg_str="NginX: Simple PHP: Hello World!"
echo "$cfg_str"

cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 http://139.19.171.104:9000/hi.php

###################
cfg_str="NginX: SSL Simple PHP: Hello World!"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 https://139.19.171.104:4444/hi.php


###################

cfg_str="Apache: Static HTML"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 http://139.19.171.104/hi.html

###################
cfg_str="Apache: SSL Static HTML"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 https://139.19.171.104/hi.html


###################
connections=32

cfg_str="Apache: Simple PHP: Hello World!"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 http://139.19.171.104/hi.php

###################
cfg_str="Apache: SSL Simple PHP: Hello World!"
echo "$cfg_str"
cmd=" echo -e \"$cfg_str \n\" >> /local/sme/exp/mpstat_bench.out"
do_cmd $ruser $host "$cmd" 1 1

./wrk -t$threads -c$connections -d${time}s -R500000 https://139.19.171.104/hi.php


###################

cmd="pkill -9 mpstat"
do_cmd $ruser $host "$cmd"

scp $ruser@$host:/local/sme/exp/mpstat_bench_webservers.out .

cat mpstat_bench_webservers.out
