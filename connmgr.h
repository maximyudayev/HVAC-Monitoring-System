#ifndef _CONNMGR_H_
#define _CONNMGR_H_

#include "sbuffer.h"

/**
 * This method starts listening on the given port and when when a sensor node connects it 
 * stores the sensor data in the shared buffer.
 **/
void connmgr_listen(int port_number, sbuffer_t ** buffer);

/**
 * This method should be called to clean up the connmgr, and to free all used memory. 
 * After this no new connections will be accepted
 **/
void connmgr_free();

/**
 * This method shares variables from threads space to carry out more functionality,
 * like having access to IPC mutex/rwlock and updating the return value of the thread
 **/
void connmgr_init(connmgr_init_arg_t * arg);

#endif /* _CONNMGR_H_ */