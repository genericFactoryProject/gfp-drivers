# kernel
The gf kernel source.

## overview

(1) driver framework management:

device, bus and driver

(2) kernel object & resource management:

2.1 Time
2.2 Preemptive point
2.3 Memory
2.4 Virtual and Physics mapping
2.5 Cross Optimization, such as Memory and Time
2.6 Recursive dispatch engine

## compile
cmake -S. -Bbuild && cmake --build build

## update
git submodule init
git submodule update