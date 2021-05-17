/***************************************************************************************************
 *
 * FileName:        sbuffer.c
 * Comment:         Shared buffer prototypes with thread-safe implementation
 * Dependencies:    Header (.h) files sbuffer.h
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                       Date            Version       Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev	            22/12/2019      0.1           Template copied from Toledo
 *                              25/12/2019      1.0           Seemingly no issues when multithreading
 *                                                            When sleeping threads (to simulate)
 *                                                            waiting on IO, no data corruption occurs
 *                                                            but Valgrind still complains despite of
 *                                                            no memory leaks
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                         Date            Finished      Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Make functions thread     24/12/2019      25/12/2019    Chose the read/write lock for shared
 * safe and configure                                         buffer itself and 2 mutexes for 
 * synchronization                                            implementator program
 * (free, push, pop)
 * 2) Call sbuffer_remove       25/12/2019      25/12/2019    For higher level caller this change makes
 * internally from sbuffer_pop                                no difference
 * since it is now allowed to
 * change function signatures,
 * only add new ones
 * 3) Adjust implementation for 25/12/2019      25/12/2019
 * proposed sbuffer data
 * encapsulation
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include "sbuffer.h"

/**
 * Custom Types
 **/
struct sbuffer_data {
    sensor_data_t data;
    int read_by[READER_THREADS]; // labeling to identify when the message was read by both threads and can be removed
};

typedef struct sbuffer_node {
    struct sbuffer_node * next;
    sbuffer_data_t * element;
} sbuffer_node_t;

struct sbuffer {
    sbuffer_node_t * head;
    sbuffer_node_t * tail;
    pthread_rwlock_t * lock;
};

/**
 * Private Prototypes
 **/
static void _sbuffer_print_content(sbuffer_t * buffer);

/**
 * Public Prototypes
 **/
//
int sbuffer_init(sbuffer_t ** buffer)
{
    *buffer = malloc(sizeof(sbuffer_t));
    
    if(*buffer == NULL) return SBUFFER_FAILURE;
    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    (*buffer)->lock = malloc(sizeof(pthread_rwlock_t)); // malloc rwlock so it can be referenced from external object during cleanup
    pthread_rwlock_init((*buffer)->lock, NULL);
    
    return SBUFFER_SUCCESS; 
}

int sbuffer_free(sbuffer_t ** buffer)
{
    pthread_rwlock_t * lock;
    pthread_rwlock_wrlock((*buffer)->lock); // apply write lock to avoid interruption during clean up
    
    if((buffer == NULL) || (*buffer == NULL)) 
    {
        pthread_rwlock_unlock((*buffer)->lock);
        
        return SBUFFER_FAILURE;
    }
    
    while(sbuffer_remove(*buffer, NULL) != SBUFFER_NO_DATA);
    
    lock = (*buffer)->lock; // copy pointer to rwlock
    free(*buffer); // free and NULL the buffer
    *buffer = NULL;
    pthread_rwlock_unlock(lock); // release the rwlock to the buffer
    pthread_rwlock_destroy(lock); // destroy the rwlock to the buffer
    free(lock); // free the memory allocated to rwlock
    
    return SBUFFER_SUCCESS;		
}

int sbuffer_remove(sbuffer_t * buffer, sensor_data_t * data)
{
    if(buffer == NULL) return SBUFFER_FAILURE;
    if(buffer->head == NULL) return SBUFFER_NO_DATA;
    
    sbuffer_node_t * dummy = buffer->head;

    free(dummy->element);
    dummy->element = NULL;

    if(buffer->head == buffer->tail) buffer->head = buffer->tail = NULL; // buffer has only one node
    else buffer->head = buffer->head->next; // buffer has many nodes
    
    dummy->next = NULL;

    #if (DEBUG_LVL > 1)
    printf("After removing, node pointing to %p points to next at %p points to element at %p\n", dummy, dummy->next, dummy->element);
    fflush(stdout);
    #endif

    free(dummy); // release node from memory

    return SBUFFER_SUCCESS;
}

int sbuffer_pop(sbuffer_t * buffer, void ** node_ptr, sensor_data_t * data, int readby)
{
    if(buffer == NULL) return SBUFFER_FAILURE;

    sbuffer_node_t ** node = (sbuffer_node_t **) node_ptr;
    pthread_rwlock_wrlock(buffer->lock);
    if(buffer->head == NULL || (*node != NULL && *node == buffer->tail && (*node)->element != NULL && (*node)->element->read_by[readby] == 1)) // if sbuffer is empty or thread already at last element and read it before
    {
        if(buffer->head == NULL) *node = NULL;
        pthread_rwlock_unlock(buffer->lock);

        return SBUFFER_NO_DATA;
    } else if(*node == NULL || (*node != NULL && (*node)->element == NULL)) *node = buffer->head; // node is NULL at the start or when pointing to no longer existing node
    else if(*node != NULL && (*node)->element != NULL && (*node)->element->read_by[readby] == 1) // if this node was already consumed by thread
    {
        if((*node)->next != NULL) *node = (*node)->next; // fetch next one or do not do anything

        pthread_rwlock_unlock(buffer->lock);

        return SBUFFER_NODE_ALREADY_CONSUMED;
    }

    #if (DEBUG_LVL > 1)
    printf("Next node now for thread %d - %p vs head %p and tail %p\n", readby, *node, buffer->head, buffer->tail);
    fflush(stdout);
    #endif

    *data = (*node)->element->data;
    
    if((*node)->element != NULL)
    {
        (*node)->element->read_by[readby] = 1;
    
        #if (DEBUG_LVL > 1)
        printf("Thread %d marked node %p read\n", readby, *node);
        fflush(stdout);
        #endif
    } else
    {
        pthread_rwlock_unlock(buffer->lock);
        *node = NULL;

        return SBUFFER_NODE_NO_LONGER_AVAILABLE;
    }
    
    int read_by_all = 1;
    if((*node)->element != NULL)
    {
        for(int i = 0; i < READER_THREADS; i++) // check if node has been utilised by all threads
        {
            if((*node)->element->read_by[i] == 0) // if the node was not read by any of the threads, indicate
            {
                read_by_all = 0;
                break;
            }
        }
    } else
    {
        pthread_rwlock_unlock(buffer->lock);
        *node = NULL;

        return SBUFFER_NODE_NO_LONGER_AVAILABLE;
    }

    if(read_by_all && (*node)->element != NULL) // if node was read by all threads, remove it from shared buffer
    {
        #if (DEBUG_LVL > 1)
        printf("Thread %d removed node %p\n", readby, *node);
        fflush(stdout);
        #endif
        
        sbuffer_remove(buffer, NULL);
        *node = NULL; // set node pointing to NULL
    } 
    else if((*node)->element != NULL && (*node)->next != NULL && read_by_all == 0) *node = (*node)->next;
    else if((*node)->element == NULL)
    {
        pthread_rwlock_unlock(buffer->lock);
        *node = NULL; // if process was preempted between locks and node no longer exists in structure

        return SBUFFER_NODE_NO_LONGER_AVAILABLE;  
    }
    
    #ifdef DEBUG
    if(buffer->head != NULL) _sbuffer_print_content(buffer);
    #endif

    pthread_rwlock_unlock(buffer->lock);
    
    return SBUFFER_SUCCESS;
}

int sbuffer_insert(sbuffer_t * buffer, sensor_data_t * data)
{
    if(buffer == NULL) return SBUFFER_FAILURE;
    
    sbuffer_node_t * dummy = malloc(sizeof(sbuffer_node_t));
    if(dummy == NULL) return SBUFFER_FAILURE;
    
    dummy->element = malloc(sizeof(sbuffer_data_t));
    if(dummy->element == NULL)
    {
        free(dummy);

        return SBUFFER_FAILURE;
    }
    
    dummy->element->data = *data;
    dummy->next = NULL;
    for(int i = 0; i < READER_THREADS; i++) dummy->element->read_by[i] = 0;

    pthread_rwlock_wrlock(buffer->lock);
    if(buffer == NULL) 
    {
        pthread_rwlock_unlock(buffer->lock);
        free(dummy->element);
        dummy->element = NULL;
        free(dummy);
        
        return SBUFFER_FAILURE;
    }
    
    if(buffer->tail == NULL) buffer->head = buffer->tail = dummy; // buffer empty (buffer->head should also be NULL)
    else // buffer not empty
    {
        buffer->tail->next = dummy;
        buffer->tail = dummy; 
    }

    #if (DEBUG_LVL > 1)
    _sbuffer_print_content(buffer);
    printf("\nNew node at %p pointing to element at %p and next node %p- %"PRIu16" %g %ld datamgr (%d) storagemgr (%d)\n", buffer->tail, dummy->element, buffer->tail->next, buffer->tail->element->data.id, buffer->tail->element->data.value, buffer->tail->element->data.ts, buffer->tail->element->read_by[0], buffer->tail->element->read_by[1]);
    fflush(stdout);
    #endif

    pthread_rwlock_unlock(buffer->lock);

    return SBUFFER_SUCCESS;
}

void sbuffer_print_content(sbuffer_t * buffer)
{
    pthread_rwlock_rdlock(buffer->lock);
    _sbuffer_print_content(buffer);
    pthread_rwlock_unlock(buffer->lock);
}

static void _sbuffer_print_content(sbuffer_t * buffer)
{
    printf("\n##### Printing SBUFFER Content Summary #####\n");
    sbuffer_node_t * dummy = buffer->head;
    for(int i = 0; dummy != NULL; dummy = dummy->next, i++)
    {
        printf("%d: %p | %p | %p\n", i, dummy, dummy->next, dummy->element);
    }
    printf("\n");
    fflush(stdout);
}

void write_to_pipe(pthread_mutex_t * pipe_mutex, int * pfds, char * send_buf)
{
    pthread_mutex_lock(pipe_mutex);
    write(*(pfds+1), send_buf, strlen(send_buf)+1);
    pthread_mutex_unlock(pipe_mutex);        
    free(send_buf);
}