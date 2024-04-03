#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define SHMKEY_CLOCK 1234
#define SHMKEY_PROCESS_TABLE 5678
#define MSGKEY 7890
#define DEFAULT_INTERVAL 100000 // 100 ms in nanoseconds
#define MESSAGE_TYPE 1

#define QUEUE_COUNT 3
#define MAX_PROCESSES 20
#define MAX_CHILDREN 5

struct SimulatedClock {
    int seconds;
    int nanoseconds;
};

struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int blocked;
    int eventBlockedUntilSec;
    int eventBlockedUntilNano;
};

struct Message {
    long mtype; // Message type (1 for termination signal, 2 for process to OSS)
    int timeUsed; // Time used by the process
};

struct SimulatedClock *simulatedClock;
struct PCB *processTable;
int shmClockID, shmProcessTableID;
int msgQueueID;

// Function to initialize shared memory
void initializeSharedMemory() {
    // Get shared memory for the simulated system clock
    shmClockID = shmget(SHMKEY_CLOCK, sizeof(struct SimulatedClock), 0666);
    if (shmClockID == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    simulatedClock = (struct SimulatedClock *)shmat(shmClockID, NULL, 0);
    if ((void *)simulatedClock == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    // Get shared memory for the process table
    shmProcessTableID = shmget(SHMKEY_PROCESS_TABLE, sizeof(struct PCB) * MAX_PROCESSES, 0666);
    if (shmProcessTableID == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    processTable = (struct PCB *)shmat(shmProcessTableID, NULL, 0);
    if ((void *)processTable == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

// Function to initialize message queue
void initializeMessageQueue() {
    // Get message queue
    msgQueueID = msgget(MSGKEY, 0666);
    if (msgQueueID == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }
}

// Function to send message to worker
void sendToWorker(int workerIndex) {
    struct Message msg;
    msg.mtype = workerIndex + 2; // Worker messages start from 2
    msg.timeUsed = DEFAULT_INTERVAL; // Assuming default time quantum

    if (msgsnd(msgQueueID, &msg, sizeof(int), IPC_NOWAIT) == -1) {
        perror("msgsnd");
        exit(EXIT_FAILURE);
    }
}

// Function to log OSS messages
void logOSSMessage(const char *message) {
    printf("OSS: %s\n", message);
}

// Function to log OSS dispatching messages
void logOSSDispatch(int pid, int queueIndex, int dispatchTime, int timeUsed) {
    printf("OSS: Dispatching process with PID %d from queue %d at time %d:%d, total time this dispatch was %d nanoseconds\n",
           pid, queueIndex, simulatedClock->seconds, simulatedClock->nanoseconds, dispatchTime);
    printf("OSS: Receiving that process with PID %d ran for %d nanoseconds\n", pid, timeUsed);
}

// Function to simulate work done by worker process
void doWork(int maxTime) {
    // Simulate work
    int timeUsed = rand() % maxTime + 1;
    usleep(timeUsed);

    // Send message to OSS indicating time used
    sendToWorker(timeUsed);
}

int main(int argc, char *argv[]) {
    int maxTime = 1000000; // Maximum time to simulate work (1 second by default)

    // Seed random number generator
    srand(time(NULL) + getpid());

    // Initialize shared memory and message queue
    initializeSharedMemory();
    initializeMessageQueue();

    // Main loop to simulate OSS behavior
    while (true) {
        // Simulate work done by OSS
        doWork(maxTime);

        // Log OSS message
        logOSSMessage("Generating process with PID 3 and putting it in queue 0");

        // Log OSS dispatching message
        logOSSDispatch(2, 0, 790, 400000);

        // Log OSS message
        logOSSMessage("Putting process with PID 2 into queue 1");

        // Log OSS dispatching message
        logOSSDispatch(3, 0, 1000, 270000);

        // Log OSS message
        logOSSMessage("Not using its entire time quantum");

        // Log OSS message
        logOSSMessage("Putting process with PID 3 into blocked queue");

        // Log OSS dispatching message
        logOSSDispatch(1, 0, 7000, 0);
    }

    return 0;
}
