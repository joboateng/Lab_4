CC=gcc
all: oss worker 

%.o: %.c 
	$(CC) -c  -w $<

oss: oss.o SM.o times.o queue.o
	gcc -o oss oss.o SM.o times.o queue.o -pthread
	
worker: worker.o SM.o times.o queue.o
	gcc -o worker worker.o SM.o times.o queue.o -pthread

clean:
	rm oss worker *.o *.out
