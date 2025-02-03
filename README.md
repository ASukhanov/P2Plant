# P2Plant
Server, which hosts process variables for point-to-point communications with a client.

## Dependency
TinyCBOR: https://github.com/intel/tinycbor

## Build
gcc src/main.cpp src/helpers.cpp src/transport_ipc.cpp tests/simulatedADCs.cpp ../tinycbor/lib/libtinycbor.a -o bin/simulatedADCs
