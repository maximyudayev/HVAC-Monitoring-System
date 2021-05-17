/***************************************************************************************************
 *
 * FileName:        connmgr.c
 * Comment:         Network connection manager
 * Dependencies:    Header (.h) files connmgr.h
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                       Date            Version       Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev	            11/12/2019      0.1           Template copied from Toledo
 *                              13/12/2019      0.2           Functionality implemented as described
 *                                                            in Lab 8 text
 *                              07/01/2020      1.0           Completed, when signalled by Data Manager
 *                                                            connection to non-existing sensor is 
 *                                                            dropped
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                         Date            Finished      Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Implement functionality   11/12/2019      13/12/2019
 * defined in lab text
 * 2) Test implementation       11/12/2019      13/12/2019    Implementation appears to work correctly,
 *                                                            multiple connections are accepted and
 *                                                            cleaned up on timeout and termination
 * 3) Update current            25/12/2019      25/12/2019
 * implementation according to
 * defensive programming tips
 * i.e. prevent exceptions and
 * check for errors
 * 4) Add access to main        25/12/2019      29/12/2019
 * process's IPC pipe to write
 * to the log process data of
 * interest
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 ***************************************************************************************************/

/**
 * Includes
 **/
#define _GNU_SOURCE
#define BUILDING_GATEWAY
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <poll.h>
#include <pthread.h>
#include "config.h"
#include "connmgr.h"
#include "lib/tcpsock.h"
#include "lib/dplist.h"

/**
 * Custom Types
 **/
struct tcpsock_dpl_el {     // Custom element to use with my dplist implementation to be able to use compare() call
    tcpsock_t * sock_ptr;   // to and create dummy to compare with (tcpsock_t is incomplete data type otherwise, hence
    int sd;                 // can't create a dummy with a given sd to use in compare())
    sensor_ts_t last_active;
    sensor_id_t sensor;
};

/**
 * Private Prototypes
 **/
//
static void * socket_copy(void * element);
static void socket_free(void ** element);
static int socket_compare(void * x, void * y);

/**
 * Global Variables
 **/
static dplist_t * socket_list;
static tcpsock_t * server;
static struct pollfd * poll_fds;
static pthread_rwlock_t * sbuffer_open_rwlock;
static pthread_mutex_t * ipc_pipe_mutex;
static pthread_rwlock_t * storagemgr_failed_rwlock;
static pthread_mutex_t * connmgr_drop_conn_mutex;
static sensor_id_t * connmgr_sensor_to_drop;
static int * storagemgr_fail_flag;
static int * sbuffer_open;
static int * retval;
static int * pfds;

/**
 * Functions
 **/
// 
void connmgr_init(connmgr_init_arg_t * arg)
{
    sbuffer_open_rwlock = arg->sbuffer_rwlock;
    sbuffer_open = arg->sbuffer_flag;
    retval = arg->status;
    ipc_pipe_mutex = arg->pipe_mutex;
    pfds = arg->ipc_pipe_fd;
    storagemgr_failed_rwlock = arg->storagemgr_failed_rwlock;
    storagemgr_fail_flag = arg->storagemgr_fail_flag;
    connmgr_drop_conn_mutex = arg->connmgr_drop_conn_mutex;
    connmgr_sensor_to_drop = arg->connmgr_sensor_to_drop;
}

void connmgr_listen(int port_number, sbuffer_t ** buffer)
{
    char * send_buf;

    if(port_number < MIN_PORT || port_number > MAX_PORT) 
    {
        asprintf(&send_buf, "%ld Connection Manager: invalid PORT", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

        *retval = CONNMGR_INCORRECT_PORT; 
        poll_fds = NULL; // set pointers to NULL to avoid freeing unallocated space in call to 'free' (initial value may not be NULL)
        socket_list = NULL;
        server = NULL;

        return;
    }

    socket_list = dpl_create(&socket_copy, &socket_free, &socket_compare);
    poll_fds = (struct pollfd *) malloc(sizeof(struct pollfd)); // Initially array for 1 element

    if(tcp_passive_open(&(server), port_number) != TCP_NO_ERROR) 
    {
        *retval = CONNMGR_SERVER_OPEN_ERROR; // her setting poll_fds and socket_list to NULL is not needed as it was allocated already
        
        asprintf(&send_buf, "%ld Connection Manager: failed to start", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
        
        return;
    }

    asprintf(&send_buf, "%ld Connection Manager: started successfully", time(NULL));
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

    tcp_get_sd(server, &(poll_fds[0].fd)); // Set socket file descriptor to poll elements
    poll_fds[0].events = POLLIN; // Choose poll events
    
    struct tcpsock_dpl_el * client;
    struct tcpsock_dpl_el dummy;
    dplist_node_t * node;
    int conn_counter = 0, sbuffer_insertions = 0;
    sensor_data_t data;
    int bytes, tcp_res, tcp_conn_res, poll_res, sbuffer_res;

    while((poll_res = poll(poll_fds, (conn_counter+1), TIMEOUT*1000)) || conn_counter) // Repeat until poll times-out after no connections are left
    {
        pthread_rwlock_rdlock(storagemgr_failed_rwlock); // putting inside the loop does not force storagemgr to hang until poll elapses TIMEOUT seconds
        if(*storagemgr_fail_flag)                  // connmgr instead treats the signal asynchronously whenever it is done polling
        {
            pthread_rwlock_unlock(storagemgr_failed_rwlock);

            *retval = CONNMGR_INTERRUPTED_BY_STORAGEMGR;

            asprintf(&send_buf, "%ld Connection Manager: signalled to terminate by Storage Manager", time(NULL));
            write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

            connmgr_free();

            #if (DEBUG_LVL > 0)
            printf("Connection Manager is stopped\n");
            fflush(stdout);
            #endif

            pthread_exit(retval);
        }
        pthread_rwlock_unlock(storagemgr_failed_rwlock);

        if(poll_res == -1) break;
        if((poll_fds[0].revents & POLLIN) && conn_counter < MAX_CONN) // When an event is received from Master socket, create new socket unless limit is reached
        {
            #if (DEBUG_LVL > 1)
            printf("Incoming client connection\n");
            fflush(stdout);
            #endif

            client = (struct tcpsock_dpl_el *) malloc(sizeof(struct tcpsock_dpl_el));
            
            if((tcp_conn_res = tcp_wait_for_connection(server, &(client->sock_ptr))) != TCP_NO_ERROR) // Blocks until a connection is processed
            {
                *retval = CONNMGR_SERVER_CONNECTION_ERROR;

                asprintf(&send_buf, "%ld Connection Manager: failed to accept new connection (%d)", time(NULL), tcp_conn_res);
                write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
            } else
            {
                conn_counter++; // Increment number of connections
                poll_fds = (struct pollfd *) realloc(poll_fds, sizeof(struct pollfd)*(conn_counter+1)); // Increase poll_fd array size
                tcp_get_sd(client->sock_ptr, &(poll_fds[conn_counter].fd)); // Set socket file descriptor to poll elements
                client->sd = poll_fds[conn_counter].fd;
                client->last_active = (sensor_ts_t) time(NULL);
                client->sensor = 0;
                poll_fds[conn_counter].events = POLLIN | POLLHUP; // Choose poll events
                dpl_insert_sorted(socket_list, client, false); // Insert connection into dplist
                
                asprintf(&send_buf, "%ld Connection Manager: new connection received", time(NULL));
                write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

                #if (DEBUG_LVL > 0)
                printf("\n##### Printing Socket DPLIST Content Summary #####\n");
                dpl_print_heap(socket_list);
                #endif
            }
            poll_res--;
        }

        for(int i = 1; i < (conn_counter+1) && poll_res > 0; i++) // poll_res indicates number of structures, stop looping when that number is reached
        {   
            dummy.sd = poll_fds[i].fd; // Find corresponding client, based on the sd
            node = dpl_get_reference_of_element(socket_list, &dummy); // Get corresponding element from dplist
            client = (node != NULL) ? (struct tcpsock_dpl_el *) dpl_get_element_of_reference(node) : NULL;
            
            if(client != NULL && ((client->last_active + (sensor_ts_t) TIMEOUT) > (sensor_ts_t) time(NULL)) && (poll_fds[i].revents & POLLIN)) // If there is data available from client socket and socket is non NULL and has not timed out yet
            {
                #if (DEBUG_LVL > 1)
                printf("Receiving data from %d peer of %d total\n", i, conn_counter);
                fflush(stdout);
                #endif
                
                bytes = sizeof(data.id); // read sensor ID
                tcp_res = tcp_receive(client->sock_ptr, (void *) &data.id, &bytes);
                bytes = sizeof(data.value); // read temperature
                tcp_res = tcp_receive(client->sock_ptr, (void *) &data.value, &bytes);
                bytes = sizeof(data.ts); // read timestamp
                tcp_res = tcp_receive(client->sock_ptr, (void *) &data.ts, &bytes);
                
                if((tcp_res == TCP_NO_ERROR) && bytes) 
                {
                    client->last_active = (sensor_ts_t) time(NULL); // Make sure to update last_active only when receiving is successful
                    if(client->sensor == 0) client->sensor = data.id;
                    sbuffer_res = sbuffer_insert(*buffer, &data); // sbuffer implementation takes care of thread safety

                    if(sbuffer_res == SBUFFER_SUCCESS) // this block is purely for debugging
                    {
                        sbuffer_insertions++;

                        #if (DEBUG_LVL > 1)
                        printf("Inserted new in shared buffer: %" PRIu16 " %g %ld\n", data.id, data.value, data.ts);
                        fflush(stdout);
                        #endif
                    } else
                    {
                        #if (DEBUG_LVL > 1)
                        printf("Failed to insert in shared buffer: %" PRIu16 " %g %ld\n", data.id, data.value, data.ts);
                        fflush(stdout);
                        #endif
                    }
                } else if(tcp_res == TCP_CONNECTION_CLOSED) 
                {
                    poll_fds[i].events = -1;
                    
                    asprintf(&send_buf, "%ld Connection Manager: lost connection with", time(NULL));
                    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
                }
            }

            pthread_mutex_lock(connmgr_drop_conn_mutex);
            if((client != NULL && client->sensor == *connmgr_sensor_to_drop) || (poll_fds[i].revents & POLLHUP) || (poll_fds[i].events == -1) || (client != NULL && ((client->last_active + (sensor_ts_t) TIMEOUT) < (sensor_ts_t) time(NULL))) || client == NULL) // If peer terminated connection or connection timed out for existing socket or no element was found stop listening to this descriptor, remove file descriptor from the list
            {
                if(client != NULL && client->sensor == *connmgr_sensor_to_drop) 
                {
                    *connmgr_sensor_to_drop = 0;
                    pthread_mutex_unlock(connmgr_drop_conn_mutex);

                    asprintf(&send_buf, "%ld Connection Manager: signalled to drop connection to %"PRIu16, time(NULL), client->sensor);
                    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
                } else pthread_mutex_unlock(connmgr_drop_conn_mutex);
                
                #if (DEBUG_LVL > 1)
                printf("Peer closed connection or timed out - %d of %d\n", i, conn_counter);
                fflush(stdout);
                #endif
                
                if(client != NULL) 
                {
                    asprintf(&send_buf, "%ld Connection Manager: connection to %"PRIu16" closed", time(NULL), client->sensor);
                    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
                    
                    dpl_remove_node(socket_list, node, true); // Close and remove connection from dplist if element exists
                }
                for(int id1 = 0, id2 = 0; id1 < conn_counter; id1++, id2++)
                {
                    id2 += (id2 == i) ? 1 : 0; // Skip deleted element
                    poll_fds[id1] = poll_fds[id2]; // Copy socket descriptors between arrays
                }
                poll_fds = realloc(poll_fds, sizeof(struct pollfd)*conn_counter);
                conn_counter--; // Decrement number of sockets. Decremented after realloc because array contains server socket at index 0, hence new_connections+1 elements for new array
                i--; // Ensures when an element is removed from poll_fds, incrementation won't skip over the following element
                
                #if (DEBUG_LVL > 0)
                printf("\n##### Printing Socket DPLIST Content Summary #####\n");
                dpl_print_heap(socket_list);
                #endif
            } else pthread_mutex_unlock(connmgr_drop_conn_mutex);
        }
    }
    
    if(poll_res == -1)
    {
        *retval = CONNMGR_SERVER_POLL_ERROR;
        
        asprintf(&send_buf, "%ld Connection Manager: error polling sockets", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    }

    #if (DEBUG_LVL > 0)
    printf("Connection Manager: total %d messages processed during session\n", sbuffer_insertions);
    fflush(stdout);
    #endif
}

void connmgr_free()
{
    char * send_buf;

    if(server != NULL && tcp_close(&server) != TCP_NO_ERROR) 
    {
        *retval = CONNMGR_SERVER_CLOSE_ERROR; // close master socket if any
        
        asprintf(&send_buf, "%ld Connection Manager: failed to stop", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    } else
    {
        asprintf(&send_buf, "%ld Connection Manager: stopped successfully", time(NULL));        
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    }
    if(poll_fds != NULL) free(poll_fds); // Clean up allocated socket descriptor array if any
    if(socket_list != NULL) dpl_free(&socket_list, true); // Clean up allocated tcpsock_dpl_el dplist if any

    pthread_rwlock_wrlock(sbuffer_open_rwlock); // lock mutex to sbuffer_open shared data
    #if (DEBUG_LVL > 0)
    printf("Server is shutting down. Closing shared buffer\n");
    fflush(stdout);
    #endif
    *sbuffer_open = 0; // indicate reader threads the end of buffer
    pthread_rwlock_unlock(sbuffer_open_rwlock);
}

static void * socket_copy(void * element)
{
    struct tcpsock_dpl_el * dummy = (struct tcpsock_dpl_el *) malloc(sizeof(struct tcpsock_dpl_el));
    dummy->sock_ptr = ((struct tcpsock_dpl_el *) element)->sock_ptr;
    dummy->sd = ((struct tcpsock_dpl_el *) element)->sd;
    dummy->last_active = ((struct tcpsock_dpl_el *) element)->last_active;
    dummy->sensor = ((struct tcpsock_dpl_el *) element)->sensor;
    return dummy;
}

static void socket_free(void ** element)
{
    tcp_close(&(((struct tcpsock_dpl_el *) *element)->sock_ptr)); // Close connection to that socket
    free((struct tcpsock_dpl_el *) *element); // Free allocated space of the dplist element
    *element = NULL; // Set pointer to a dplist element pointing to NULL
}

static int socket_compare(void * x, void * y)
{
    return ((((struct tcpsock_dpl_el *) x)->sd == ((struct tcpsock_dpl_el *) y)->sd) ? 0 : ((((struct tcpsock_dpl_el *) x)->sd > ((struct tcpsock_dpl_el *) y)->sd) ? -1 : 1));
}