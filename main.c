/*********************************************************************************************************
 *
 * FileName:        main.c
 * Comment:         Core functionality of the program. Combines the former threader.c and server.c
 *                  functionality from previous assignments.
 *                  Creates a thread for connmgr, datamgr and sqlmgr. 
 *                  Creates separate logging process.
 * Dependencies:    Header (.h) files ...enter dependecies here...
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                           Date            Version         Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev	                07/01/2020      1.0             Completed
 *                                                                  To improve current version, I would use
 *                                                                  own sensor_node.c file instead of labtools
 *                                                                  default one and update connection functions
 *                                                                  in tcpsock.c to attach sensor id to socket
 *                                                                  to be able to drop connections if no such
 *                                                                  sensor is registered with Data Manager and
 *                                                                  to also log which sensor connected on TCP
 *                                                                  connection for readability of log file           
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                             Date            Finished        Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Put everything together       25/12/2019      28/12/2019      Refactored and optimized, replaced
*                                                                   exit() and returns with updates to
*                                                                   return value pointers (for threads).
*                                                                   Added config.c file with a function
*                                                                   to implement in threads for writing to
*                                                                   IPC pipe to avoid code duplication in
*                                                                   all the files.
*                                                                   Wanted to refactor main and threads
*                                                                   for sweet C++ like struct passing to
*                                                                   thread_function since functionality
*                                                                   pattern between threads is very
*                                                                   similar, but constraint of leaving
*                                                                   function signatures intact prevents
*                                                                   from doing so optimally and cleanly
 * 2) Test and troubleshoot         25/12/2019      04/01/2020
 * consider using code-coverage
 * 3) Write make func for           25/12/2019      26/12/2019
 * automation
 * 4) Replace assert() wherever     28/12/2019      30/12/2019
 * present by retval ptr change
 * or other acceptable non
 * terminating means to prevent
 * program from crashing on non
 * fatal issues (it is interesting
 * to view and log where errors
 * occured and carry on). Same
 * for exit(), etc.
 * 5) Think of passing node ID      28/12/2019      07/01/2020      Requires to update sensor_node.c to pass
 * on tcp connection to connmgr                                     ID as parameter to tcpsock.c, but labtools
 * for clearer log messages                                         uses standard sensor_node.c file preventing
 *                                                                  this solution. Alternative marking on 1st
 *                                                                  packet reception is too clumsy
 * 6) Perform error checking in     28/12/2019      06/01/2020
 * all functions (to update
 * retval and make meaningful
 * logs)
 * 7) Add error/status defines      28/12/2019      06/01/2020
 * for each manager to config.h
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 ********************************************************************************************************/

/**
 * Includes
 **/
#define _GNU_SOURCE
#define BUILDING_GATEWAY
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <inttypes.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include "errmacros.h"
#include "config.h"
#include "datamgr.h"
#include "sensor_db.h"
#include "connmgr.h"
#include "sbuffer.h"
#include "lib/tcpsock.h"
#include "lib/dplist.h"

/**
 * Custom Types
 **/
struct tcpsock_dpl_el {     // Custom element to use with my dplist implementation to be able to use compare() call
    tcpsock_t * sock_ptr;   // to and create dummy to compare with (tcpsock_t is incomplete data type otherwise, hence
    int sd;                 // can't create a dummy with a given sd to use in compare())
    sensor_ts_t last_active;
};

/**
 * Global Variables
 **/
static pthread_rwlock_t sbuffer_open_rwlock;
static pthread_mutex_t ipc_pipe_mutex; // use as mutex to IPC pipe for logging
static pthread_mutex_t connmgr_drop_conn_mutex;
static pthread_rwlock_t storagemgr_failed_rwlock;
static sbuffer_t * buffer; // incomplete data type, all logic and synchronization of buffer is taken care of in sbuffer implementation
static sensor_id_t connmgr_sensor_to_drop = 0;
static int sbuffer_open = 1;
static int storagemgr_failed = 0;
static int pfds[2];

/**
 * Private Prototypes
 **/
void * connmgr(void * arg);
void * datamgr(void * arg);
void * storagemgr(void * arg);
void print_help(void);

/**
 * Functions
 **/
int main(int argc, char *argv[])
{
    int server_port;
    if(argc != 2)
    {
        print_help();
        exit(EXIT_SUCCESS);
    } else
    {
        server_port = atoi(argv[1]);
        if(server_port > MAX_PORT || server_port < MIN_PORT) 
        {
            print_help();
            exit(EXIT_SUCCESS);
        }
    }

    pid_t parent_pid, child_pid;
    int result;
    parent_pid = getpid();
    
    #if (DEBUG_LVL > 0)
    printf("Parent process (%d) is started...\n", parent_pid);
    fflush(stdout);
    #endif
    
    result = pipe(pfds); // Create separate log process
    SYSCALL_ERROR(result);

    pthread_mutex_init(&ipc_pipe_mutex, NULL);

    child_pid = fork();
    SYSCALL_ERROR(child_pid);
    
    if(child_pid == 0) // Child's Code
    {
        close(pfds[1]); // Child does not need writing end

        FILE * log_data = fopen("gateway.log", "w");
        FILE_OPEN_ERROR(log_data);
        int sequence = 0;

        child_pid = getpid();
        char rcv_buf[PIPE_BUF];
        char * buf_to_write;
        char * msg;
        
        #if (DEBUG_LVL > 0)
        printf(CHILD_POS"Child process (%d) of parent (%d) is started...\n", child_pid, parent_pid);
        fflush(stdout);
        #endif        
        
        while((result = read(pfds[0], rcv_buf, PIPE_BUF)) > 0) // Process will stay in loop
        {
            asprintf(&buf_to_write, "%d %s\n", sequence++, rcv_buf);
            fwrite(buf_to_write, strlen(buf_to_write), 1, log_data); // Write data from pipe to log file

            free(buf_to_write);
        }

        if(result == -1) // If reading from pipe resulted in an error
        {
            asprintf(&msg, "%d %ld Error reading from pipe, pipe closed\n", sequence++, time(NULL));
        } else if(result == 0)
        {
            asprintf(&msg, "%d %ld Pipe between parent (%d) and child (%d) terminated normally\n", sequence++, time(NULL), parent_pid, child_pid);
        }

        fwrite(msg, strlen(msg), 1, log_data); // Write exit message to log file
        free(msg);
        close(pfds[0]);
        fclose(log_data); // Close log file
        
        #if (DEBUG_LVL > 0)
        printf(CHILD_POS"Child process (%d) of parent (%d) is terminating...\n", child_pid, parent_pid);
        fflush(stdout);
        #endif

        _exit(EXIT_SUCCESS); 
    }

    // Parent's Code
    close(pfds[0]); // Parent does not need reading end
    
    #if (DEBUG_LVL > 0)
    printf("Parent process (%d) has created child logging process (%d)...\n", parent_pid, child_pid);
    fflush(stdout);
    #endif
    
    pthread_t threads[NUM_THREADS];
    void * exit_codes[NUM_THREADS]; // array of pointers to thread returns

    sbuffer_init(&buffer);
    pthread_rwlock_init(&sbuffer_open_rwlock, NULL);
    pthread_rwlock_init(&storagemgr_failed_rwlock, NULL);
    pthread_mutex_init(&connmgr_drop_conn_mutex, NULL);

    int arg0 = 0, arg1 = 1, arg2 = server_port;

    pthread_create(&(threads[0]), NULL, &datamgr, &arg0);
    pthread_create(&(threads[1]), NULL, &storagemgr, &arg1);
    pthread_create(&(threads[2]), NULL, &connmgr, &arg2);

    for(int i = 0; i < NUM_THREADS; i++) pthread_join(threads[i], &exit_codes[i]); // blocks until all threads terminate

    #if (DEBUG_LVL > 0)
    printf("Threads stopped. Cleaning up\nThread exit result:\n"CHILD_POS"Data Manager: %d\n"CHILD_POS"Storage Manager %d\n"CHILD_POS"Connection Manager: %d\n", *((int *) exit_codes[0]), *((int *) exit_codes[1]), *((int *) exit_codes[2]));
    fflush(stdout);
    #endif

    close(pfds[1]); // First, let threads terminate, then shut down IPC pipe -> ensures all data in pipe is written before closing
    wait(NULL); // Wait on child process

    #if (DEBUG_LVL > 0)
    printf("Child process stopped. Cleaning up\n");
    fflush(stdout);
    #endif

    sbuffer_free(&buffer);
    pthread_rwlock_destroy(&sbuffer_open_rwlock);
    pthread_mutex_destroy(&ipc_pipe_mutex);
    pthread_mutex_destroy(&connmgr_drop_conn_mutex);
    pthread_rwlock_destroy(&storagemgr_failed_rwlock);
    
    for(int i = 0; i < NUM_THREADS; i++) free(exit_codes[i]); // release memory alloc'ed to exit codes

    exit(EXIT_SUCCESS);
}   

void * datamgr(void * arg)
{
    int * retval = malloc(sizeof(int));
    *retval = THREAD_SUCCESS;
    FILE * fp_sensor_map = fopen("room_sensor.map", "r");

    #if (DEBUG_LVL > 0)
    printf("Data Manager is started\n");
    fflush(stdout);
    #endif

    datamgr_init_arg_t datamgr_init_arg = {
        .sbuffer_rwlock = &sbuffer_open_rwlock,
        .pipe_mutex = &ipc_pipe_mutex,
        .sbuffer_flag = &sbuffer_open,
        .storagemgr_fail_flag = &storagemgr_failed,
        .storagemgr_failed_rwlock = &storagemgr_failed_rwlock,
        .connmgr_drop_conn_mutex = &connmgr_drop_conn_mutex,
        .connmgr_sensor_to_drop = &connmgr_sensor_to_drop,
        .ipc_pipe_fd = pfds,
        .status = retval,
        .id = *((int*) arg),
    };

    datamgr_init(&datamgr_init_arg);
    datamgr_parse_sensor_data(fp_sensor_map, &buffer);
    int ret_listen = *retval; // in between these calls, the retval maybe different. it maybe interesting to know the value in both
    datamgr_print_summary();
    datamgr_free();
    *retval = (ret_listen != THREAD_SUCCESS && ret_listen != *retval) ? ret_listen: *retval; // in case the thread value was affected by listen and then free, show the first
    
    fclose(fp_sensor_map);

    #if (DEBUG_LVL > 0)
    printf("Data Manager is stopped\n");
    fflush(stdout);
    #endif

    pthread_exit(retval);
}

void * storagemgr(void * arg)
{
    int * retval = malloc(sizeof(int));
    *retval = THREAD_SUCCESS;
    DBCONN * db;
    int attempts = 0;

    #if (DEBUG_LVL > 0)
    printf("Storage Manager is started\n");
    fflush(stdout);
    #endif

    storagemgr_init_arg_t storagemgr_init_arg = {
        .sbuffer_rwlock = &sbuffer_open_rwlock,
        .pipe_mutex = &ipc_pipe_mutex,
        .sbuffer_flag = &sbuffer_open,
        .ipc_pipe_fd = pfds,
        .status = retval,
        .id = *((int*) arg),
    };

    storagemgr_init(&storagemgr_init_arg);
    
    do {
        db = init_connection(1);
        attempts++;
        if(db == NULL) pthread_yield();
    } while(attempts < STORAGE_INIT_ATTEMPTS && db == NULL); // attempt to connect to DB n times

    if(db != NULL)
    {
        storagemgr_parse_sensor_data(db, &buffer);
        int ret_listen = *retval; // in between these calls, the retval maybe different. it maybe interesting to know the value in both
        disconnect(db);
        *retval = (ret_listen != THREAD_SUCCESS && ret_listen != *retval) ? ret_listen: *retval; // in case the thread value was affected by listen and then free, show the first   
    } else // write to pipe and fail gracefully the entire program - signal other threads to exit
    {
        char * send_buf;
        asprintf(&send_buf, "%ld Storage Manager: Failed to start DB server %d times, exitting", time(NULL), STORAGE_INIT_ATTEMPTS);
        write_to_pipe(&ipc_pipe_mutex, pfds, send_buf);

        pthread_rwlock_wrlock(&storagemgr_failed_rwlock); // signal other threades to terminate by changing shared data value
        storagemgr_failed = 1;
        pthread_rwlock_unlock(&storagemgr_failed_rwlock);
    }

    #if (DEBUG_LVL > 0)
    printf("Storage Manager is stopped\n");
    fflush(stdout);
    #endif

    pthread_exit(retval);
}

void * connmgr(void * arg)
{   
    int * retval = malloc(sizeof(int));
    *retval = THREAD_SUCCESS;

    #if (DEBUG_LVL > 0)
    printf("Connection Manager is started\n");
    fflush(stdout);
    #endif

    connmgr_init_arg_t connmgr_init_arg = {
        .sbuffer_rwlock = &sbuffer_open_rwlock,
        .pipe_mutex = &ipc_pipe_mutex,
        .sbuffer_flag = &sbuffer_open,
        .storagemgr_fail_flag = &storagemgr_failed,
        .storagemgr_failed_rwlock = &storagemgr_failed_rwlock,
        .connmgr_drop_conn_mutex = &connmgr_drop_conn_mutex,
        .connmgr_sensor_to_drop = &connmgr_sensor_to_drop,
        .ipc_pipe_fd = pfds,
        .status = retval,
    };

    connmgr_init(&connmgr_init_arg);
    connmgr_listen(*((int*) arg), &buffer);
    int ret_listen = *retval; // in between these calls, the retval maybe different. it maybe interesting to know the value in both
    connmgr_free();
    *retval = (ret_listen != THREAD_SUCCESS && ret_listen != *retval) ? ret_listen: *retval; // in case the thread value was affected by listen and then free, show the first
    
    #if (DEBUG_LVL > 0)
    printf("Connection Manager is stopped\n");
    fflush(stdout);
    #endif

    pthread_exit(retval);
}

void print_help(void)
{
    printf("Use this program with 1 command line options: \n");
    printf("\t%-15s : TCP server port number\n", "\'server port\'");
    fflush(stdout);
}