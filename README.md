# P2Plant
Server, which hosts adn posts process variables for point-to-point communications with a client.<br>
It simplifies development of hardware support for control systems like [EPICS](https://epics-base.github.io/p4p/index.html).<br>
The client access the process variables using **get**, **set** and **info** requests. Multiple requests can be executed in one transaction.<br>
Varying process variables are streamed continuously.<br>
Communication link between server and client is point-to-point [IPC](https://pubs.opengroup.org/onlinepubs/7908799/xsh/ipc.html). Data are encoded using [Concise Binary Object Representation (CBOR)](https://en.wikipedia.org/wiki/CBOR) interface: [tinycbor](https://github.com/intel/tinycbor).<br>
The client API for python clients is identical to json API.<br>
For maximum efficiency, the vector variables are encoded as typed arrays, no copy operation involved.<br>
Multi-dimensional vectors are supported.<br>

## Integration with EPICS PVAccess
Any P2Plant-based device could be bridged to EPICS PVAccess ecosystem using
[p2plant_ioc](https://github.com/ASukhanov/p2plant_ioc) softIocPVA.
It recognizes all features of P2Plant process variables and posts them as EPICS PVs.

## Usage
See tests/simulatedADCs.cpp.
- During initialization phase the process variables should be defined, initialized and their pointers are placed in the PVs array.
- During main loop, the continuously measured parameters need to be updated, timestamped and added to CBOR encoder for delivery.

## Dependency
TinyCBOR: https://github.com/intel/tinycbor

## Build
`make`

# Example
Run simulated 8-channel ADC:<br>
`bin/simulatedADCs`

For access see: [P2PlantAcces](https://github.com/ASukhanov/P2PlantAccess)


