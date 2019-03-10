// 1. I have many troubles in pthread_exit because gardbage collection
//    So I modify that codes with refering sample codes
// 2. And I convert my codes into c++ for using queue library
// 3. I am learned of timer handling for reference source
//    ITIMER_REAL help making SIGALRM

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <setjmp.h> // setjmp, longjmp

#include <sys/time.h>
#include <time.h>
#include <signal.h> // ALARM signal related

#include <stdint.h> // uintptr_t

#include <string.h>
#include <queue>
#include <deque>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>

#define JB_BX 0
#define JB_SI 1
#define JB_DI 2
#define JB_BP 3
#define JB_SP 4
#define JB_PC 5

#define INTERVAL 50
#define STACK_SIZE 32767

#define YOO_DEBUG 0

using namespace std;
/*
 * ptr mangle ; for security reasons
 * just do mangle
 */
static int ptr_mangle(int p)
{
    unsigned int ret;
    __asm__(" movl %1, %%eax;\n"
        " xorl %%gs:0x18, %%eax;"
        " roll $0x9, %%eax;"
        " movl %%eax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%eax"
    );
    return ret;
}

template<typename T, typename Container=std::deque<T> >
class iterable_queue : public std::queue<T,Container>
{
public:
    typedef typename Container::iterator iterator;
    typedef typename Container::const_iterator const_iterator;

    iterator begin() { return this->c.begin(); }
    iterator end() { return this->c.end(); }
    const_iterator begin() const { return this->c.begin(); }
    const_iterator end() const { return this->c.end(); }
};


void tswitcher(int signo);
void exitf_thread(void);
void schedule();
void lock();
void unlock();

void pthread_exit_wrapper();

/* for signal blocking when switching thread */
/* for making lock and unlock */
sigset_t block_sig;

//static struct timeval tv1,tv2;
static struct itimerval itimer = {0};
static struct itimerval ctimer = {0};
static struct itimerval null_timer = {0};
static struct sigaction act;

/* 50 ms timer
 * This timer routine can start and stop SIGALRM
 * This control is much better than ualarm()
 */

static void pause_tswitcher()
{
  // ITIMER_REAL can make SIGALRM,  make ctimer null
  setitimer(ITIMER_REAL,&null_timer,&ctimer);
}

static void resume_tswitcher()
{
  // ITIMER_REAL can make SIGALRM, makes ctimer active
  setitimer(ITIMER_REAL,&ctimer,NULL);
}

static void start_tswitcher()
{
  // ITIMER_REAL can make SIGALRM, makes ctimer <- itimer 50ms
  ctimer = itimer; 
  setitimer(ITIMER_REAL,&ctimer,NULL);
}

static void stop_tswitcher()
{
  // ITIMER_REAL can make SIGALRM
  // stop timer
  setitimer(ITIMER_REAL,&null_timer,NULL);
}

struct _thread {
  pthread_t id;
  jmp_buf env;
  char *stack;
  deque<struct _thread> wait_pool; // for join
  void *exit_value;
};
typedef struct _thread  THREAD; 
static deque<THREAD> thread_pool;


THREAD thread_pool_pop(pthread_t id)
{
for (auto it=thread_pool.begin(); it!=thread_pool.end();it++) {
  if (id==it->id) {
	thread_pool.erase(it);
	return *it;
  } // end if
} // end for
}

struct _sem {
  int sem_id;
  sem_t *sem;
  int pshared;
  unsigned value;
  deque<THREAD> thread_pool;
};
#define SEM_VALUE_MAX 65536
typedef struct _sem  SEM; 
static deque<SEM> sem_pool;

int sem_id_count=0;

//typedef struct {
//  unsigned int init_param;
//  volatile unsigned int counter_param;
//} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
 SEM new_sem;
 //sem->init_param=sem_id_count++;
 //sem->counter_param=value;
 // ERROR handling
 if (pshared!=0) return ENOSYS;
 if (value>SEM_VALUE_MAX) return EINVAL;
 if (sem==NULL) return -1;

 sem->__align=sem_id_count++; // __align is used for saving id

 new_sem.sem_id=sem->__align;
 new_sem.pshared=pshared;
 new_sem.value=value;
 sem_pool.push_back(new_sem);
 return 0;
}

int sem_wait(sem_t *sem)
{
 //THREAD *current=NULL;
 // find current thread
 // ERROR handling
 int found=0;
 if (sem==NULL) return EINVAL;
 
lock();
 auto it=thread_pool.begin();
 auto itsem=sem_pool.begin();
#if YOO_DEBUG
 printf("\nsem_wait:thread:%d", pthread_self());
#endif
 for (it=thread_pool.begin(); it!=thread_pool.end(); ++it) {
   if (it->id==pthread_self()) {
#if YOO_DEBUG
        printf("\n%d is try to get sem_wait", it->id);
#endif
	break;
   } //end if
 } // end for 
 // semaphore operation
 for (itsem=sem_pool.begin(); itsem!=sem_pool.end();itsem++) {
    if (sem->__align==itsem->sem_id) {
#if YOO_DEBUG
        printf("\nsem_id(%d), value:%d", itsem->sem_id, itsem->value);
#endif
 	if (itsem->value>0) { // will lock
	   itsem->value--; 
#if YOO_DEBUG
           printf("\nsem_wait is ok(sem_id:%d", itsem->sem_id);
#endif
	} else if (itsem->value==0) { // blocked
	   //itsem->thread_pool.push_back(*it); 
	   //thread_pool.erase(it);
	   //how about the below?
    	   itsem->thread_pool.push_back(thread_pool.front());
     	   //thread_pool.pop_front();
	   
    //thread_pool.push_back(thread_pool.front());
    //thread_pool.pop_front();
#if YOO_DEBUG
           printf("\n%d is queued to sem_id(%d)", it->id, itsem->sem_id);
#endif
	   unlock();
	   schedule();
	}
	unlock();
	return 0;
    } // end if
  } // end for 
 unlock();
 return EINVAL;
}

int sem_post(sem_t *sem)
{
#if 0
 THREAD *current=NULL;
 // find current thread
 for (auto it=thread_pool.begin(); it!=thread_pool.end(); ++it) {
   if (it->id==pthread_self()) {
	current=it;
	break;
   } //end if
 } // end for 
#endif
 if (sem==NULL) return EINVAL;
lock();

#if YOO_DEBUG
 printf("\nsem_post:thread:%d", pthread_self());
#endif
 // semaphore operation
 for (auto itsem=sem_pool.begin(); itsem!=sem_pool.end();itsem++) {
    if (sem->__align==itsem->sem_id) {
        if (itsem->thread_pool.size()>0) {
	   thread_pool.push_back(itsem->thread_pool.front());
	   itsem->thread_pool.pop_front();
	   itsem->value++;
	} else {
	   itsem->value++;
	}
	unlock();
	return 0;
    } // end if
  } // end for 
unlock();
return EINVAL;
}
 
int sem_destroy(sem_t *sem)
{
int found;
if (sem==NULL) return EINVAL;
found=0;
 for (auto itsem=sem_pool.begin(); itsem!=sem_pool.end();itsem++) {
    if (sem->__align==itsem->sem_id) {
	   if (itsem->thread_pool.size()>0) {
		return -1; // error
	   } else {
		sem_pool.erase(itsem);
		return 0;
	   }
	found=1;
    }
 } // end for 
if (found==0) return EINVAL;
 return 0;
}

static THREAD init_thread;
static THREAD exit_thread;

/* for assigning id */
static unsigned long id_counter = 1; 
/* we initialize in pthread_create only once */
static int pthread_initialized = 0;

/*
 * pthread_init()
 *
 * Initialize
 * 1. timer
 * 2. signal - SIGALRM
 * 3. start and activate
 */
void pthread_init()
{
        // when alarm, do switch thread to another
  act.sa_handler = tswitcher;
  sigemptyset(&act.sa_mask); // signal masking
  act.sa_flags = SA_NODEFER; // for real time use

  if(sigaction(SIGALRM, &act, NULL) == -1) {
	printf("\nSIGALRM fail");
	exit(1);
  }

// for blocking signal
sigemptyset(&block_sig);
sigaddset(&block_sig, SIGALRM);


// itimer makes SIGALRM
itimer.it_value.tv_sec = INTERVAL/1000;
itimer.it_value.tv_usec = (INTERVAL*1000) % 1000000;
itimer.it_interval = itimer.it_value;

init_thread.id = 0;
init_thread.stack = NULL;
	
/* front of thread_pool is the active thread */
thread_pool.push_back(init_thread);

exit_thread.id = 128;
exit_thread.stack = (char *) malloc (STACK_SIZE);

memset(&exit_thread.env,0,sizeof(exit_thread.env));
sigemptyset(&exit_thread.env->__saved_mask);

exit_thread.env->__jmpbuf[JB_SP] = ptr_mangle((uintptr_t)(exit_thread.stack+STACK_SIZE-8));
exit_thread.env->__jmpbuf[JB_PC] = ptr_mangle((uintptr_t)exitf_thread);

/* start timer for making signal */
start_tswitcher();
pause(); /* wait for signal */
}


int find_thread(pthread_t tid) 
{
 for (auto it=thread_pool.begin(); it!=thread_pool.end(); ++it) {
   if (it->id==tid) {
   } //end if
 } // end for 
return 0;
}

/* 
 * pthread_join()
 * 
 */
int pthread_join(pthread_t thread, void **value_ptr) 
{
 for (auto it=thread_pool.begin(); it!=thread_pool.end(); ++it) {
   if (it->id==thread) {
#if YOO_DEBUG
	printf("\njoin id: %d is attached to %d", pthread_self(), it->id);
#endif
	//thread_pool.erase(*it);
	//thread_pool.front().wait_pool.push_back(*it);
	it->wait_pool.push_back(thread_pool.front());
#if YOO_DEBUG
	printf("\nid(%d) is push_back to wait_pool", thread_pool.front().id);
#endif
	thread_pool.pop_front();
	break;
   }
 } // end for 
  //thread.wait_pool.push(pthread_self()); 
return 0;
}

/* 
 * pthread_create()
 * 
 */
int pthread_create(pthread_t *restrict_thread, const pthread_attr_t *restrict_attr, void *(*start_routine)(void*), void *restrict_arg) 
{
	
  /* set up init_thread and timer and so on */
  if(!pthread_initialized) {
     pthread_initialized = 1;
     pthread_init();
  }

  pause_tswitcher(); // for no intercept

  THREAD new_tcb;
  new_tcb.id = id_counter++;
  *restrict_thread = new_tcb.id;

  new_tcb.stack = (char *)malloc(STACK_SIZE);

  *(int*)(new_tcb.stack+STACK_SIZE-4) = (int)restrict_arg;
  // after function exit, execute pthread_exit()
  //*(int*)(new_tcb.stack+STACK_SIZE-8) = (int)pthread_exit;
  *(int*)(new_tcb.stack+STACK_SIZE-8) = (int)pthread_exit_wrapper;
	
  memset(&new_tcb.env,0,sizeof(new_tcb.env));
  sigemptyset(&new_tcb.env->__saved_mask);
	
  new_tcb.env->__jmpbuf[JB_SP] = ptr_mangle((uintptr_t)(new_tcb.stack+STACK_SIZE-8));
  new_tcb.env->__jmpbuf[JB_PC] = ptr_mangle((uintptr_t)start_routine);

  thread_pool.push_back(new_tcb); // insert new tcb into deque
    
  resume_tswitcher();

  return 0;	
}



/* 
 * pthread_self()
 *
 * just return the current thread's id
 * undefined if thread has not yet been created
 * (e.g., main thread before setting up thread subsystem) 
 */
pthread_t pthread_self(void) {
  if(thread_pool.size() == 0) {
	return 0;
  } else {
	return (pthread_t)thread_pool.front().id;
  }
}

void pthread_exit_wrapper()
{
  unsigned int res;
  asm("movl %%eax, %0\n":"=r"(res));
  
  for (auto it=thread_pool.begin(); it!=thread_pool.end(); ++it) {
    if (it->id==pthread_self()) {
#if YOO_DEBUG
        printf("\n%d is try to get sem_wait", it->id);
#endif
	it->exit_value=(void *)res;
	break;
    } //end if
 } // end for 
  pthread_exit((void *)res);
}

/* 
 * pthread_exit()
 *
 * pthread_exit will execute  after start_routine finishes
 */
void pthread_exit(void *value_ptr) {

if(pthread_initialized == 0) {
	exit(0);
}

/* for not geting interrupted */
stop_tswitcher();

if(thread_pool.front().id == 0) {
  init_thread = thread_pool.front();
  if(setjmp(init_thread.env)) {
	free((void*) exit_thread.stack);
	exit(0);
  } // end if 
} // end if
longjmp(exit_thread.env,1); 
}

/* lock()
 * disable SIGALARM
 * not getting interrupted
 */
 
void lock()
{
  if (sigprocmask(SIG_BLOCK, &block_sig, NULL)!=0) {
     printf("\nlock() fail\n");
  }
}

/* unlock()
 * enable SIGALARM
 * getting interrupted
 */

void unlock()
{
  if (sigprocmask(SIG_UNBLOCK, &block_sig, NULL)!=0) {
     printf("\nlock() fail\n");
  }
}

void print_runqueue(deque<THREAD> tmp_thread_pool)
{
  deque<THREAD> copy_thread_pool;
  copy_thread_pool = tmp_thread_pool;
#if YOO_DEBUG
  printf("\nrunqueue: ");
#endif
  while (!copy_thread_pool.empty()) {
#if YOO_DEBUG
     printf("  %d", copy_thread_pool.front().id);
#endif
     copy_thread_pool.pop_front();
  }
#if YOO_DEBUG
  printf("\n");
#endif
}

/* 
 * tswitcher()
 * called when receiving SIGALRM
 */
void tswitcher(int signo) 
{ 
  if(thread_pool.size() <= 1) { return; }

  print_runqueue(thread_pool); 
	
  if(setjmp(thread_pool.front().env) == 0) { /* switch threads */
    // use setjmp as changing thread
#if YOO_DEBUG
    printf("ts: %d\n", thread_pool.front().id);
#endif
    thread_pool.push_back(thread_pool.front());
    thread_pool.pop_front();
    longjmp(thread_pool.front().env,1);
  } // end if
  return;
}

void schedule()
{
  print_runqueue(thread_pool); 

pause_tswitcher();
  if(setjmp(thread_pool.front().env) == 0) { /* switch threads */
    // use setjmp as changing thread
#if YOO_DEBUG
    printf("schedule: %d\n", thread_pool.front().id);
#endif
    //thread_pool.push_back(thread_pool.front());
    thread_pool.pop_front();
    resume_tswitcher();
    longjmp(thread_pool.front().env,1);
  } // end if
  return;
}



/* 
 * exitf_thread()
 * I experienced many toubles because of pthread_exit() 
 * This exit_thread helps solving pthread_exit() error
 */
void exitf_thread(void)
{
  // free init 
  free((void*) thread_pool.front().stack);
  thread_pool.front().stack = NULL;
#if YOO_DEBUG
  printf("\n%d is deleted", thread_pool.front().id);
#endif
  // thread exited point
  // make waited thread live
  for (auto it=thread_pool.front().wait_pool.begin(); it!=thread_pool.front().wait_pool.end();it++) {
          thread_pool.push_back(*it);
#if YOO_DEBUG
	printf("\n%d is push_back to thread_pool", it->id);
#endif
  } // end for 

  thread_pool.pop_front();

  if(thread_pool.size() == 0) { // no thread in pool mean this is last 
     longjmp(init_thread.env,1);
  } else {
     // will do rest thread
     start_tswitcher();
     longjmp(thread_pool.front().env,1);
  } // end if
}

