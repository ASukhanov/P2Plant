# P2Plant
Server hosting process variables for point-to-point communications.

## Dependency
TinyCBOR: https://github.com/intel/tinycbor

## Build
gcc src/main.cpp src/helpers.cpp src/transport_ipc.cpp tests/simulatedADCs.cpp ../tinycbor/lib/libtinycbor.a -o bin/simulatedADCs
