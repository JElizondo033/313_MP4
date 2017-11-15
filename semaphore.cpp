#include "semaphore.h"
using namespace std;

Semaphore::Semaphore(int _val) : value(_val)
{
	pthread_mutex_init(&this->m, NULL);
	pthread_cond_init(&this->c, NULL);
}

Semaphore::~Semaphore()
{
	pthread_mutex_destroy(&this->m);
	pthread_cond_destroy(&this->c);
}

int Semaphore::P()
{
	pthread_mutex_lock(&this->m);
	
	this->value--;
	if (this->value < 0)
	{
		//wait while value is negative 
		pthread_cond_wait(&this->c, &this->m);
	}
	
	pthread_mutex_unlock(&this->m);
	return this->value; //placeholder return
}

int Semaphore::V()
{
	pthread_mutex_lock(&this->m);
	
	this->value++;

	if (this->value <= 0)
	{
		//wake up any waiting threads
		pthread_cond_signal(&this->c);
	}
	
	
	pthread_mutex_unlock(&this->m);
	return this->value; //placeholder return
	
}
