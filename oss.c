
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "SM.h"
#include "times.h"
#include "queue.h"

#define DEBUG 0							
#define VERBOSE 0						
#define TUNING 0						

#define PRIQUEUEHI 5000					
#define PRIQUEUEMED 500000				
#define PRIQUEUELO 50000000				

const int maxChildProcessCount = 100; 	
const long maxWaitInterval = 3L;		

int totalChildProcessCount = 0; 		
int signalIntercepted = 0; 				
int childrenDispatching = 0;			
int ossSeconds;							
int ossUSeconds;						
int quantum = 100000;					
char timeVal[30]; 						
long timeStarted = 0;					
long timeToStop = 0;					

long long totalTurnaroundTime;			
long long totalWaitTime;
int totalProcesses;
long long totalCpuIdleTime;

const int hipri = 0;					
const int medpri = 1;
const int lopri = 2;

FILE *logFile;

SmStruct shmMsg;
SmStruct *p_shmMsg;
sem_t *sem;

pid_t childpids[5000]; 					

void signal_handler(int signalIntercepted); 	
void increment_clock(int offset); 				
void kill_detach_destroy_exit(int status); 		

int pcbMapNextAvailableIndex();						
void pcbAssign(int pcbMap[], int index, int pid);	
void pcbDelete(int pcbMap[], int index);			
int pcbFindIndex(int pid);							
int pcbDispatch(int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS], int priQueueQuantums[3]); 
void pcbUpdateStats(int pcbIndex);					
void pcbAssignQueue(int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS], int priQueueQuantums[], int pcbIndex); 
void pcbUpdateTotalStats(int pcbIndex);				
void pcbDisplayTotalStats();						

int main(int argc, char *argv[]) {
	int childProcessCount = 0;			
	int dispatchedProcessCount = 0;		
	int maxDispatchedProcessCount = 1;	
	int opt; 							
	pid_t childpid;						
	int maxConcSlaveProcesses = 18;		
	int maxOssTimeLimitSeconds = 10000;	
	char logFileName[50]; 				
	strncpy(logFileName, "log.out", sizeof(logFileName)); 
	int totalRunSeconds = 20; 			
	int goClock = 0;					

	int pcbMap[MAX_PROCESS_CONTROL_BLOCKS];

	
	int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS]; 						
	int priQueueQuantums[] = {PRIQUEUEHI, PRIQUEUEMED, PRIQUEUELO};		

	initialize(priQueues[hipri]);
	initialize(priQueues[medpri]);
	initialize(priQueues[lopri]);

	time_t t;
	srand(getpid()); 					
	int interval = (rand() % maxWaitInterval);
	long nextChildTime;	

	
	while ((opt = getopt(argc, argv, "hl:q:s:t:")) != -1) {
		switch (opt) {
		case 'l': 
			strncpy(logFileName, optarg, sizeof(logFileName));
			if (DEBUG) printf("opt l detected: %s\n", logFileName);
			break;
		case 'q': 
			quantum = atoi(optarg);
			if (DEBUG) printf("opt q detected: %d\n", quantum);
			break;
		case 's': 
			maxConcSlaveProcesses = atoi(optarg);
			if (DEBUG) printf("opt s detected: %d\n", maxConcSlaveProcesses);
			break;
		case 't': 
			totalRunSeconds = atoi(optarg);
			if (DEBUG) printf("opt t detected: %d\n", totalRunSeconds);
			break;
		case 'h': 
			if (DEBUG) printf("opt h detected\n");
			fprintf(stderr,"Usage: ./%s <arguments>\n", argv[0]);
			break;
		default:
			break;
		}
	}

	if (argc < 1) {
			fprintf(stderr, "Usage: %s command arg1 arg2 ...\n", argv[0]);
			return 1;
	}

	
	logFile = fopen(logFileName,"w+");

	if (logFile == NULL) {
		perror("Log file can't open");
		exit(1);
	}

	
	getTime(timeVal);
	if (DEBUG) printf("\n\nOSS  %s: create shared memory\n", timeVal);

	
	int shmid;
	if ((shmid = shmget(SHM_MSG_KEY, SHMSIZE, IPC_CREAT | 0660)) == -1) {
		fprintf(stderr, "sharedMemory: shmget error code: %d", errno);
		perror("sharedMemory: Creating shared memory segment failed\n");
		exit(1);
	}
	p_shmMsg = &shmMsg;
	p_shmMsg = shmat(shmid,NULL,0);

	p_shmMsg->ossSeconds = 0;
	p_shmMsg->ossUSeconds = 0;
	p_shmMsg->dispatchedPid = 0;
	p_shmMsg->dispatchedTime = 0;

	
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++)
		pcbDelete(pcbMap, i);

	
	sem = open_semaphore(1);

	
	signal(SIGINT, signal_handler);

	getTime(timeVal);
	if (DEBUG && VERBOSE) printf("OSS  %s: Main loop entered\n", timeVal);

	
	while (1) {

		
		struct timespec timeperiod;
		timeperiod.tv_sec = 0;
		timeperiod.tv_nsec = 5 * 10000;
		nanosleep(&timeperiod, NULL);

		
		if (signalIntercepted) { 
			printf("\nmaster:  oss terminating children due to a signal! \n\n");
			printf("master: parent terminated due to a signal!\n\n");

			kill_detach_destroy_exit(130);
		}

		
		if (totalChildProcessCount >= maxChildProcessCount			
				|| ossSeconds >= maxOssTimeLimitSeconds || 			
				(timeToStop != 0 && timeToStop < getUnixTime())) { 	

			char typeOfLimit[50];
			strncpy(typeOfLimit,"",50);
			if (totalChildProcessCount >= maxChildProcessCount) strncpy(typeOfLimit,"because of process limit",50);
			if (ossSeconds > maxOssTimeLimitSeconds ) strncpy(typeOfLimit,"because of OSS time limit",50);
			if (timeToStop != 0 && timeToStop < getUnixTime()) strncpy(typeOfLimit,"because of real time limit (20s)",50);

			getTime(timeVal);
			printf("\nOSS  %s: Halting %s.\nTotal Processes Spawned: %d\nTotal Processes Time: %d\nOSS Seconds: %d.%09d\nStop Time:    %ld\nCurrent Time: %ld\n",
					timeVal, typeOfLimit, totalChildProcessCount, totalChildProcessCount, ossSeconds, ossUSeconds, timeToStop, getUnixTime());

			fprintf(logFile, "\nOSS  %s: Halting %s.\nTotal Processes Spawned: %d\nTotal Processes  Time: %d\nOSS Seconds: %d.%09d\nStop Time:    %ld\nCurrent Time: %ld\n",
								timeVal, typeOfLimit, totalChildProcessCount, totalChildProcessCount, ossSeconds, ossUSeconds, timeToStop, getUnixTime());

			pcbDisplayTotalStats();

			kill_detach_destroy_exit(0);
		}

	
		if (childpid != 0 && goClock) {
			if (timeToStop == 0) {
				
				struct timespec timeperiod;
				timeperiod.tv_sec = 0;
				timeperiod.tv_nsec = 50 * 1000 * 1000;
				nanosleep(&timeperiod, NULL);

				timeStarted = getUnixTime();
				timeToStop = timeStarted + (1000 * totalRunSeconds);
				getTime(timeVal);
				if (TUNING)
					printf("OSS  %s: OSS starting clock.  Real start time: %ld  Real stop time: %ld\n", timeVal, timeStarted, timeToStop);
			}

			increment_clock(quantum);

			
			if (dispatchedProcessCount < 1)
				totalCpuIdleTime += (long) quantum;
		}

		getTime(timeVal);
		if (DEBUG && VERBOSE) printf( "OSS  %s: CHILD PROCESS COUNT: %d\nMAX CONC PROCESS COUNT: %d\n", timeVal, childProcessCount, maxConcSlaveProcesses);

		
		if (childProcessCount >= maxConcSlaveProcesses) {
			goClock = 1; 

			getTime(timeVal);
			if (DEBUG && VERBOSE) printf( "OSS  %s: DISPATCHED PROCESS COUNT: %d\nMAX DISPATCHED PROCESS COUNT: %d\n", timeVal, dispatchedProcessCount, maxDispatchedProcessCount);

			
			if (dispatchedProcessCount < maxDispatchedProcessCount)
				dispatchedProcessCount += pcbDispatch(priQueues, priQueueQuantums);

			
			if (p_shmMsg->userPid == 0)
				continue; 

			int pcbIndex = pcbFindIndex(p_shmMsg->userPid);	
			getTime(timeVal);

			if (p_shmMsg->userHaltSignal) { 	
				if (DEBUG) printf("OSS  %s: Child %d is halting at my time %d.%09d\n", timeVal, p_shmMsg->userPid, ossSeconds, ossUSeconds);

				
				pcbUpdateStats(pcbIndex);
				pcbAssignQueue(priQueues, priQueueQuantums, pcbIndex);
				dispatchedProcessCount--;

				
				p_shmMsg->userPid = 0;
				p_shmMsg->userHaltSignal = 0;
				p_shmMsg->userHaltTime = 0;

				
				dispatchedProcessCount += pcbDispatch(priQueues, priQueueQuantums);

				continue;
			} else { 							
				if (DEBUG) printf("OSS  %s: Child %d is terminating at my time %d.%09d\n", timeVal, p_shmMsg->userPid, ossSeconds, ossUSeconds);

				
				pcbUpdateStats(pcbIndex);
				pcbUpdateTotalStats(pcbIndex);
				pcbDelete(pcbMap, pcbIndex);
				dispatchedProcessCount--; 
				childProcessCount--; 

			      
				p_shmMsg->userPid = 0;
				p_shmMsg->userHaltSignal = 0;
				p_shmMsg->userHaltTime = 0;
			}

		}

		getTime(timeVal);
		if (DEBUG && VERBOSE) printf( "OSS  %s: Process %d CHILD PROCESS COUNT: %d\n", timeVal, getpid(), childProcessCount);

		
		if (childProcessCount < maxConcSlaveProcesses) {

			if (goClock && getUnixTime() < nextChildTime){	
				continue;
			}

			int assignedPcb = pcbMapNextAvailableIndex(pcbMap); 
			if (assignedPcb == -1) 								
				continue;

			getTime(timeVal);
			if (DEBUG && VERBOSE)
				printf("OSS  %s: Child (fork #%d from parent) has been assigned pcb index: %d\n", timeVal, totalChildProcessCount, assignedPcb);

			char iStr[1];										
			sprintf(iStr, " %d", totalChildProcessCount);

			char assignedPcbStr[2];								
			sprintf(assignedPcbStr, " %d", assignedPcb);

			childpid = fork();									

			
			if (childpid == -1) {
				perror("master: Failed to fork");
				kill_detach_destroy_exit(1);
				return 1;
			}

			
			if (childpid == 0) {

				getTime(timeVal);
				if (DEBUG) printf( "OSS  %s: Child %d (fork #%d from parent) will attempt to execl worker\n", timeVal, getpid(), totalChildProcessCount);

				int status = execl("./worker", iStr, assignedPcbStr, NULL);

				getTime(timeVal);
				if (status) printf("OSS  %s: Child (fork #%d from parent) has failed to execl worker error: %d\n", timeVal, totalChildProcessCount, errno);

				perror("OSS: Child failed to execl() the command");
				return 1;
			}

			
			if (childpid != 0) {

				pcbAssign(pcbMap, assignedPcb, childpid);		
				push_back(priQueues[hipri], assignedPcb);		

				if (!childrenDispatching) {
					childrenDispatching = 1;
					dispatchedProcessCount += pcbDispatch(priQueues, priQueueQuantums); 
				}

				childpids[totalChildProcessCount] = childpid; 	
				childProcessCount++; 
				totalChildProcessCount++; 
				nextChildTime = getUnixTime() + (((long) rand() % maxWaitInterval) * 1000L);

				getTime(timeVal);
				if (DEBUG && VERBOSE) printf( "OSS  %s: Process %d CHILD PROCESS COUNT: %d\n", timeVal, getpid(), childProcessCount);

				getTime(timeVal);
				if (DEBUG || TUNING)
					printf("OSS  %s: Generating process with PID %d and putting it in queue %d at time %d.%09d\n",
							timeVal, (int) childpid, hipri, ossSeconds, ossUSeconds);
				fprintf(logFile, "OSS  %s: Generating process with PID %d and putting it in queue %d at time %d.%09d\n",
						timeVal, (int) childpid, hipri, ossSeconds, ossUSeconds);

			}

		}

	} 

	fclose(logFile);

	kill_detach_destroy_exit(0);

	return 0;
}


void trim_newline(char *string) {
	string[strcspn(string, "\r\n")] = 0;
}

void signal_handler(int signal) {
	if (DEBUG) printf("\nmaster: Encountered signal! \n\n");
	signalIntercepted = 1;
}

void increment_clock(int offset) {
	const int oneBillion = 1000000000;

	ossUSeconds += offset;

	if (ossUSeconds >= oneBillion) {
		ossSeconds++;
		ossUSeconds -= oneBillion;
	}

	if (0 && DEBUG && VERBOSE)
		printf("master: updating oss clock to %d.%09d\n", ossSeconds, ossUSeconds );
	p_shmMsg->ossSeconds = ossSeconds;
	p_shmMsg->ossUSeconds = ossUSeconds;

}

void kill_detach_destroy_exit(int status) {
	
	for (int p = 0; p < totalChildProcessCount; p++) {
		if (DEBUG) printf("master: oss terminating child process %d  \n", (int) childpids[p]);
		kill(childpids[p], SIGTERM);
	}

	
	shmdt(p_shmMsg);
	shmctl(SHM_MSG_KEY, IPC_RMID, NULL);

	
	sem_unlink(SEM_NAME);
	close_semaphore(sem);
	sem_destroy(sem);

	if (status == 0) printf("Simulation done without issues. \n\n");

	exit(status);
}

int pcbMapNextAvailableIndex(int pcbMap[]) {
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		if (!pcbMap[i])
			return i;
	}
	return -1;
}
 void pcbAssign(int pcbMap[], int index, int pid) {
	 pcbMap[index] = 1;
	 p_shmMsg->pcb[index].pid = pid;

	 getTime(timeVal);
	 if (DEBUG) printf("OSS  %s: Assigning child %d pcb at %d.%09d\n", timeVal, p_shmMsg->pcb[index].pid, ossSeconds, ossUSeconds);
 }

 void pcbDelete(int pcbMap[], int index) {
	 getTime(timeVal);
	 if (DEBUG) printf("OSS  %s: Deleting child %d pcb at %d.%09d\n", timeVal, p_shmMsg->pcb[index].pid, ossSeconds, ossUSeconds);

 	 pcbMap[index] = 0;
 	 p_shmMsg->pcb[index].lastBurstLength = 0;
 	 p_shmMsg->pcb[index].pid = 0;
 	 p_shmMsg->pcb[index].processPriority = 0;
 	 p_shmMsg->pcb[index].totalCpuTime = 0;
 	 p_shmMsg->pcb[index].totalTimeInSystem = 0;
  }

 int pcbFindIndex(int pid) {
	 for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		 if (p_shmMsg->pcb[i].pid == pid) {
			 if (DEBUG) printf("OSS  %s: found pcbIndex: %d\n", timeVal, i);
			 return i;
		 }
	 }
	 return -1;
 }

 int pcbDispatch(int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS], int priQueueQuantums[]) {
 	 for (int i = 0; i < 3; i++) {
 		 if (DEBUG && VERBOSE) printQueue(priQueues[i]);
 		 int pcbIndex = pop(priQueues[i]);
 		 if (pcbIndex > -1) {
 			 p_shmMsg->dispatchedPid = p_shmMsg->pcb[pcbIndex].pid;
 			 p_shmMsg->dispatchedTime = priQueueQuantums[i] - p_shmMsg->pcb[pcbIndex].lastBurstLength;

 			 getTime(timeVal);
 			 printf("OSS  %s: Dispatching process with PID %d from queue %d at time %d.%09d\n", timeVal, p_shmMsg->pcb[pcbIndex].pid, i, ossSeconds, ossUSeconds);
 			 fprintf(logFile, "OSS  %s: Dispatching process with PID %d from queue %d at time %d.%09d\n", timeVal, p_shmMsg->pcb[pcbIndex].pid, i, ossSeconds, ossUSeconds);
 			 return 1;
 		 }
 	 }
 	 return 0;
  }

 void pcbUpdateStats(int pcbIndex) {
	 p_shmMsg->pcb[pcbIndex].lastBurstLength = p_shmMsg->userHaltTime;
	 p_shmMsg->pcb[pcbIndex].totalCpuTime += p_shmMsg->userHaltTime;
	 p_shmMsg->pcb[pcbIndex].totalTimeInSystem = 0;

	 getTime(timeVal);
	 printf("OSS  %s: Total time this dispatch was %d nanoseconds\n", timeVal, p_shmMsg->pcb[pcbIndex].lastBurstLength);
	 fprintf(logFile, "OSS  %s: Total time this dispatch was %d nanoseconds\n", timeVal, p_shmMsg->pcb[pcbIndex].lastBurstLength);
	 fprintf(logFile, "OSS  %s: Receiving that process with PID %d ran for %d nanoseconds\n", timeVal, p_shmMsg->pcb[pcbIndex].pid, p_shmMsg->pcb[pcbIndex].lastBurstLength);
 }

 void pcbAssignQueue(int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS], int priQueueQuantums[], int pcbIndex) {
	 int assignedQueue;
	 if (p_shmMsg->pcb[pcbIndex].totalCpuTime < priQueueQuantums[hipri]) {
		 push_back(priQueues[hipri], pcbIndex); 
		 assignedQueue = hipri;
	 } else if (p_shmMsg->pcb[pcbIndex].totalCpuTime - priQueueQuantums[hipri] < priQueueQuantums[medpri]) {
		 push_back(priQueues[medpri], pcbIndex); 
		 assignedQueue = medpri;
	 } else {
		 push_back(priQueues[lopri], pcbIndex); 
		 assignedQueue = lopri;
	 }

	 getTime(timeVal);
	 printf("OSS  %s: Putting process with PID %d into queue %d \n", timeVal, p_shmMsg->pcb[pcbIndex].pid, assignedQueue);
	 fprintf(logFile, "OSS  %s: Putting process with PID %d into queue %d \n", timeVal, p_shmMsg->pcb[pcbIndex].pid, assignedQueue);
 }

void pcbUpdateTotalStats(int pcbIndex) {
	totalProcesses++;

	
	long long totalSeconds = abs(p_shmMsg->pcb[pcbIndex].endUserSeconds - p_shmMsg->pcb[pcbIndex].startUserSeconds);
	long long totalUSeconds = abs(p_shmMsg->pcb[pcbIndex].endUserUSeconds - p_shmMsg->pcb[pcbIndex].startUserUSeconds);
	totalTurnaroundTime += ((totalSeconds * 1000 * 1000 * 1000) + totalUSeconds);

	
	totalWaitTime += (totalTurnaroundTime - p_shmMsg->pcb[pcbIndex].totalCpuTime);
}

void pcbDisplayTotalStats() {
	printf("Average Turnaround Time: %lli\nAverage Wait Time: %lli\nCPU Idle Time: %lli\n\n", totalTurnaroundTime/totalProcesses, totalWaitTime/totalProcesses, totalCpuIdleTime);
	fprintf(logFile, "Average Turnaround Time: %lli\nAverage Wait Time: %lli\nCPU Idle Time: %lli\n\n", totalTurnaroundTime/totalProcesses, totalWaitTime/totalProcesses, totalCpuIdleTime);
}
