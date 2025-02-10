all: p2plant_psc

p2plant_psc: src/*.cpp
	gcc src/p2plant.cpp src/helpers.cpp src/transport_ipc.cpp tests/simulatedADCs.cpp ../tinycbor/lib/libtinycbor.a -o bin/simulatedADCs

clean:
	rm bin/*

