#include "mutex.h"
using namespace std;

Mutex::Mutex()
{
	m = PTHREAD_MUTEX_INITIALIZER;
}

Mutex::~Mutex()
{
	pthread_mutex_destroy(&m);
}

int Mutex::Lock()
{
	return pthread_mutex_lock(&m);
}

int Mutex::Unlock()
{
	return pthread_mutex_unlock(&m);
}

