all: client dataserver

dataserver: dataserver.cpp reqchannel.o 
	g++ -std=c++11 -g -o dataserver dataserver.cpp reqchannel.o -lpthread 

client: client.cpp  reqchannel.o BoundedBuffer.o mutex.o semaphore.o
	g++ -std=c++11 -g -o client client.cpp reqchannel.o BoundedBuffer.o mutex.o  semaphore.o -lpthread


reqchannel.o: reqchannel.cpp reqchannel.h
	g++ -std=c++11 -c -g reqchannel.cpp

semaphore.o: semaphore.h semaphore.cpp
	g++ -std=c++11 -c -g semaphore.cpp

BoundedBuffer.o: BoundedBuffer.cpp BoundedBuffer.h
	g++ -std=c++11 -c -g BoundedBuffer.cpp

mutex.o: mutex.h mutex.cpp
	g++ -std=c++11 -c -g mutex.cpp

clean:
	rm *.o client dataserver
