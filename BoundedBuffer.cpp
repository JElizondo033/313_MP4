#include "BoundedBuffer.h"
using namespace std;


BoundedBuffer::BoundedBuffer(int size) : full(0), empty(size), bufferSize(size)
{}

BoundedBuffer::~BoundedBuffer() {}

void BoundedBuffer::deposit(string data) {
	this->empty.P();
	this->mutex.Lock();

	//insert into buffer
	this->buffer.push_back(data);
	
	this->mutex.Unlock();
	this->full.V();
}

string BoundedBuffer::remove() {
	this->full.P();
	this->mutex.Lock();

	//remove from the buffer
	string returnData = this->buffer.front();
	this->buffer.pop_front();

	this->mutex.Unlock();
	this->empty.V();

	return returnData;
}
