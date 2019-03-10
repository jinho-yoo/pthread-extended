#define YOO_DEBUG 0
// for debug message

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>

/* example thread */
int g_count=0;

void *thread1(void *arg)
{

  while (1) {       
	printf("\nthread 1: %d", g_count);
	g_count++;
	sleep(1);
  }
}

void *thread2(void *arg)
{       
   while (1) {       
        printf("\nthread 2: %d", g_count);
	g_count++;
	sleep(1);
   }
}
void *thread3(void *arg)
{       
   while (1) {       
        printf("\nthread 3: %d", g_count);
	g_count++;
	sleep(1);
   }
}


int main()
{
pthread_t t1;
pthread_t t2;
pthread_t t3;
int first=0;


pthread_create(&t1, NULL, thread1, NULL);
pthread_create(&t2, NULL, thread2, NULL);
pthread_create(&t3, NULL, thread3, NULL);

while (1) {
printf("\nmain: g_count=%d", g_count);
sleep(2);
}


printf("\n");

}
