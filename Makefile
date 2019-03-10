CC=g++
all:threads.o


threads.o: threads.cpp
	$(CC) -m32 -c threads.cpp
   
clean:
	if [ -f threads.o ]; then \rm threads.o; fi
