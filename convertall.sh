#!/bin/sh

for net in `ls $1`; do
	for chan in `ls $1/$net`; do
		./FromText $1/$net/$chan/$chan.log $2 $net $chan
	done
done

