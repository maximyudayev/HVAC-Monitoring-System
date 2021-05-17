#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#ifdef BUILDING_GATEWAY
	#ifndef TIMEOUT
        #error TIMEOUT not set
    #endif
    #ifndef SET_MAX_TEMP
        #error SET_MAX_TEMP not set
    #endif
    #ifndef SET_MIN_TEMP
        #error SET_MIN_TEMP not set
    #endif

	#define PIPE_BUF 80
	#define CHILD_POS "\t\t\t"

	#ifndef MAX_CONN
		#define MAX_CONN 5  // state the max. number of connections the server will handle before exiting
	#endif

	#ifndef RUN_AVG_LENGTH
		#define RUN_AVG_LENGTH 5
	#endif

	#ifndef STORAGE_INIT_ATTEMPTS
		#define STORAGE_INIT_ATTEMPTS 3
	#endif

	#define NUM_THREADS 3
	#define READER_THREADS 2

	#define THREAD_SUCCESS 0
	#define THREAD_ERR_FILEIO 1

	#define DATAMGR_FILE_PARSE_ERROR 2
	#define DATAMGR_INTERRUPTED_BY_STORAGEMGR 3

	#define CONNMGR_INCORRECT_PORT 4
	#define CONNMGR_SERVER_OPEN_ERROR 5
	#define CONNMGR_SERVER_CLOSE_ERROR 6
	#define CONNMGR_SERVER_CONNECTION_ERROR 7
	#define CONNMGR_SERVER_POLL_ERROR 8
	#define CONNMGR_INTERRUPTED_BY_STORAGEMGR 9
#endif

typedef uint16_t sensor_id_t;
typedef double sensor_value_t;     
typedef time_t sensor_ts_t;         // UTC timestamp as returned by time() - notice that the size of time_t is different on 32/64 bit machine

typedef struct{
	sensor_id_t id;
	sensor_value_t value;
	sensor_ts_t ts;
} sensor_data_t;

typedef struct {
	pthread_rwlock_t * sbuffer_rwlock;
	pthread_mutex_t * pipe_mutex;
	pthread_mutex_t * stdio_mutex;
	int * sbuffer_flag;
	int * ipc_pipe_fd;
	int * status;
	int id;
} storagemgr_init_arg_t;

typedef struct {
	pthread_rwlock_t * sbuffer_rwlock;
	pthread_mutex_t * pipe_mutex;
	pthread_mutex_t * stdio_mutex;
	pthread_rwlock_t * storagemgr_failed_rwlock;
	pthread_mutex_t * connmgr_drop_conn_mutex;
	sensor_id_t * connmgr_sensor_to_drop;
	int * sbuffer_flag;
	int * storagemgr_fail_flag;
	int * ipc_pipe_fd;
	int * status;
	int id;
} datamgr_init_arg_t;

typedef struct {
	pthread_rwlock_t * sbuffer_rwlock;
	pthread_mutex_t * pipe_mutex;
	pthread_mutex_t * stdio_mutex;
	pthread_rwlock_t * storagemgr_failed_rwlock;
	pthread_mutex_t * connmgr_drop_conn_mutex;
	sensor_id_t * connmgr_sensor_to_drop;
	int * sbuffer_flag;
	int * storagemgr_fail_flag;
	int * ipc_pipe_fd;
	int * status;
} connmgr_init_arg_t;
			
#endif /* _CONFIG_H_ */