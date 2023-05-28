CC = g++
CFLAGS = -Wall -pthread -std=c++20 -O2

all: sikradio-sender sikradio-receiver

clean:
	rm -f sikradio-sender sikradio-receiver *.o

dist:
	tar -czvf dw440014.tgz sikradio-sender.cpp sikradio-receiver.cpp common.h err.h makefile

sikradio-sender: sikradio-sender.cpp
	$(CC) $(CFLAGS) sikradio-sender.cpp -o sikradio-sender

sikradio-receiver: sikradio-receiver.cpp
	$(CC) $(CFLAGS) sikradio-receiver.cpp -o sikradio-receiver

.PHONY: all clean dist
