

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include "SM.h"
#include "times.h"
#include "queue.h"

#define DEBUG 0 			
#define TUNING 0
#define MAX_WORK_INTERVAL 75 * 1000 * 1000 
#define BINARY_CHOICE 2

SmStruct shmMsg;
SmStruct *p_shmMsg;

int childId; 				
int pcbIndex;				
int startSeconds;			
int startUSeconds;			
int endSeconds;				
int endUSeconds;			
int exitSeconds;			
int exitUSeconds;			


char timeVal[30]; 

void do_work(int willRunForThisLong);

int main(int argc, char *argv[]) {
childId = atoi(argv[0]); 
pcbIndex = atoi(argv[1]); 

getTime(timeVal);
if (DEBUG) printf("Worker %s: PCBINDEX: %d\n", timeVal, pcbIndex);

srand(getpid()); 
int processTimeRequired = rand() % (MAX_WORK_INTERVAL);
const int oneMillion = 1000000000;


getTime(timeVal);
if (childId < 0) {
	if (DEBUG) printf("Worker %s: Wrong child id: %d\n", timeVal, getpid());
	exit(1);
} else {
	if (DEBUG) printf("Worker %s: child %d (#%d) simulated work load: %d started normally after execl\n", timeVal, (int) getpid(), childId, processTimeRequired);

	
	getTime(timeVal);
	if (DEBUG) printf("Worker %s: child %d (#%d) create shared memory\n", timeVal, (int) getpid(), childId);

	
	int shmid;
	if ((shmid = shmget(SHM_MSG_KEY, SHMSIZE, 0660)) == -1) {
		printf("sharedMemory: shmget error code: %d", errno);
		perror("sharedMemory: Creating shared memory segment failed\n");
		exit(1);
	}
	p_shmMsg = &shmMsg;
	p_shmMsg = shmat(shmid, NULL, 0);

	startSeconds = p_shmMsg->ossSeconds;
	startUSeconds = p_shmMsg->ossUSeconds;

	getTime(timeVal);
	if (TUNING || DEBUG)
		printf("Worker %s: child %d (#%d) read start time in shared memory: %d.%09d\n",
			timeVal, (int) getpid(), childId, startSeconds, startUSeconds);


	
	sem_t *sem = open_semaphore(0);

	struct timespec timeperiod;
	timeperiod.tv_sec = 0;
	timeperiod.tv_nsec = 5 * 10000;

	while (1) { 

		if (p_shmMsg->dispatchedPid != (int) getpid()) {
				nanosleep(&timeperiod, NULL); 
				continue;
			}

			sem_wait(sem);

			int runTime = p_shmMsg->dispatchedTime; 

			
			p_shmMsg->dispatchedPid = 0;
			p_shmMsg->dispatchedTime = 0;

			sem_post(sem);

			getTime(timeVal);
			printf("Worker %s: ceceiving that process %d can run for %d nanoseconds\n", timeVal, (int) getpid(), runTime);

			
			int willRunForFullTime = (rand() % BINARY_CHOICE); 
			int willRunForThisLong;

			if (willRunForFullTime) {
				willRunForThisLong = runTime; 
			} else {
				willRunForThisLong = (rand() % runTime); 
			}

			do_work(willRunForThisLong); 

			sem_wait(sem);

			
			p_shmMsg->userPid = (int) getpid();
			if (p_shmMsg->pcb[pcbIndex].totalCpuTime + willRunForThisLong > processTimeRequired) {
				p_shmMsg->userHaltSignal = 0; 

				
				p_shmMsg->pcb[pcbIndex].startUserSeconds = startSeconds;
				p_shmMsg->pcb[pcbIndex].startUserUSeconds = startUSeconds;
				p_shmMsg->pcb[pcbIndex].endUserSeconds = p_shmMsg->ossSeconds;
				p_shmMsg->pcb[pcbIndex].endUserUSeconds = p_shmMsg->ossUSeconds;
			}
			else
				p_shmMsg->userHaltSignal = 1; 
			p_shmMsg->userHaltTime = willRunForThisLong;

			sem_post(sem);

			getTime(timeVal);
			if (DEBUG) printf("Worker %s: Process %d checking escape conditions\nTotalCPUTime: %d willRunForThisLong: %d processTimeRequired: %d\n", timeVal, (int) getpid(), p_shmMsg->pcb[pcbIndex].totalCpuTime, willRunForThisLong ,processTimeRequired);

			if (p_shmMsg->pcb[pcbIndex].totalCpuTime + willRunForThisLong > processTimeRequired)
				break;

	} 

	getTime(timeVal);
	printf("Worker %s: Process %d escaped main while loop\n", timeVal, (int) getpid());

	sem_wait(sem);

	
	p_shmMsg->userPid = (int) getpid();
	p_shmMsg->userHaltSignal = 0;

	sem_post(sem);

	
	shmdt(p_shmMsg);

	
	close_semaphore(sem);

	getTime(timeVal);
	if (DEBUG) printf("Worker %s: child %d (#%d) exiting normally\n", timeVal, (int) getpid(), childId);
}
exit(0);
}



void do_work(int willRunForThisLong) {

	getTime(timeVal);
	printf("Worker %s: Process %d working for %d nanoseconds\n", timeVal, (int) getpid(), willRunForThisLong);

	struct timespec sleeptime;
	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = willRunForThisLong;
	nanosleep(&sleeptime, NULL); 

	getTime(timeVal);
	printf("Worker %s: Process %d work completed\n", timeVal, (int) getpid());

}

