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
#define DEFAULT_PROBABILITY 10
#define MESSAGE_TYPE 1

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
    shmProcessTableID = shmget(SHMKEY_PROCESS_TABLE, sizeof(struct PCB) * 20, 0666);
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

// Function to send message to OSS
void sendToOSS(int timeUsed) {
    struct Message msg;
    msg.mtype = MESSAGE_TYPE;
    msg.timeUsed = timeUsed;

    if (msgsnd(msgQueueID, &msg, sizeof(int), IPC_NOWAIT) == -1) {
        perror("msgsnd");
        exit(EXIT_FAILURE);
    }
}

// Function to simulate work done by worker process
void doWork(int maxTime) {
    // Simulate work
    int timeUsed = rand() % maxTime + 1;
    usleep(timeUsed);

    // Send message to OSS indicating time used
    sendToOSS(timeUsed);
}

int main(int argc, char *argv[]) {
    int probability = DEFAULT_PROBABILITY;
    int maxTime = 1000000; // Maximum time to simulate work (1 second by default)
    int opt;

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "hp:t:")) != -1) {
        switch (opt) {
            case 'h':
                // Help option
                printf("Usage: %s [-h] [-p probability] [-t maxtime]\n", argv[0]);
                exit(EXIT_SUCCESS);
            case 'p':
                // Probability of termination
                probability = atoi(optarg);
                break;
            case 't':
                // Maximum time to simulate work
                maxTime = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-h] [-p probability] [-t maxtime]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Seed random number generator
    srand(time(NULL) + getpid());

    // Initialize shared memory and message queue
    initializeSharedMemory();
    initializeMessageQueue();

    // Main loop to simulate worker process
    while (true) {
        // Simulate work done by worker process
        doWork(maxTime);

        // Check probability of termination
        if (rand() % 100 < probability) {
            // Terminate worker process
            printf("Worker PID %d terminated.\n", getpid());
            exit(EXIT_SUCCESS);
        }
    }

    return 0;
}
