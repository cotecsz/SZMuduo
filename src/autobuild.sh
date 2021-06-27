#!/bin/bash

set -e

if [ ! -d `pwd`../build ];then
    mkdir `pwd`../build
fi

rm -rf `pwd`../build/*

cd `pwd`,,/build &&
    cmake ../src &&
    make

cd ..

if [ ! -d /usr/include/szmuduo ]; then
    mkdir /usr/include/szmuduo
fi

for header in `ls *.h`
do
    cp $header /usr/include/szmuduo
done

cp `pwd`../lib/libszmuduo.so /usr/lib

ldconfig 