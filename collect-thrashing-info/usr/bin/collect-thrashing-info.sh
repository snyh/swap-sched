#!/bin/bash

threshold=80 # 80MB
logFileR=~/.cache/thrashing_infos.csv
logFileP=~/.cache/thrashing_processes
mkdir ~/.cache
recordTimes=100

swapUsed=$(free -m | awk 'FNR==3 {print $3}')
function doRecord
{
    echo "start recording.."
    date > $logFileP
    cat /proc/meminfo >> $logFileP
    ps aux >> $logFileP
    dstat -tcpdgsm --top-latency-avg --output $logFileR 1 $recordTimes
}

while true ; do
    if [[ $swapUsed -gt $threshold ]]; then
        doRecord
        exit 0
    fi
    sleep 1
done
