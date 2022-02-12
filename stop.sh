#!/bin/bash

ps -e -o pid -o cmd | grep PR | grep -v -E '(mpirun|grep)' | gawk -F' ' '{print $1}' > processes
while read line
do
  pid="$line"
  kill -10 "$pid"
done < processes
rm processes

