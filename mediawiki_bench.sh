#!/bin/sh

. "../side-channels/scripts/cmd.sh"

ruser="root"
host="139.19.171.104"

cmd="mpstat -u -P ALL 1 > /local/sme/exp/mpstat_bench_mediawiki.out"
do_cmd $ruser $host "$cmd" 1 1
time=130
rate=1450
flags="--latency --u_latency "
threads=32
connections=32

cfg_str="Apache: SSL, Mediawiki trace"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -R${rate}  $flags  -H 'accept-encoding: gzip, deflate, br' -s \
multiple_url_paths.lua https://139.19.171.104


###################
cfg_str="Apache: SSL, Mediawiki Small page"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -H 'accept-encoding: gzip, deflate, br' \
-R${rate}  $flags  https://139.19.171.104/mediawiki/index.php?title=Mecsplicateur

###################
cfg_str="Apache: SSL, Mediawiki Large page"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -H 'accept-encoding: gzip, deflate, br' \
-R${rate}  $flags  https://139.19.171.104/mediawiki/index.php?tiitle=Water


###################
cfg_str="NginX: SSL, Mediawiki trace"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -R${rate}  $flags  -H 'accept-encoding: gzip, deflate, br' -s \
multiple_url_paths.lua https://139.19.171.104:4444


###################
cfg_str="NginX: SSL, Mediawiki Small page"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -H 'accept-encoding: gzip, deflate, br' \
-R${rate}  $flags  https://139.19.171.104:4444/mediawiki/index.php?title=Mecsplicateur

###################
cfg_str="NginX: SSL, Mediawiki Large page"
echo "$cfg_str"

./wrk -t$threads -c$connections -d${time}s -H 'accept-encoding: gzip, deflate, br' \
-R${rate}  $flags  https://139.19.171.104:4444/mediawiki/index.php?tiitle=Water


###################

cmd="pkill -9 mpstat"
do_cmd $ruser $host "$cmd"

scp $ruser@$host:/local/sme/exp/mpstat_bench_mediawiki.out .

cat mpstat_bench_mediawiki.out
