// File:    my_pthread.c
// Author:  Clifford Imhomoh
// Date:    09/23/2017


#include "my_pthread_t.h"

// a pointer to the current executing thread
tcb * current_thread = NULL; 
int number_of_threads = 0;
// keep track of number of interrupts for maintenence cycle
unsigned int num_interrupts = 0;

void init_main () {

    tcb * main_block = (tcb*)malloc(sizeof(tcb)); 
    main_block->thread_context = (ucontext_t*)malloc(sizeof(ucontext_t)); 
    main_block->priority = 0; 
    main_block->tid = ++number_of_threads; 
    main_block->thread_state = RUNNING; 
    main_block->run_time = 0; 

    //capture the context and set the current running thread to the main block 
    getcontext(main_block->thread_context); 
    current_thread = main_block; 

    // add main block to the queue of threads
   // Insert_to_qeueue(main_block); 
}


tcb * get_running_thread () {

    return current_thread;
}




// create a new thread
int my_pthread_create(my_pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg, int priority) {

    // First time calling pthread_create
    if ( Pqueue[0] == NULL ) {

        // first initialize the thread that calls pthread exit for all other threads
        getcontext(&exit_caller); 

        exit_caller.uc_link = 0; 
        exit_caller.uc_stack.ss_sp = malloc(STACK_SIZE);

        if ( exit_caller.uc_stack.ss_sp == NULL ){

            printf("Malloc could not allocate memory\n");
            exit(1); 
        }
        // may also need to set flags
        exit_caller.uc_stack.ss_size = STACK_SIZE;
        // function pointer to pthread exit
        void (*exit)(void*) = &my_pthread_exit;  
        makecontext(&exit_caller, (void*)exit, 1, NULL); 

        // initialize list of return values
        return_list = (ret_list*)malloc(sizeof(ret_list));
        return_list->head = NULL; 
        return_list->tail = NULL; 

        // initialize the waitQueue
        WaitQueue = (queue*)malloc(sizeof(queue)); 
        WaitQueue->head = NULL; 
        WaitQueue->tail = NULL; 
        my_queue_init(); // initialize queue
        init_main();     // initialize main context

        count_lock = (my_pthread_mutex_t*)malloc(sizeof(my_pthread_mutex_t)); 
        my_pthread_mutex_init(count_lock,NULL); 
    }

    // time to create the new thread

    ucontext_t * new_thread = (ucontext_t*)malloc(sizeof(ucontext_t));
    getcontext(new_thread);  

    if ( new_thread == NULL ) {

        printf("Malloc could not allocate anymore memory\n");
        exit(1); 
    }

    new_thread->uc_stack.ss_sp = malloc(STACK_SIZE);

    if ( new_thread->uc_stack.ss_sp == NULL ){

        printf("malloc could not allocate more memory\n");
        exit(1); 
    }

    // initialize fields of this newly created thread
    new_thread->uc_stack.ss_size = STACK_SIZE; 
    new_thread->uc_stack.ss_flags = 0;  
    new_thread->uc_link = &exit_caller; 

    if ( arg ) {

        makecontext(new_thread, (void*)function, 1, arg); 
    }

    else {

        makecontext(new_thread, (void*)function,0); 
    }

    // create a thread control block for this thread
    my_pthread_mutex_lock(count_lock);
    *thread = ++number_of_threads; 
    my_pthread_mutex_unlock(count_lock); 
    tcb * block = (tcb*)malloc(sizeof(tcb));
    
    if ( priority != 0 )
        block->priority = priority; 
    else
    block->priority = 0; 

    block->thread_context = new_thread; 
    block->run_time = 0; 
    block->join = 0; 
    block->thread_state = READY;
    block->tid = *thread;  

    // add the new block to the priority queue

    Insert_to_qeueue(block); 

    // call my pthread_yield to begin scheduling
    my_pthread_yield(); 

    return 0;
};


/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield() {
    // when going into the scheduler, we must first disable the timer
 //    printf("yielding\n");
    struct itimerval timer; 
    getitimer(ITIMER_REAL, &timer); 

    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL); 

    schedule_threads(); 

    return 0;
};


void schedule_threads () {

    struct itimerval timer; 
    struct sigaction interrupt; 

    tcb * context = get_running_thread(); 

    /* priority boost for every thirty interrupts
     when the number of threads is greater than 500, the overhead of boosting all threads becomes costly */
    
  /*  if ( number_of_threads < 5000 ){

        if ( num_interrupts%30 == 0 ){

            priority_boost(); 
        }  

    } */

    //update the time quanta and modify priorities

    if ( context-> priority == 0 ) {

        context->run_time += 15; 

    }

    else if ( context->priority == 1 ) {

        context->run_time += 30; 
    }

    // the priority is level 2
    else {

        context->run_time += 60; 

        // priority boost
        if ( context->run_time >= 225 ) {

            context->priority = 0; 
            context->run_time = 0; 
        }
    }

    // check if the thread as exited and free all of its resources

    if ( context->thread_state == EXITED ) {

        free( context->thread_context->uc_stack.ss_sp);  // free the stack memory
        free(context->thread_context); 
        // may need to change
        free(context);
        context = NULL;              
    }


    else {
        // only add back to run queue if the thread is not waiting for some IO
        if ( context != NULL && context->thread_state != WAITING ) {

            if ( context->priority < MAX_PRIORITY )
                context->priority++; 

            // put the context back into the queue
            context->thread_state = READY;
            Insert_to_qeueue(context);
        }
    }

    // get the next thread to run /  initialize time
    memset(&interrupt, 0, sizeof(interrupt)); 
    interrupt.sa_handler = &timer_interrupt; 
    sigaction(SIGALRM, &interrupt, NULL); 

    tcb * to_run = get_tcb(); 

    // no more threads to schedule
    if ( to_run == NULL )
        return; 

    if ( to_run->priority == 0 ) {

        timer.it_value.tv_sec = 0; 
        timer.it_value.tv_usec = LEVEL1; 

        timer.it_interval.tv_sec  = 0; 
        timer.it_interval.tv_usec = LEVEL1; 
    }


    else if ( to_run->priority == 1 ) {

        timer.it_value.tv_sec = 0; 
        timer.it_value.tv_usec = LEVEL2; 

        timer.it_interval.tv_sec  = 0; 
        timer.it_interval.tv_usec = LEVEL2; 
    }


    else {

        timer.it_value.tv_sec = 0; 
        timer.it_value.tv_usec = LEVEL3; 

        timer.it_interval.tv_sec  = 0; 
        timer.it_interval.tv_usec = LEVEL3;
    }

    setitimer(ITIMER_REAL, &timer, NULL); 

    // the current thread has not exited
    if ( context != NULL ) {

        current_thread = to_run; 
        to_run->thread_state = RUNNING; 

       // printf("Swaping context between thread %d  and thread %d\n", context->tid, to_run->tid );

        swapcontext(context->thread_context, to_run->thread_context); 
    }

    else {

        current_thread = to_run; 
        setcontext(to_run->thread_context); 
    }



}


// responsible for catching interrupts
void timer_interrupt () {
    //printf("Timer went off\n");
    // when going into the scheduler, we must first disable the timer
    struct itimerval timer; 
    getitimer(ITIMER_REAL, &timer); 

    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL); 

    num_interrupts++; 

    schedule_threads(); 
}



/* terminate a thread */
void my_pthread_exit(void *value_ptr) {

    // simply deal with the return value

    tcb * victim = get_running_thread(); 

    // first check if there are any threads waiting on this thread

    tcb * ptr = WaitQueue->head; 

    while ( ptr != NULL ){

        if ( ptr->join == victim->tid )
            break; 

        ptr = ptr->next; 
    }

    // put this thread onto the run queue
    if ( ptr != NULL ){
    ptr->thread_state = READY; 
    Insert_to_qeueue(ptr);
    } 

    // if the return value is null, just set its state to exited
    if ( value_ptr == NULL ) {

        victim->thread_state = EXITED; 
        my_pthread_yield(); 
    }

   // insert return value into the list of return values
    else{

        return_value *new_Val = (return_value*)malloc(sizeof(return_value)); 
        new_Val->ret_val = value_ptr; 
        new_Val->id = victim->tid; 

        if ( return_list->head == NULL ) {  // first element in the list 

            return_list->head = new_Val; 
            return_list->tail = new_Val; 
            return_list->tail->next = NULL; 
        }

        else {    // insert at the end of the list

            return_list->tail->next = new_Val; 
            return_list->tail = new_Val; 
            return_list->tail->next = NULL; 
        }

        victim->thread_state = EXITED; 
        my_pthread_yield(); 
    }


};

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr) {

    tcb * to_join = get_running_thread(); 

    tcb * potential_thread = search_by_tid(thread); 
    // this means that the thread has not exited, so we put this thread on the wait queue
    if ( potential_thread != NULL ) {

        to_join->thread_state = WAITING; 
        to_join->join = thread; 

        if ( WaitQueue->head == NULL ) {

            WaitQueue->head = to_join; 
            WaitQueue->tail = to_join; 
            WaitQueue->tail->next = NULL; 
        }

        else {

            WaitQueue->tail->next = to_join; 
            WaitQueue->tail = to_join; 
            WaitQueue->tail->next = NULL; 
        }

        my_pthread_yield(); 
    }


  
    // if the first statement is skipped, then it means that the thread has exited, so we simply retrieve the return value
    return_value * ptr = return_list->head;
    return_value * prev = NULL; 

    while ( ptr != NULL ) {

        if ( ptr->id == thread )
            break;

        prev = ptr; 
        ptr = ptr->next; 
    }

    // no return value
    if ( ptr == NULL )
        return; 

    // first element in the list
    if ( prev == NULL ) {

        //only element in the list
        if ( ptr->next == NULL ) {

            *value_ptr = ptr->ret_val;
            return_list->head = NULL; 
            return_list->tail = NULL;  
            free(ptr); 
            return; 
        }

        else {

            *value_ptr = ptr->ret_val; 
            return_list->head = return_list->head->next; 
            ptr->next = NULL; 
            free(ptr); 
            return; 
        }
    }

        // not the first element

    else {

        *value_ptr = ptr->ret_val; 
        prev->next = ptr->next; 
        ptr->next = NULL; 
        free(ptr); 
        return; 
    }



    return 0;
};

/* initial the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {

    if ( mutex->initialized == 1 ){

        printf("This mutex is already initialized fool\n");
        errno = EBUSY; 
        return errno; 
    }

    // initialize the mutex fields

    mutex->lock  =  0; 
    mutex->owner =  0;
    mutex->destroyed = 0; 
    mutex->initialized = 0; 
    mutex->curr_holder = NULL; 

    return 0;
};

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {

    if ( mutex->destroyed == 1 ) {

        printf("Attempting to lock a destroyed mutex\n");
        errno = EBADF; 
        return errno; 
    }

    tcb * owner = get_running_thread(); 

    // handle priority inversion 

    if ( mutex->lock == 1 && (owner->priority > mutex->curr_holder->priority) ) {

        update_priority(mutex->curr_holder->tid, owner->priority); 
    } 


    // simple implementation of a spin lock, may need to improve to lower context switching 
    while ( __sync_lock_test_and_set(&mutex->lock, 1) == 1 ){

        my_pthread_yield(); 
    }

    // only the owner of this mutex can unlock it
    mutex->owner = owner->tid; 
    mutex->curr_holder = owner; 

    return 0;
};

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) {

    tcb * attempt = get_running_thread(); 

    if ( mutex->owner != attempt->tid) {

        printf("This is not your mutex fool\n");
        errno = EBUSY; 
        return errno; 
    }


    mutex->owner = 0; 
    __sync_lock_release(&mutex->lock); 

    return 0;
};

/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) {

    if ( mutex->lock == 1 ) {

        errno = EBUSY; 
        printf("Attempting to destroy a locked mutex\n");
        return errno; 
    }

    mutex->destroyed = 1; 
    mutex->owner = 0; 
    mutex->initialized = 0; 

    return 0;
};


// this function initializes the multi_level priority queue
void my_queue_init () {

    int i = 0; 

    while ( i < RANKS ) {

        Pqueue[i] = (queue*)malloc(sizeof(queue)); 
        Pqueue[i]->head = NULL; 
        Pqueue[i]->tail = NULL; 
        i++; 
    }
}

 // inserts a thread control block into priority Queue
int Insert_to_qeueue ( tcb * member ) {

    int rank = member->priority; 

    // the head is null 
    if ( Pqueue[rank]->head == NULL ) {

        Pqueue[rank]->head = member; 
        Pqueue[rank]->tail = member; 
        Pqueue[rank]->tail->next = NULL; 
        return 1; 
    }

    Pqueue[rank]->tail->next = member; 
    Pqueue[rank]->tail = member; 
    Pqueue[rank]->tail->next = NULL; 
    return 1; 
}


// returns a thread for scheduling 

tcb * get_tcb () {

    int i = 0; 
    tcb * victim; 
    // iterate through the levels until you find a thread to schedule 
    while ( i < RANKS ){

        if ( Pqueue[i]->head != NULL ){

            if ( Pqueue[i]->head->next == NULL ){    // only once member in this level 

                victim = Pqueue[i]->head; 
                Pqueue[i]->head = NULL; 
                Pqueue[i]->tail = NULL; 
                return victim; 
            }

            else {        // multiple members in this level so return the first one

                victim = Pqueue[i]->head; 
                Pqueue[i]->head = Pqueue[i]->head->next; 
                victim->next = NULL; 
                return victim; 
            }
        }

        i++; 

    }

    return NULL; 
}

// searches through the multi level queue for a specific thread

void update_priority ( my_pthread_t tid, unsigned int new_priority ) {

    // first lets retrieve this thread
    tcb * boosted = removed_by_tid(tid);
    if ( boosted == NULL )
        return;  
    boosted->priority = new_priority; 
    Insert_to_qeueue(boosted); 

}


/* this function is meant to be used in the case of priority inversion 
    and boosting priority of all threads during the maintenence cycle
*/
tcb * removed_by_tid ( my_pthread_t tid ) {

    int i = 0; 
    tcb * ptr = NULL;
    tcb * prev = NULL; 

    while ( i < RANKS ) {

        ptr = Pqueue[i]->head; 

        while ( ptr != NULL ) {

            if ( ptr->tid == tid )
                break; 

            prev = ptr; 
            ptr = ptr->next; 
        }

        if ( ptr == NULL || ptr->tid == tid )
            break; 

        i++; 
    }


    // could not find thread
    if ( ptr == NULL )
        return; 

    // first check if the thread is at the front of its level 

    if ( prev == NULL ) {
        // the only element on that level 
        if ( ptr->next == NULL ) {

            Pqueue[i]->head = NULL; 
            Pqueue[i]->tail = NULL; 
            return ptr; 
        }
        // not the only element on that level 
        else {

            Pqueue[i]->head = Pqueue[i]->head->next; 
            ptr->next = NULL; 
            return ptr; 
        }
    }

    prev->next = ptr->next; 
    ptr->next = NULL; 
    return ptr; 
}

tcb * search_by_tid ( my_pthread_t tid ) {

    int i = 0; 
    tcb * ptr; 

    while ( i < RANKS ) {

        ptr = Pqueue[i]->head; 

        while ( ptr != NULL ) {

            if ( ptr->tid == tid )
                return ptr; 

            ptr = ptr->next; 
        }

        i++; 
    }

    return NULL; 
}

// gives a priority boost to all threads in the run queue
void priority_boost () {
    // may want to include main later
    unsigned int i = 1; 

    for (; i <= number_of_threads; i++)
        update_priority(i,0); 

}

