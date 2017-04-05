

#include "VirtualMachine.h"
#include "Machine.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <queue>
#include <limits>

using namespace std;

volatile int sleepCount = 10;
volatile int go = -1;
volatile int globalTicks = 0;
volatile int tickStart;

int INFINITY = std::numeric_limits<int>::max();

extern "C"{
	TVMStatus VMStart(int tickms, int argc, char *argv[]);

	TVMStatus VMTickMS(int *tickmsref);
	TVMStatus VMTickCount(TVMTickRef tickref);

	TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid);
	TVMStatus VMThreadDelete(TVMThreadID thread);
	TVMStatus VMThreadActivate(TVMThreadID thread);
	TVMStatus VMThreadTerminate(TVMThreadID thread);
	TVMStatus VMThreadID(TVMThreadIDRef threadref);
	TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref);
	TVMStatus VMThreadSleep(TVMTick tick);

	TVMStatus VMMutexCreate(TVMMutexIDRef mutexref);
	TVMStatus VMMutexDelete(TVMMutexID mutex);
	TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref);
	TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout);     
	TVMStatus VMMutexRelease(TVMMutexID mutex);


	TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor);
	TVMStatus VMFileClose(int filedescriptor);      
	TVMStatus VMFileRead(int filedescriptor, void *data, int *length);
	TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
	TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset);


	TVMMainEntry VMLoadModule(const char *module);
	void VMUnloadModule(void);
	TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);


	void CallBackFile(void* ptr, int result);
	// void VMFileOpenCallBack(void* ptr, int result);
	// void VMFileWriteCallBack(void* ptr, int result);
	void VMThreadSleepCallBack(void* ptr);
	void MachineAlarmCallback(void* ptr);

	void Scheduler();
	void IdleThreadFunction(void*);
	void Skeleton(void* ptr);

}

class TCB{
public:
	TVMThreadID ID; 
	SMachineContextRef mcntxref;
	//TVMMutexID mutexID; MAYBE DONT NEED!!!!

	TVMTick mutexWaitTicks;

	TVMThreadState state;
	TVMStatus status;
	TVMThreadPriority priority;
	TVMMemorySize memSize;
	TVMTick sleepTicks;
	TVMThreadEntry entry;
	sigset_t signalState;
	void* param;
	int result;
}; 

class MutexClass{
public:
	TVMMutexID ID;
	queue<TCB*> highPriority;
	queue<TCB*> mediumPriority;
	queue<TCB*> lowPriority;
	TCB* curHolder;
	int state;// 0 is unlock state and 1 is lock state;

};

vector<TCB*> allThreads;
priority_queue<TCB*> highThreads;
priority_queue<TCB*> mediumThreads;
priority_queue<TCB*> lowThreads;
vector<TCB*> sleepThreads;

vector<MutexClass*> allMutexs;

TCB *MainThread = new TCB();
TCB *IdleThread;
TCB *curThread;


TVMStatus VMStart(int tickms, int argc, char *argv[]){
	tickStart = tickms;
	MachineInitialize();
	TVMMainEntry ptr = VMLoadModule(argv[0]);
	if(ptr == NULL){
		MachineTerminate();
		return VM_STATUS_FAILURE;
	}

	else{

		// CREATE MAIN THREAD
		MainThread->ID = 0;
		MainThread->priority = VM_THREAD_PRIORITY_NORMAL; // MAYBE CHANGE
		MainThread->state = VM_THREAD_STATE_RUNNING;
		MainThread->memSize = 0x100000;
		MainThread->mcntxref = new SMachineContext;
		MainThread->entry = NULL;

		allThreads.push_back(MainThread);
		//mediumThreads.push(MainThread);

		//Create context of main thread
		char *stackArray = new char[MainThread->memSize]; 
		MachineContextCreate(MainThread->mcntxref, NULL, MainThread->param, 
			stackArray, MainThread->memSize);

		curThread = MainThread;

		// Create idle thread
		TVMThreadID idle = 99;
		VMThreadCreate(IdleThreadFunction, NULL, 0x100000, VM_THREAD_PRIORITY_IDLE, &idle);
		VMThreadActivate(idle);


		MachineRequestAlarm(tickms*1000, MachineAlarmCallback, NULL);
		MachineEnableSignals(); 
		ptr(argc, argv);
		MachineTerminate();
		return VM_STATUS_SUCCESS;
	}

}


TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
	if(entry == NULL || tid == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}

	MachineSuspendSignals(&curThread->signalState);
	TCB *NewThread = new TCB();
	NewThread->ID = allThreads.size();
	*tid = NewThread->ID;
	NewThread->priority = prio;
	NewThread->state = VM_THREAD_STATE_DEAD;
	NewThread->memSize = memsize;
	NewThread->entry = entry;
	NewThread->param = param;
	NewThread->mcntxref = new SMachineContext;


	// If idle thread set as global idle else put in ALl threads
	if(prio == VM_THREAD_PRIORITY_IDLE)
		IdleThread = NewThread;

	allThreads.push_back(NewThread);
	

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;
}



TVMStatus VMThreadActivate(TVMThreadID thread){
	MachineSuspendSignals(&curThread->signalState);
	for(unsigned int i=0; i<allThreads.size();i++){
		if(allThreads[i]->ID == thread){

			// cout << "LOOK" << allThreads[i]->ID << endl; // *** ADDED COUT 

			if(allThreads[i]->state != VM_THREAD_STATE_DEAD){
				return VM_STATUS_ERROR_INVALID_STATE;
			}
			else{

				char *stackArray = new char[allThreads[i]->memSize]; 


				if(allThreads[i]->priority == VM_THREAD_PRIORITY_IDLE){
				MachineContextCreate(allThreads[i]->mcntxref, allThreads[i]->entry, allThreads[i]->param, 
					stackArray, allThreads[i]->memSize); //Need to change params maybe 					
				}
				else{
				MachineContextCreate(allThreads[i]->mcntxref, Skeleton, allThreads[i], 
					stackArray, allThreads[i]->memSize); //Need to change params maybe 
				}

				allThreads[i]->state = VM_THREAD_STATE_READY;

				if(allThreads[i]->priority == VM_THREAD_PRIORITY_LOW)
					lowThreads.push(allThreads[i]);
				else if(allThreads[i]->priority == VM_THREAD_PRIORITY_NORMAL){
					mediumThreads.push(allThreads[i]);
				}
				else if(allThreads[i]->priority == VM_THREAD_PRIORITY_HIGH)
					highThreads.push(allThreads[i]);

				if(allThreads[i]->priority > curThread->priority)
					Scheduler();

				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_SUCCESS;
			}
		}
	}
	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMThreadDelete(TVMThreadID thread){
	MachineSuspendSignals(&curThread->signalState);

    for(unsigned int i = 0; i < allThreads.size(); i++){
            if(allThreads[i]->ID == thread){
                if(allThreads[i]->state != VM_THREAD_STATE_DEAD)
                    return VM_STATUS_ERROR_INVALID_STATE;

                allThreads.erase(allThreads.begin()+i);

                MachineResumeSignals(&curThread->signalState);
                return VM_STATUS_SUCCESS;
            }
        }
    MachineResumeSignals(&curThread->signalState);
    return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){
	MachineSuspendSignals(&curThread->signalState);

	if(curThread->ID == thread){
		//cout << "here you go" << endl;
		// if(curThread->ID == VM_THREAD_STATE_DEAD)
		// 	return VM_STATUS_ERROR_INVALID_STATE; 
		curThread->state = VM_THREAD_STATE_DEAD;
		Scheduler();
	}

    for(unsigned int i = 0; i < allThreads.size(); i ++){
        if(allThreads[i]->ID == thread){
            if(allThreads[i]->state == VM_THREAD_STATE_DEAD){
            	MachineResumeSignals(&curThread->signalState);
                return VM_STATUS_ERROR_INVALID_STATE;
            }
            allThreads[i]->state = VM_THREAD_STATE_DEAD;
            MachineResumeSignals(&curThread->signalState);
            return VM_STATUS_SUCCESS;
        }

    }
    MachineResumeSignals(&curThread->signalState);
    return VM_STATUS_ERROR_INVALID_ID;

}


TVMStatus VMThreadID(TVMThreadIDRef threadref){
	if(threadref == NULL)
		return VM_STATUS_ERROR_INVALID_PARAMETER;

	MachineSuspendSignals(&curThread->signalState);
	threadref = &curThread->ID;

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;
}


TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
        if(stateref == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        MachineSuspendSignals(&curThread->signalState);
        for(unsigned int i = 0; i < allThreads.size(); i ++){
           if(allThreads[i]->ID == thread){
                *stateref = allThreads[i]->state;
                MachineResumeSignals(&curThread->signalState);
                return VM_STATUS_SUCCESS;
           }
        }

        MachineResumeSignals(&curThread->signalState);
        return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMThreadSleep(TVMTick tick){
	MachineSuspendSignals(&curThread->signalState);


	if(tick == VM_TIMEOUT_INFINITE){
		MachineResumeSignals(&curThread->signalState);
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else if(tick == VM_TIMEOUT_IMMEDIATE){
		curThread->state = VM_THREAD_STATE_READY;
		if(curThread->priority == VM_THREAD_PRIORITY_LOW)
			lowThreads.push(curThread);
		else if(curThread->priority == VM_THREAD_PRIORITY_NORMAL)
			mediumThreads.push(curThread);
		else if(curThread->priority == VM_THREAD_PRIORITY_HIGH)
			highThreads.push(curThread);

		Scheduler();
		MachineResumeSignals(&curThread->signalState);
		return VM_STATUS_SUCCESS;
	}
	else{
		curThread->state = VM_THREAD_STATE_WAITING;
		curThread->sleepTicks = tick;
		sleepThreads.push_back(curThread);

		Scheduler();
		MachineResumeSignals(&curThread->signalState);
		return VM_STATUS_SUCCESS;
	}

	MachineResumeSignals(&curThread->signalState);
}



TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
	if(filename == NULL || filedescriptor == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}

	MachineSuspendSignals(&curThread->signalState);
	curThread->state = VM_THREAD_STATE_WAITING;
	MachineFileOpen(filename, flags, mode, CallBackFile, curThread);
	Scheduler();
	*filedescriptor = curThread->result;

	MachineResumeSignals(&curThread->signalState);
	if(curThread->result >= 0)
		return VM_STATUS_SUCCESS;
	return VM_STATUS_FAILURE;
} // FileOpen


TVMStatus VMFileClose(int filedescriptor){

	MachineSuspendSignals(&curThread->signalState);
	curThread->state = VM_THREAD_STATE_WAITING;
	MachineFileClose(filedescriptor, CallBackFile, curThread);
	Scheduler();

	MachineResumeSignals(&curThread->signalState);

	if(curThread->result >= 0)
		return VM_STATUS_SUCCESS;

	return VM_STATUS_FAILURE;
} // FileClose


TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
	if(data == NULL || length == NULL)
		return VM_STATUS_ERROR_INVALID_PARAMETER;

	MachineSuspendSignals(&curThread->signalState);
	curThread->state = VM_THREAD_STATE_WAITING;
	MachineFileRead(filedescriptor, data, *length, CallBackFile, curThread);
	Scheduler();

	*length = curThread->result;

	MachineResumeSignals(&curThread->signalState);
	if(curThread->result >= 0)
		return VM_STATUS_SUCCESS;
	return VM_STATUS_FAILURE;
} //FileRead


TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
	if(data == NULL || length == NULL)
		return VM_STATUS_ERROR_INVALID_PARAMETER;


	MachineSuspendSignals(&curThread->signalState);
	curThread->state = VM_THREAD_STATE_WAITING;
	MachineFileWrite(filedescriptor, data, *length, CallBackFile, curThread);
	Scheduler();
	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;
} //Filewrite



TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){

	MachineSuspendSignals(&curThread->signalState);
	curThread->state = VM_THREAD_STATE_WAITING;
	MachineFileSeek(filedescriptor, offset, whence, CallBackFile, curThread);

	Scheduler();

	*newoffset = curThread->result;

	MachineResumeSignals(&curThread->signalState);
	if(curThread->result >= 0)
		return VM_STATUS_SUCCESS;
	
	return VM_STATUS_FAILURE;
} // FileSeek


TVMStatus VMTickMS(int *tickmsref){
	if(tickmsref == NULL)
		return VM_STATUS_ERROR_INVALID_PARAMETER;

	MachineSuspendSignals(&curThread->signalState);

	*tickmsref = tickStart;

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;
} //TickMS

TVMStatus VMTickCount(TVMTickRef tickref){

	MachineSuspendSignals(&curThread->signalState);

	if(tickref == NULL){
		MachineResumeSignals(&curThread->signalState);
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else{
		*tickref = globalTicks;
	}

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;

}// TickCount



TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
	if(mutexref == NULL)
		return VM_STATUS_ERROR_INVALID_PARAMETER;

	MachineSuspendSignals(&curThread->signalState);

	MutexClass *Mut = new MutexClass();
	Mut->state = 0;
	allMutexs.push_back(Mut);
	Mut->ID = allMutexs.size();
	*mutexref = Mut->ID;

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
	MachineSuspendSignals(&curThread->signalState);

	for(unsigned int i = 0; i < allMutexs.size(); i++){
		if(mutex == allMutexs[i]->ID){
			if(allMutexs[i]->state == 0){
				allMutexs.erase(allMutexs.begin() + i);

				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_SUCCESS;
			}else{
				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
		}
	}
	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
	if(ownerref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	MachineSuspendSignals(&curThread->signalState);
	for(unsigned int i = 0; i < allMutexs.size(); i++){
		if(mutex == allMutexs[i]->ID){
			ownerref = &allMutexs[i]->curHolder->ID;
			// cout << &allMutexs[i]->curHolder->ID << endl;
			MachineResumeSignals(&curThread->signalState);
			return VM_STATUS_SUCCESS;
		}
	}

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
	MachineSuspendSignals(&curThread->signalState);
	for(unsigned int i = 0; i < allMutexs.size(); i++){
		if(mutex == allMutexs[i]->ID){

			//If mutex is not held by anyone just take it
			if(allMutexs[i]->state != 1){
				allMutexs[i]->state = 1;
				allMutexs[i]->curHolder = curThread;

				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_SUCCESS;
			}

			//Otherwise the mutex is held
			else{
				if(timeout == VM_TIMEOUT_IMMEDIATE)
					return VM_STATUS_FAILURE;

				// Wait for infinity
				else if(timeout == VM_TIMEOUT_INFINITE){
					curThread->mutexWaitTicks = INFINITY;

					if(curThread->priority == VM_THREAD_PRIORITY_LOW)
						allMutexs[i]->lowPriority.push(curThread);
					if(curThread->priority == VM_THREAD_PRIORITY_NORMAL)
						allMutexs[i]->mediumPriority.push(curThread);
					if(curThread->priority == VM_THREAD_PRIORITY_HIGH)
						allMutexs[i]->highPriority.push(curThread);

					curThread->state = VM_THREAD_STATE_WAITING;

					Scheduler();

				}
				else{
					curThread->mutexWaitTicks = timeout;
					if(curThread->priority == VM_THREAD_PRIORITY_LOW)
						allMutexs[i]->lowPriority.push(curThread);
					if(curThread->priority == VM_THREAD_PRIORITY_NORMAL)
						allMutexs[i]->mediumPriority.push(curThread);
					if(curThread->priority == VM_THREAD_PRIORITY_HIGH)
						allMutexs[i]->highPriority.push(curThread);

					curThread->state = VM_THREAD_STATE_WAITING;
					Scheduler();

				}
			}
			MachineResumeSignals(&curThread->signalState);
			return VM_STATUS_SUCCESS;
		}
	}

	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_ERROR_INVALID_ID;
}

TVMStatus VMMutexRelease(TVMMutexID mutex){
	MachineSuspendSignals(&curThread->signalState);

	for(unsigned int i = 0; i < allMutexs.size(); i++){
		if(allMutexs[i]->ID == mutex){
			if(allMutexs[i]->state == 0){

				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
			else{

				allMutexs[i]->state = 0;
				allMutexs[i]->curHolder = NULL;


				if(allMutexs[i]->highPriority.size() != 0){
					TCB* newHolder = allMutexs[i]->highPriority.front();
					newHolder->mutexWaitTicks = 0;
					allMutexs[i]->curHolder = newHolder;
					allMutexs[i]->highPriority.pop();

					// cout << newHolder << endl;
					newHolder->state = VM_THREAD_STATE_READY;
					highThreads.push(newHolder);
					Scheduler();
					MachineResumeSignals(&curThread->signalState);
					return VM_STATUS_SUCCESS;
					
				}

				if(allMutexs[i]->mediumPriority.size() != 0){
					TCB* newHolder = allMutexs[i]->mediumPriority.front();
					newHolder->mutexWaitTicks = 0;
					allMutexs[i]->curHolder = newHolder;
					allMutexs[i]->mediumPriority.pop();

					newHolder->state = VM_THREAD_STATE_READY;
					mediumThreads.push(newHolder);

					Scheduler();
					MachineResumeSignals(&curThread->signalState);
					return VM_STATUS_SUCCESS;

				}

				if(allMutexs[i]->lowPriority.size() != 0){
					TCB* newHolder = allMutexs[i]->lowPriority.front();
					newHolder->mutexWaitTicks = 0;
					allMutexs[i]->curHolder = newHolder;
					allMutexs[i]->lowPriority.pop();

					newHolder->state = VM_THREAD_STATE_READY;
					lowThreads.push(newHolder);

					Scheduler();
					MachineResumeSignals(&curThread->signalState);
					return VM_STATUS_SUCCESS;
				}

				MachineResumeSignals(&curThread->signalState);
				return VM_STATUS_SUCCESS;

			}
		}
	}


	MachineResumeSignals(&curThread->signalState);
	return VM_STATUS_ERROR_INVALID_ID;
}

/* CALL BACKS */

void CallBackFile(void* ptr, int result){

	MachineSuspendSignals(&curThread->signalState);
	TCB* temp = (TCB*)ptr;
	temp->result = result;
	temp->state = VM_THREAD_STATE_READY;

	if(temp->priority == VM_THREAD_PRIORITY_HIGH)
		highThreads.push(temp);
	else if(temp->priority == VM_THREAD_PRIORITY_NORMAL)
		mediumThreads.push(temp);
	else lowThreads.push(temp);
	Scheduler();

	MachineResumeSignals(&curThread->signalState);

}



//WORKS
void MachineAlarmCallback(void* calldata){
	MachineSuspendSignals(&curThread->signalState);

	globalTicks++;

	for(unsigned int i=0;i<sleepThreads.size();i++){

		if(sleepThreads[i]->state == VM_THREAD_STATE_DEAD)
			sleepThreads.erase(sleepThreads.begin()+i);

		sleepThreads[i]->sleepTicks--;
		if(sleepThreads[i]->sleepTicks == 0){
			sleepThreads[i]->state = VM_THREAD_STATE_READY;
	
		if(sleepThreads[i]->priority == VM_THREAD_PRIORITY_HIGH)
			highThreads.push(sleepThreads[i]);
		else if(sleepThreads[i]->priority == VM_THREAD_PRIORITY_NORMAL)
			mediumThreads.push(sleepThreads[i]);
		else lowThreads.push(sleepThreads[i]);			
		
		sleepThreads.erase(sleepThreads.begin()+i);
		}
	}


	//decrement mutex counters
	for(unsigned int i=0;i<allMutexs.size();i++){
		if(allMutexs[i]->lowPriority.size() != 0 ){
			for(unsigned int k=0; k<allMutexs[i]->lowPriority.size(); k++){
				TCB* temp = allMutexs[i]->lowPriority.front();
				if(temp->mutexWaitTicks != INFINITY){
					allMutexs[i]->lowPriority.pop();
					temp->mutexWaitTicks--;
					if(temp->mutexWaitTicks != 0)
						allMutexs[i]->lowPriority.push(temp);
					else{
						temp->state = VM_THREAD_STATE_READY;
						lowThreads.push(temp);
					}
				}
			}
		}

		if(allMutexs[i]->mediumPriority.size() != 0){
			for(unsigned int k=0; k<allMutexs[i]->mediumPriority.size(); k++){
				TCB* temp = allMutexs[i]->mediumPriority.front();
				if(temp->mutexWaitTicks != INFINITY){
					allMutexs[i]->mediumPriority.pop();
					temp->mutexWaitTicks--;
					if(temp->mutexWaitTicks != 0)
						allMutexs[i]->mediumPriority.push(temp);
					else{
						temp->state = VM_THREAD_STATE_READY;
						mediumThreads.push(temp);
					}
				}
			}
		}

		if(allMutexs[i]->highPriority.size() != 0){
			for(unsigned int k=0; k<allMutexs[i]->highPriority.size(); k++){
				TCB* temp = allMutexs[i]->highPriority.front();
				if(temp->mutexWaitTicks != INFINITY){
					allMutexs[i]->highPriority.pop();
					temp->mutexWaitTicks--;
					if(temp->mutexWaitTicks != 0)
						allMutexs[i]->highPriority.push(temp);
					else{
						temp->state = VM_THREAD_STATE_READY;
						highThreads.push(temp);
					}
				}
			}
		}
	}

	Scheduler();

	MachineResumeSignals(&curThread->signalState);
}


// 0 is main
// 1 is the idler
// 2 is low 
// 3 is midum
// 4 is high
void Scheduler(){
	 //cout << "IN SCHEDULAR: " << endl;


	// cout << allThreads.size() << endl;
	TCB* Old = curThread;
	TCB* New = NULL;
	//Old->state = VM_THREAD_STATE_WAITING;

	 //cout << "In scheudlared" << endl;

   if(curThread->state == VM_THREAD_STATE_RUNNING && curThread->priority != VM_THREAD_PRIORITY_IDLE){
   		if( curThread->priority == VM_THREAD_PRIORITY_LOW && lowThreads.empty() && mediumThreads.empty() && highThreads.empty() )
       		return;

       	if( curThread->priority == VM_THREAD_PRIORITY_NORMAL && mediumThreads.empty()  && highThreads.empty() )
       		return;

       	if( curThread->priority == VM_THREAD_PRIORITY_HIGH && highThreads.empty() )
       		return;

   }


   	// if(curThread->priority == VM_THREAD_PRIORITY_HIGH && curThread->state == VM_THREAD_STATE_RUNNING)
   	// 	highPriority.push(curThread);

    //cout << mediumThreads.size() << endl;
   if(highThreads.size()){
   		New = highThreads.top();
   		highThreads.pop();
   }
   else if(mediumThreads.size()){
   		New = mediumThreads.top();
   		mediumThreads.pop();
   }
   else if(lowThreads.size()){
   		New = lowThreads.top();
   		lowThreads.pop();
   }
   else {
   		New = IdleThread;
   		//cout << "switch to idle" << endl;
   		//cout << "IDLE scheudlared" << endl;
   }

   	if(curThread->state == VM_THREAD_STATE_RUNNING && curThread->priority == VM_THREAD_PRIORITY_LOW)
   		lowThreads.push(curThread);
   	else if(curThread->state == VM_THREAD_STATE_RUNNING && curThread->priority == VM_THREAD_PRIORITY_NORMAL)
   		mediumThreads.push(curThread);
   	else if(curThread->state == VM_THREAD_STATE_RUNNING && curThread->priority == VM_THREAD_PRIORITY_HIGH)
   		highThreads.push(curThread);


	curThread = New;
   	New->state = VM_THREAD_STATE_RUNNING;

 	// cout << Old->ID << " " << New->ID << endl;

 	// cout << Old->state << endl;
   	MachineContextSwitch(Old->mcntxref, New->mcntxref);

   	return;



}


void IdleThreadFunction(void*){
	MachineEnableSignals();
	while(true){}
}


void Skeleton(void* ptr){
	TCB* temp = (TCB*)ptr;
	MachineEnableSignals();
	temp->entry(temp->param);
	VMThreadTerminate(temp->ID);
}
