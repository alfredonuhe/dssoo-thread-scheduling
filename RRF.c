#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"
#include "queue.h"

long hungry = 0L;

TCB* scheduler();
void activator();
void timer_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;
/* Declaración de las colas de hilos de baja y alta prioridad */
struct queue *lowq;
struct queue *highq;

/* Initialize the thread library */
void init_mythreadlib() {
  int i;
/* Inicialización de las colas*/
  lowq=queue_new();
  highq=queue_new();
/* Inicialización del t_state y activación de la interrumpción del temporizador */
  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(5);
  }	
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
  t_state[0].tid = 0;
  running = &t_state[0];
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority)
{

  int i;
  /* Se comprueba que la prioridad introducida tiene un valor correcto */
  if(priority!=0 && priority!=1){
	perror("Prioridad incorrecta");
  	exit(-1);
  }
  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;      
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;	
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr, 1);  
  TCB *t= &t_state[i];

  /* Inserción del hilo creado en la cola que corresponda. 
     Si el hilo creado tiene prioridad baja se inserta en la cola de baja prioridad.
     Si la prioridad el hilo creado es alta su inserción dependerá de la prioridad del hilo en ejecución de forma que si
     el hilo en ejecución es de prioridad baja el hilo creado no se encola sino que se activa, interrumpiendo la ejecución del hilo ejecutándose, 
     si por el contrario el hilo en ejecución es de alta prioridad, el hilo creado se inserta en la cola de alta prioridad. */
  
  if(t->priority==0){
     disable_interrupt();
     enqueue(lowq,t);
     enable_interrupt();
  }
  else{

     if(running->priority==0){
           activator(t);
     }
     else{
           disable_interrupt();
           enqueue(highq,t);
           enable_interrupt();
     }
  }

  return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	
  printf("*** THREAD %d FINISHED\n", tid);	
  running->state = FREE;
  free(running->run_env.uc_stack.ss_sp);
  TCB* next=scheduler();
  activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  /* Se comprueba que la prioridad introducida tiene un valor correcto */
  if(priority!=0 && priority!=1){
	perror("Prioridad incorrecta");
  	exit(-1);
  }
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}

/* Timer interrupt */


/* Interrupción del temporizador: esta función controlará que los procesos de baja prioridad se ejecuten sólo durante una rodaja de tiempo.
   Si el proceso es de alta prioridad esta función no afecta a su ejecución, puesto que estos procesos no se ejecutan en rodajas. */

void timer_interrupt(int sig)
{

  if(running->priority==0){  
	      // Si el proceso ejecutándose es de baja prioridad, en cada llamada a timer_interrupt se reduce el número de ticks del hilo en 1
	      running->ticks=running->ticks-1;
   
 	      /* Cuando el número de ticks llega a 0 significa que el hilo en ejecución ha terminado su rodaja.
                 Por tanto, se llama al planificador para obtener el próximo hilo a ejecutar y el activador lo pone en ejecución a no ser
                 que el hilo que devuelve el planificador sea el mismo que está en ejecución, en este caso el hilo no se activa
                 (puesto que ya está activado) sino que se restaura su valor de ticks a QUANTUM_TICKS y continua su ejecución. */
	      if(running->ticks==0){
                      TCB* next=scheduler();
                      if(current!=next->tid){   
                          activator(next);
                      }
                      else{
                          running->ticks=QUANTUM_TICKS;
                      }
	      }
  }
} 



/* Scheduler: returns the next thread to be executed */

/* Planificador de procesos: Devuelve el siguiente hilo que se va a ejecutar.*/
TCB* scheduler(){

        /* Primero se comprueba si hay algún hilo en la cola de alta prioridad.
           Si lo hay, devolverá el primer hilo de la cola, siendo este hilo el próximo en ponerse en ejecución. 
           Si la cola de alta prioridad está vacía, se pasa a comprobar la cola de baja prioridad. */
	if(queue_empty(highq)==0){
  		disable_interrupt();
		TCB *t=dequeue(highq);
		enable_interrupt();
		return t;
	}
        /* Cuando no quedan hilos de alta prioridad por ejecutar, se procesa la cola de baja prioridad.
           Si esta cola no está vacía se devolverá el primer hilo de la cola, siendo este el próximo en ponerse en ejecución. 
           Si la cola está vacía se pasa a comprobar si queda algún hilo en ejecución. */
	if(queue_empty(lowq)==0){
  		disable_interrupt();
		TCB *t=dequeue(lowq);
		enable_interrupt();
		return t;
	}

	/* Si la ejecución llega aquí significa que no quedan hilos esperando para ponerse en ejecución, sin embargo,
           puede ser que haya un hilo en ejecución que no haya terminado aún, si ese es el caso se devuelve dicho hilo 
           para que la ejecución continúe. Si no, el programa termina. */
	if(running->state==INIT){
               return running;
        }

  printf("FINISH\n");	
  exit(1);

}

/* Activator */

/* Activador de hilos: esta función activa el hilo que recibe por parámetro haciendo un cambio de contexto del hilo en ejecución a dicho hilo.*/

void activator(TCB* next){
        TCB *prev=running;
        int prev_tid= current;
        // El hilo que se desea activar es considerado ahora el hilo en ejecución. 
        running= next;
        current=running->tid;
        // Antes de activar el hilo, se restaura su valor de ticks.
        running->ticks=QUANTUM_TICKS;


        /* Si el hilo que deja de ejecutarse ha finalizado su función el cambio de contexto al nuevo hilo se realiza con un setcontext(). */
        if(prev->state==FREE){
                printf("*** THREAD %i FINISHED: SET CONTEXT OF %i\n", prev_tid, current);
		if(setcontext (&(next->run_env))==-1){
		     perror("Error al hacer el cambio de contexto\n");
		} 

        /* En caso de que no se cumpla lo anterior, si el hilo que deja de ejecutarse tiene prioridad baja y el que va a pasar a ejecutarse 
           tiene prioridad alta, significa que el de alta ha expulsado al de baja, por ello el hilo que deja de 
           ejecutarse se encola en la cola de baja prioridad y se hace el cambio de contexto con swapcontext(). */
	}else if(prev->priority==0 && next->priority==1){
                disable_interrupt();
                enqueue(lowq, prev);
		enable_interrupt();
		printf("*** THREAD %i EJECTED: SET CONTEXT OF %i\n", prev_tid, current);
		if(swapcontext (&(prev->run_env),&(next->run_env))==-1){
		     perror("Error al hacer el cambio de contexto\n");
		} 
        /* Si no se cumple nada de lo anterior, el cambio de contexto es a un hilo de baja prioridad. Se encola en hilo que deja de ejecutarse
           en la cola que corresponda y el cambio de contexto se hace con un swapcontext(). */
  	} else{
                if(prev->priority==0){
                      disable_interrupt();
                      enqueue(lowq, prev);
		      enable_interrupt();
                }
                else{
                      disable_interrupt();
                      enqueue(highq, prev);
		      enable_interrupt();
		}
		printf("*** SWAPCONTEXT FROM %i to %i\n", prev_tid, current);
		if(swapcontext (&(prev->run_env), &(next->run_env))==-1){
		     perror("Error al hacer el cambio de contexto\n");
		}	
        } 

}



