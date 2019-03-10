// semaphore test
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
#include <semaphore.h>



/* example thread */
int g_count=0;
sem_t main_sem;
int sem_data=0;

void *thread1(void *arg)
{
sem_wait(&main_sem);

printf("\nthread 1: %d(%d)", g_count, pthread_self());


sem_data=sem_data + 10;
printf("\nsem_data=sem_data+10");
sleep(5);

sem_post(&main_sem);
	
g_count++;
while (1);

return NULL;
}

void *thread2(void *arg)
{
int first=1;

sem_wait(&main_sem);
printf("\nthread 2: %d(%d)", g_count, pthread_self());

g_count++;
sleep(1);

sem_data=sem_data* 2;
printf("\nsem_data=sem_data*2");

sem_post(&main_sem);
	
return NULL;
}

void *thread3(void *arg)
{       
int first=0;
void **value_ptr;

   while (1) {       
	printf("\nthread 3: %d(%d)", g_count, pthread_self());
	sem_wait(&main_sem);
	sem_data++;
	sem_post(&main_sem);
	g_count++;
	sleep(2);
   }
return NULL;
}


int main(int argc, char **argv)
{
pthread_t t1;
pthread_t t2;
pthread_t t3;
int first=0;

sem_data=0;
printf("\nsem_data=%d", sem_data);

sem_init(&main_sem, 0, 1);

pthread_create(&t1, NULL, thread1, NULL);
pthread_create(&t2, NULL, thread2, NULL);
pthread_create(&t3, NULL, thread3, NULL);

while (1) {
printf("\nmain: sem_data=%d", sem_data);
sleep(2);
}

sem_destroy(&main_sem);

printf("\n");
}
