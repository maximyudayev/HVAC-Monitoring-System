#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#include "config.h"

#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1
#define SBUFFER_NODE_ALREADY_CONSUMED 2
#define SBUFFER_NODE_NO_LONGER_AVAILABLE 3

typedef struct sbuffer sbuffer_t;

/**
 * All data that can be stored in the sbuffer should be encapsulated in a
 * structure, this structure can then also hold extra info needed for your implementation
 **/
typedef struct sbuffer_data sbuffer_data_t;

/**
 * Allocates and initializes a new shared buffer
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 **/
int sbuffer_init(sbuffer_t ** buffer);

/**
 * All allocated resources are freed and cleaned up
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 **/
int sbuffer_free(sbuffer_t ** buffer);

/**
 * Removes the first data in 'buffer' (at the 'head') and returns this data as '*data'  
 * 'data' must point to allocated memory because this functions doesn't allocated memory
 * If 'buffer' is empty, the function doesn't block until new data becomes available but returns SBUFFER_NO_DATA
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 **/
int sbuffer_remove(sbuffer_t * buffer, sensor_data_t * data);

/** 
 * Inserts the data in 'data' at the end of 'buffer' (at the 'tail')
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 **/
int sbuffer_insert(sbuffer_t * buffer, sensor_data_t * data);

/**
 * Labels node as 'read' by the thread, calls internally to sbuffer_remove when node 
 * has been read by all threads
 * NOTE: data may be assumed to be valid only if function returns SBUFFER_SUCESS
 **/
int sbuffer_pop(sbuffer_t * buffer, void ** node_ptr, sensor_data_t * data, int readby);

void sbuffer_print_content(sbuffer_t * buffer);

void write_to_pipe(pthread_mutex_t * pipe_mutex, int * pfds, char * send_buf);

#endif  //_SBUFFER_H_