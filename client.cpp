/* 
    File: simpleclient.C

    Author: meme man
            Department of Meme
            Texas A&Meme University
    Date  : 6969/04/20

    Simple client main program for MP2 in CSCE 313
*/


#include <cassert>
#include <string>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <string>
#include <vector>
#include <errno.h>
#include <unistd.h>
#include <map>
#include <fstream>

#include "reqchannel.h"
#include "BoundedBuffer.h"

using namespace std;

struct reqThreadArgs {
	string name;
	int numRequests;
	BoundedBuffer* reqBuffer;

	reqThreadArgs(string name, int numRequests, BoundedBuffer* reqBuffer) :
	name(name), numRequests(numRequests), reqBuffer(reqBuffer) {}
};

struct workerThreadArgs {
	BoundedBuffer* reqBuffer; //consume requests from the request buffer
	map<string, BoundedBuffer*>* statBufferMap; //pointer to stat buffer container
	vector<RequestChannel*>* requestChannels;

	workerThreadArgs(BoundedBuffer* reqBuffer, map<string, BoundedBuffer*>* statBufferMap, vector<RequestChannel*>* requestChannels) :
	reqBuffer(reqBuffer), statBufferMap(statBufferMap), requestChannels(requestChannels) {}
};

struct statThreadArgs {
	string name;
	BoundedBuffer* statBuffer; //consume data from stat buffer & build histogram
	vector<int>* histogramVec;

	statThreadArgs(string name, BoundedBuffer* statBuffer, vector<int>* histogramVec) :
	name(name), statBuffer(statBuffer), histogramVec(histogramVec) {}
};

//GLOBALS
bool EXPORT_DATA = false;
bool PRINT_HISTOGRAMS = false;

int NUM_REQUESTS = 1000;	//# of requests to issue PER PERSON - cmd line arg
int NUM_REQUEST_CHANNELS = 1; //# of worker threads active - cmd line arg
int REQ_BUFFER_SIZE = 100;  //size of request buffer - cmd line arg
int activeReqThreads = 3;   //counter - how many request threads currently active?
int activeWorkerThreads;    //counter - how many worker threads currently active?

Mutex reqCounterLock;
Mutex workerCounterLock;

void printHistogram(vector<int>* histogram)
{	//prints the argument histogram to the terminal (1 bin per data value)
	cout << "HISTOGRAM:" << endl;
	for (int i = 0; i < histogram->size(); i++)
	{
		cout << "data value " << i << "(" << histogram->at(i) << "): ";
		for (int j = 0; j < histogram->at(i); j++)
		{
			cout << "+";
		}
		cout << endl;
	}
}

void* reqThreadFunc(void* argsStruct)
{
	//extract args from struct pointer
	reqThreadArgs* args = (reqThreadArgs*)argsStruct;
	string name = args->name;
	int numRequests = args->numRequests;
	BoundedBuffer* reqBuffer = args->reqBuffer;

	for (int i = 0; i < numRequests; i++)
	{	//deposit "data <name>" into request buffer
		reqBuffer->deposit("data " + name);
	}

	//when we are done, notify the worker threads 
	reqBuffer->deposit("done");
}

void* statThreadFunc(void* argsStruct)
{
	//extract args from struct pointer
	statThreadArgs* args = (statThreadArgs*) argsStruct;
	string name = args->name;
	BoundedBuffer* statBuffer = args->statBuffer;
	vector<int>* histogramVec = args->histogramVec;

	for (;;)
	{
		string data = statBuffer->remove();
		if (data == "quit")
		{	//if worker threads are done depositing replies, exit the thread
			break;
		}
		else //if data != "quit", it must be a numeric string
		{
			try
			{
				histogramVec->at(stoi(data))++;	//add entry to histogram
			}
			catch (invalid_argument& e)
			{	//ignore potentially garbage data 
				continue;
			}
		}
	}
	
}

void* eventHandlerThreadFunc(void* argsStruct)
{
	//extract args from struct
	workerThreadArgs* args = (workerThreadArgs*)argsStruct;
	
	BoundedBuffer* reqBuffer = args->reqBuffer;
	map<string, BoundedBuffer*>* statBufferMap = args->statBufferMap;
	vector<RequestChannel*>* requestChannels = args->requestChannels;

	//descriptor to reqChannel map
	map<int, RequestChannel*> fdMap;

	//descriptor to name map
	map<int, string> nameMap;
	
	//'prime the pump' 
	for (int i = 0; i < requestChannels->size(); i++)
	{
		//send test request & extract name
		string request = reqBuffer->remove();
		string name = request.substr(5);
		
		requestChannels->at(i)->cwrite(request);

		//build the descriptor->reqChannel map (so we can send requests across proper channels)
        fdMap.insert(pair<int, RequestChannel*>(requestChannels->at(i)->read_fd(), requestChannels->at(i)));

        //build the descriptor->name map (so we know what stat buffer to fwd reply to)
        nameMap.insert(pair<int, string>(requestChannels->at(i)->read_fd(), name));
	}

	//declare/init read set
	fd_set readSet;
	
	//event loop
	for(;;)
	{
		//reset read set before each select() call
		FD_ZERO(&readSet);
		int maxFD = 0;
		
		for (int i = 0; i < requestChannels->size(); i++)
		{
			//add each channel's read descriptor to the read set 
			FD_SET(requestChannels->at(i)->read_fd(), &readSet);
			if (requestChannels->at(i)->read_fd() > maxFD)
			{	//keep track of highest descriptor count (select is tedious)
				maxFD = requestChannels->at(i)->read_fd();
			}
		}
		
		//wait for reply on channel descriptors -- this BLOCKS until there is activity!
		select(maxFD + 1, &readSet, NULL, NULL, NULL);

		//determine which descriptors were set
		for (int FD = 0; FD < maxFD; FD++)
		{
			if (FD_ISSET(FD, &readSet))
			{	//get reply from each set descriptor
				RequestChannel* replyChannel = fdMap.find(FD)->second;
				string reply = replyChannel->cread();

				//forward reply to proper stat map
				string name = nameMap.find(FD)->second;

				map<string, BoundedBuffer*>::iterator statMapIter = statBufferMap->find(name);
				statMapIter->second->deposit(reply);
				
				//send next request and update descriptor->name map
				string newRequest = reqBuffer->remove();
				if (newRequest == "done")
				{
					reqCounterLock.Lock();
					activeReqThreads--;
					reqCounterLock.Unlock();

					if (activeReqThreads == 0)
					{
						//if no more request threads, tell stat buffer to quit
						for (auto it =  statBufferMap->begin(); it != statBufferMap->end(); it++)
						{
							it->second->deposit("quit");
						}
						break;	//exit the thread
					}
				}
				else
				{
					string newName = newRequest.substr(5);
					
					replyChannel->cwrite(newRequest);
	
					map<int, string>::iterator nameMapIter = nameMap.find(FD);
					nameMapIter->second = newName;
				}
			}
		}		
	}
	
}

void buildHistogramFile(ofstream& dataFile, vector<int>* histogram)
{	//writes a histogram to a file (.xls format -- \r = new row, \t = new col)
  dataFile << "Data" << "\t" << "Count" << "\r";
  for (int i = 0; i < histogram->size(); i++)
  {
	  dataFile << i << "\t" << histogram->at(i) << "\r";
  }
}

string int2string(int number) {
   stringstream ss;//create a stringstream
   ss << number;//add number to the stream
   return ss.str();//return a string with the contents of the stream
}

/*--------------------------------------------------------------------------*/
/* MAIN FUNCTION */
/*--------------------------------------------------------------------------*/

int main(int argc, char * argv[]) {
  //parse command line args
  if (argc < 7)
  {	  //minimum # of args is 7 (including flags)
	  cout << "Usage:" << endl;
	  cout << "client  -n <number of data requests per person>\n\t-b <size of bounded buffer between request and worker threads>\n\t-w <number of worker threads>\n";
	  return 0;
  }
  else
  {
	  cout << "---PARAMETERS---" << endl;
	  
	  for (int i = 1; i < argc; i++) //ignore the exe name (start at argv[1])
	  {
		  if (string(argv[i]) == "-n")
		  {
			  NUM_REQUESTS = stoi(argv[i+1]);
			  cout << "num requests: " << stoi(argv[i+1]) << endl;
		  }
		  else if (string(argv[i]) == "-b")
		  {
			  REQ_BUFFER_SIZE = stoi(argv[i+1]);
			  cout << "buffer size: " << stoi(argv[i+1]) << endl;
		  }
		  else if (string(argv[i]) == "-w")
		  {
			  NUM_REQUEST_CHANNELS = stoi(argv[i+1]);
			  activeWorkerThreads = NUM_REQUEST_CHANNELS;
			  cout << "num worker threads: " << stoi(argv[i+1]) << endl;
		  }
		  else if (string(argv[i]) == "-xls")
		  {
			  EXPORT_DATA = true;
			  cout << "export data as .xls: true" << endl;
		  }
		  else if (string(argv[i]) == "-p")
		  {
			  PRINT_HISTOGRAMS = true;
			  cout << "print histograms: true" << endl;
		  }
		  else
		  {
			  continue;  
		  }
	  }
	  cout << "----------------" << endl;
  }

  //timers
  timeval startTime;
  timeval endTime;
  
  //fork & execute server
  pid_t currPID = fork();
  if (currPID == 0)
  {	  //run server in child process
	  execv("./dataserver", NULL);
  }

  cout << "CLIENT STARTED:" << endl;

  cout << "Establishing control channel... " << flush;
  RequestChannel chan("control", RequestChannel::CLIENT_SIDE); //create control channel
  cout << "done." << endl;

  //create request buffer
  BoundedBuffer* reqBuffer = new BoundedBuffer(REQ_BUFFER_SIZE);

  //request argument structs
  reqThreadArgs* reqArgsJane = new reqThreadArgs("Jane Doe", NUM_REQUESTS, reqBuffer);
  reqThreadArgs* reqArgsJoe = new reqThreadArgs("Joe Smith", NUM_REQUESTS, reqBuffer);
  reqThreadArgs* reqArgsJohn = new reqThreadArgs("John Doe", NUM_REQUESTS, reqBuffer);
  
  //stat buffers - fixed capacity of 100
  BoundedBuffer* statBufferJane = new BoundedBuffer(100);
  BoundedBuffer* statBufferJoe = new BoundedBuffer(100);
  BoundedBuffer* statBufferJohn = new BoundedBuffer(100);
  
  vector<int>* histogramVecJane = new vector<int>(100);
  vector<int>* histogramVecJoe = new vector<int>(100);
  vector<int>* histogramVecJohn = new vector<int>(100);

  //stat thread arguments
  statThreadArgs* statArgsJane = new statThreadArgs("Jane Doe", statBufferJane, histogramVecJane);
  statThreadArgs* statArgsJoe = new statThreadArgs("Joe Smith", statBufferJoe, histogramVecJoe);
  statThreadArgs* statArgsJohn = new statThreadArgs("John Doe", statBufferJohn, histogramVecJohn);

  //stat buffer map 
  map<string, BoundedBuffer*>* statBufferMap = new map<string, BoundedBuffer*>();

  //map stat buffers by person name
  statBufferMap->insert( pair<string, BoundedBuffer*>("Jane Doe", statBufferJane) );
  statBufferMap->insert( pair<string, BoundedBuffer*>("Joe Smith", statBufferJoe) );
  statBufferMap->insert( pair<string, BoundedBuffer*>("John Doe", statBufferJohn) );

  //contains request channels for worker event thread
  vector<RequestChannel*> workerReqChannels;
  
  //shared arg struct for all worker threads
  workerThreadArgs* workerArgs = new workerThreadArgs(reqBuffer, statBufferMap, &workerReqChannels);
 
  //threads
  pthread_t worker;
  
  pthread_t statJane;
  pthread_t statJoe;
  pthread_t statJohn;
  
  pthread_t reqJane;
  pthread_t reqJoe;
  pthread_t reqJohn;

  //start worker threads
  cout << "CLIENT: starting worker threads..." << endl;
  
  //timer starts here
  gettimeofday(&startTime, NULL);
  
  //worker thread request channels
  RequestChannel* workerChannel;

  //build request channels for event handler thread
  for (int i = 0; i < NUM_REQUEST_CHANNELS; i++)
  {
	  string newThreadName = chan.send_request("newthread");
	  
	  cout << "\tCLIENT: worker request channel '" << newThreadName << "' created." << endl;

	  //create a new request channel and pass it to each worker thread
	  workerChannel = new RequestChannel(newThreadName, RequestChannel::CLIENT_SIDE);
	  workerReqChannels.push_back(workerChannel);
  }

  pthread_create(&worker, NULL, eventHandlerThreadFunc, workerArgs);
  cout << endl << "CLIENT: worker threads started." << endl;

  //start request threads
  cout << "CLIENT: starting request threads..." << endl;
  pthread_create(&reqJane, NULL, reqThreadFunc, reqArgsJane);
  pthread_create(&reqJoe, NULL, reqThreadFunc, reqArgsJoe);
  pthread_create(&reqJohn, NULL, reqThreadFunc, reqArgsJohn);
  cout << endl << "CLIENT: request threads started." << endl;

  //start stat threads
  cout << "CLIENT: starting statistics threads..." << endl;
  pthread_create(&statJane, NULL, statThreadFunc, statArgsJane);
  pthread_create(&statJoe, NULL, statThreadFunc, statArgsJoe);
  pthread_create(&statJohn, NULL, statThreadFunc, statArgsJohn);
  cout << endl << "CLIENT: statistics threads started." << endl;

  //wait for threads to complete
  pthread_join(statJane, NULL);
  pthread_join(statJoe, NULL);
  pthread_join(statJohn, NULL);

  //end timer here
  gettimeofday(&endTime, NULL);
  cout << endl << "CLIENT: ALL THREADS DONE" << endl;
  
  //calculate elapsed time
  int64_t startTimeUsec = (startTime.tv_sec * 1e6) + startTime.tv_usec;
  int64_t endTimeUsec = (endTime.tv_sec * 1e6) + endTime.tv_usec;

  cout << "CLIENT: Thread execution time: " << to_string((endTimeUsec - startTimeUsec) * 1e-6) << " sec" << endl;

  if (EXPORT_DATA)
  {
	cout << "CLIENT: Writing request statistics to files..." << endl;
	ofstream janeFile("histogramJane.xls");
	ofstream joeFile("histogramJoe.xls");
	ofstream johnFile("histogramJohn.xls");
	
	buildHistogramFile(janeFile, histogramVecJane);
	buildHistogramFile(joeFile, histogramVecJoe);
	buildHistogramFile(johnFile, histogramVecJohn);

	janeFile.close();
	joeFile.close();
	johnFile.close();
  }

  if (PRINT_HISTOGRAMS)
  {
	cout << "CLIENT: Printing histograms..." << endl;
    printHistogram(histogramVecJane);
    printHistogram(histogramVecJoe);
    printHistogram(histogramVecJohn);
  }

  string reply = chan.send_request("quit");
  cout << "Reply to request 'quit' is '" << reply << "'" << endl;
  system("rm fifo_*");	//remove all pipes
}
