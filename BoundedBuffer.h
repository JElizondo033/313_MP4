#include "semaphore.h"
#include "mutex.h"
#include <list>
#include <string>


class BoundedBuffer {
	private:
		std::list<std::string> buffer;
		
		Semaphore full;  //track items produced
		Semaphore empty; //track remaining space

		Mutex mutex; 	 //buffer lock

		int bufferSize;

	public:
		BoundedBuffer(int size);
		~BoundedBuffer();

		//buffer operations
		void deposit(std::string data);
		std::string remove();
};
