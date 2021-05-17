/***************************************************************************************************
 *
 * FileName:        datamgr.c
 * Comment:         Sensor data IO program. Reads and manages data using own dplist implementation
 * Dependencies:    Header (.h) files datamgr.h and dplist.h
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                       Date            Version       Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev	            20/11/2019      0.1           Starting code downloaded
 *                              30/11/2019      1.0           Succesully implemented and completed
 *                                                            libdplist updated and improved
 *                              07/01/2020      2.0           Completed 
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                         Date            Finished      Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Implement required        20/11/2019      30/11/2019    Corrected dplist implementation to
 * functionality                                              abstract from dplist_node_t.
 * 2) Test implementation and   30/11/2019      30/11/2019    Had to update logic of 1 func in 
 * changes to dplist.so                                       libdplist and to create an additional
 *                                                            one. Small mistake in initialization
 *                                                            of a local variable became a bug
 * 3) Adjust old implementation 25/12/2019      04/01/2020    I decided to pass mutex to datamgr
 * to read from shared buffer                                 and deal with slight sync code 
 *                                                            duplication for datamgr and sqlmgr, 
 *                                                            for the sake of providing cleaner
 *                                                            interface to the calling program
 * 4) Drop connection if such   30/12/2019      07/01/2020
 * sensor does not exist                                      
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 ***************************************************************************************************/

/**
 * Includes
 **/
#define _GNU_SOURCE
#define BUILDING_GATEWAY
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdlib.h>
#include "datamgr.h"

/**
 * Custom Types
 **/
typedef struct {
    uint16_t room;
    sensor_data_t sensor;
    sensor_value_t msrmnts[RUN_AVG_LENGTH];
    unsigned char num_msrmnts;
} node_t;

/**
 * Private Prototypes
 **/
//
static void * sensor_copy(void * element);
static void sensor_free(void ** element);
static int sensor_compare(void * x, void * y);

/**
 * Global Variables
 **/
static dplist_t * dplist;
static pthread_rwlock_t * sbuffer_open_rwlock;
static pthread_mutex_t * ipc_pipe_mutex;
static pthread_rwlock_t * storagemgr_failed_rwlock;
static pthread_mutex_t * connmgr_drop_conn_mutex;
static sensor_id_t * connmgr_sensor_to_drop;
static int * storagemgr_fail_flag;
static int * sbuffer_open;
static int * retval;
static int * pfds;
static int readby;
static int num_parsed_data = 0;

/**
 * Functions
 **/
//
void datamgr_init(datamgr_init_arg_t * arg)
{
    sbuffer_open_rwlock = arg->sbuffer_rwlock;
    sbuffer_open = arg->sbuffer_flag;
    retval = arg->status;
    readby = arg->id;
    ipc_pipe_mutex = arg->pipe_mutex;
    pfds = arg->ipc_pipe_fd;
    storagemgr_failed_rwlock = arg->storagemgr_failed_rwlock;
    storagemgr_fail_flag = arg->storagemgr_fail_flag;
    connmgr_drop_conn_mutex = arg->connmgr_drop_conn_mutex;
    connmgr_sensor_to_drop = arg->connmgr_sensor_to_drop;
}

void datamgr_parse_sensor_data(FILE * fp_sensor_map, sbuffer_t ** buffer)
{
    ERROR_HANDLER(fp_sensor_map == NULL || *buffer == NULL, "Error openning streams - NULL\n");
    dplist = dpl_create(&sensor_copy, &sensor_free, &sensor_compare);
    node_t dummy;
    char * send_buf;
    sensor_value_t dummy_msrmnts[RUN_AVG_LENGTH] = {0};
    
    for(int i = 0; i < RUN_AVG_LENGTH; i++) dummy.msrmnts[i] = dummy_msrmnts[i]; // Initialize an empty array of measurements
    dummy.sensor.ts = (sensor_ts_t) 0;
    dummy.sensor.value = (sensor_value_t) 0;
    dummy.num_msrmnts = (unsigned char) 0;
    char line[11]; // Line length chosen according to curent room_sensor.map [7] caused problems
    
    while(fgets(line, sizeof(line), fp_sensor_map) != NULL) // Reads every line of the text file
    {
        sscanf(line, "%hu%hu", &(dummy.room), &(dummy.sensor.id)); // Parses line and retreives room and sensor id's
        dpl_insert_sorted(dplist, &dummy, true); // Inserts nodes in the list according to sorting criteria, copies dummy to heap
        
        #if (DEBUG_LVL > 0)
        printf("\n##### Printing Sensors|Rooms DPLIST Content Summary #####\n");
        dpl_print_heap(dplist); // Prints dplist occupied heap data changes
        #endif
    }
    if(ferror(fp_sensor_map)) 
    {
        fprintf(stderr, "Error while reading text file\n");
        fflush(stderr);
        *retval = DATAMGR_FILE_PARSE_ERROR;

        asprintf(&send_buf, "%ld Data Manager: failed to read room_sensor.map", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

        return;
    } else 
    {
        asprintf(&send_buf, "%ld Data Manager: started and parsed room_sensor.map successfully", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    }
    
    void * node = NULL;
    dummy.room = 0; // Setting dummy's room ID to NULL to compare elements by sensor ID's instead
    int sbuffer_res = SBUFFER_SUCCESS;

    pthread_rwlock_rdlock(storagemgr_failed_rwlock);
    pthread_rwlock_rdlock(sbuffer_open_rwlock); // lock mutex to sbuffer_open shared data to prevent race condition during checking end of shared buffer
    while((sbuffer_res != SBUFFER_NO_DATA || *sbuffer_open) && *storagemgr_fail_flag == 0) // use condition variable from writer thread to know when to terminate the readers
    {
        pthread_rwlock_unlock(sbuffer_open_rwlock);
        pthread_rwlock_unlock(storagemgr_failed_rwlock);

        sbuffer_res = sbuffer_pop(*buffer, &node, &(dummy.sensor), readby); // non-blocking, implementation takes care of thread-safety
        
        if(sbuffer_res != SBUFFER_SUCCESS) pthread_yield();
        else if(sbuffer_res == SBUFFER_SUCCESS) 
        {
            num_parsed_data++;

            #if (DEBUG_LVL > 1)
            printf("Data Manager: sbuffer data available %"PRIu16" %g %ld\n", dummy.sensor.id, dummy.sensor.value, dummy.sensor.ts);
            fflush(stdout);
            #endif

            dplist_node_t * temp = dpl_get_reference_of_element(dplist, &dummy); // Use compare() func to find a list item whose data matches
            if(temp == NULL) // If no such list item is found, go back to beginning of while-loop
            {
                fprintf(stderr, "%" PRIu16 " is not a valid sensor ID\n", dummy.sensor.id); // Log this information to stderr
                fflush(stderr);

                asprintf(&send_buf, "%ld Data Manager: sensor %" PRIu16 " does not exist", time(NULL), dummy.sensor.id);
                write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
                
                pthread_mutex_lock(connmgr_drop_conn_mutex); 
                *connmgr_sensor_to_drop = dummy.sensor.id; // signal Connmgr to terminate connection to this socket
                pthread_mutex_unlock(connmgr_drop_conn_mutex);

                pthread_rwlock_rdlock(storagemgr_failed_rwlock);
                pthread_rwlock_rdlock(sbuffer_open_rwlock);
                continue;
            }
            
            node_t * list_el = dpl_get_element_of_reference(temp); // Abstraction from incomplete data type dplist_node_t which is not accessible to us -> returns our custom data element type
            list_el->sensor.ts = dummy.sensor.ts; // Update the "Last Modified" stamp
            sensor_value_t sum = 0; // Accumulate the measurements and find average reading
            for(int i = RUN_AVG_LENGTH-1; i >= 0; i--) // Shift array elements (circular buffer style)
            {
                if(i != 0) list_el->msrmnts[i] = list_el->msrmnts[i-1];
                if(i == 0) list_el->msrmnts[i] = dummy.sensor.value;
                sum += list_el->msrmnts[i]; // Accumulate
            }
            
            if(list_el->num_msrmnts < RUN_AVG_LENGTH-1) // If less than running average of measurements were taken, average is 0
            {
                (list_el->num_msrmnts)++;
                list_el->sensor.value = 0;

                pthread_rwlock_rdlock(storagemgr_failed_rwlock);
                pthread_rwlock_rdlock(sbuffer_open_rwlock);
                continue; // If total measurement number is less than RUN_AVG_LENGTH, skip accumulation
            }
            
            list_el->sensor.value = sum/RUN_AVG_LENGTH; // Get average
            if(list_el->sensor.value < SET_MIN_TEMP) 
            {
                fprintf(stderr, "Sensor %" PRIu16 " in Room %" PRIu16 " detected temperature below %g *C limit of %g *C at %ld\n", list_el->sensor.id, list_el->room, (double) SET_MIN_TEMP, list_el->sensor.value, list_el->sensor.ts);
                fflush(stderr);

                asprintf(&send_buf, "%ld Data Manager: sensor %" PRIu16 " in room %" PRIu16 " - too cold %g below %g", time(NULL), list_el->sensor.id, list_el->room, list_el->sensor.value, (double) SET_MIN_TEMP);
                
                write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
            } else if(list_el->sensor.value > SET_MAX_TEMP) 
            {
                fprintf(stderr, "Sensor %" PRIu16 " in Room %" PRIu16 " detected temperature above %g *C limit of %g *C at %ld\n", list_el->sensor.id, list_el->room, (double) SET_MAX_TEMP, list_el->sensor.value, list_el->sensor.ts);
                fflush(stderr);

                asprintf(&send_buf, "%ld Data Manager: sensor %" PRIu16 " in room %" PRIu16 " - too hot %g above %g", time(NULL), list_el->sensor.id, list_el->room, list_el->sensor.value, (double) SET_MAX_TEMP);
                
                write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
            }
        }

        // usleep(100000);

        pthread_rwlock_rdlock(storagemgr_failed_rwlock);
        pthread_rwlock_rdlock(sbuffer_open_rwlock); // lock mutex to sbuffer_open shared data to prevent race condition during checking end of shared buffer
    }
    pthread_rwlock_unlock(sbuffer_open_rwlock);

    if(*storagemgr_fail_flag)
    {
        pthread_rwlock_unlock(storagemgr_failed_rwlock);

        *retval = DATAMGR_INTERRUPTED_BY_STORAGEMGR;

        asprintf(&send_buf, "%ld Data Manager: signalled to terminate by Storage Manager", time(NULL));
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

        datamgr_free();

        #if (DEBUG_LVL > 0)
        printf("Data Manager is stopped\n");
        fflush(stdout);
        #endif

        pthread_exit(retval);
    } else pthread_rwlock_unlock(storagemgr_failed_rwlock);
}

void datamgr_free()
{
    assert(dplist != NULL);
    dpl_free(&dplist, true);

    char * send_buf;

    asprintf(&send_buf, "%ld Data Manager: successfully cleaned up", time(NULL));
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

    #if (DEBUG_LVL > 0)
    printf("\nData Manager: parsed data %d times\n", num_parsed_data);
    fflush(stdout);
    #endif         
}

uint16_t datamgr_get_room_id(sensor_id_t sensor_id)
{
    assert(dplist != NULL);
    node_t dummy;
    dummy.room = 0;
    dummy.sensor.id = sensor_id;
    node_t * node = dpl_get_element_of_reference(dpl_get_reference_of_element(dplist, &dummy));
    if(node == NULL) 
    {
        fprintf(stderr, "Sensor %" PRIu16 " does not exist. Unable to get room\n", sensor_id);
        fflush(stderr);
    }
    return (node != NULL) ? node->room : -1; // Returns -1 if sensor does not exist
}

sensor_value_t datamgr_get_avg(sensor_id_t sensor_id)
{
    assert(dplist != NULL);
    node_t dummy;
    dummy.room = 0;
    dummy.sensor.id = sensor_id;
    node_t * node = dpl_get_element_of_reference(dpl_get_reference_of_element(dplist, &dummy));
    if(node == NULL) 
    {
        fprintf(stderr, "Sensor %" PRIu16 " does not exist. Unable to get average\n", sensor_id);
        fflush(stderr);
    }
    return (node != NULL) ? node->sensor.value : 0; // Returns 0 if sensor does not exist
}

time_t datamgr_get_last_modified(sensor_id_t sensor_id)
{
    assert(dplist != NULL);
    node_t dummy;
    dummy.room = 0;
    dummy.sensor.id = sensor_id;
    node_t * node = dpl_get_element_of_reference(dpl_get_reference_of_element(dplist, &dummy));
    if(node == NULL) 
    {
        fprintf(stderr, "Sensor %" PRIu16 " does not exist. Unable to get last modified timestamp\n", sensor_id);
        fflush(stderr);
    }
    return (node != NULL) ? node->sensor.ts : 0; // Returns 0 if sensor does not exist
}

int datamgr_get_total_sensors()
{
    assert(dplist != NULL);
    return dpl_size(dplist);
}

// Makes a deep copy of the source element into the heap
static void * sensor_copy(void * src_element)
{
    node_t * node = (node_t *) malloc(sizeof(node_t));
    *node = *((node_t *) src_element);
    return node;
}

// Calls to dplist API, frees used memory
static void sensor_free(void ** element)
{
    free((node_t *) *element);
}

// If Room ID is specified uses it to sort in descending order, otherwise uses Sensor ID
static int sensor_compare(void * x, void * y)
{
    if(((node_t *) x)->room == 0 || ((node_t *) y)->room == 0) // Used to be able to search nodes by Sensor ID instead of Room ID
    {
        return (((node_t *) x)->sensor.id > ((node_t *) y)->sensor.id) ? 1 : (((node_t *) x)->sensor.id == ((node_t *) y)->sensor.id) ? 0 : -1;   
    }
    return ((((node_t *) x)->room > ((node_t *) y)->room) ? 1 : ((((node_t *) x)->room == ((node_t *) y)->room) ? 0 : -1));
}

void datamgr_print_summary()
{
    for(dplist_node_t * dummy = dpl_get_first_reference(dplist); dummy != NULL; dummy = dpl_get_next_reference(dplist, dummy))
    {
        node_t * node = dpl_get_element_of_reference(dummy);
        printf("\n********Room %" PRIu16 " - Sensor %" PRIu16 "********\nCurrent average reading = %g *C\nLast modified: %ld\nLast measurements (DESC):\n", node->room, node->sensor.id, node->sensor.value, node->sensor.ts);
        fflush(stdout);
        for(int i = 0; i < RUN_AVG_LENGTH; i++) 
        {
            printf("%d) %g *C\n", i+1, node->msrmnts[i]);
            fflush(stdout);
        }
    }
}