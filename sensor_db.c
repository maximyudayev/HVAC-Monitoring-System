/***************************************************************************************************
 *
 * FileName:        sensor_db.c
 * Comment:         File IO with SQLite implementation
 * Dependencies:    Header (.h) files - sensor_db.h
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                       Date            Version       Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev                04/12/2019      1.0           Initially misunderstood and 
 *                                                            overcomplicated the problem
 *                              09/12/2019      2.0           Added child logging process. When EOF
 *                                                            is sent, 1 msg sometimes does not get
 *                                                            read by the reader
 *                              07/01/2020      3.0           Previous issues are fixed, caused by
 *                                                            race condition with EOF signal of pipe
 *                                                            Completed
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                         Date            Finished      Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Implement required        03/12/2019      04/12/2019    -
 * functionality
 * 2) Replace former assert()   08/12/2019      
 * with #define handlers for
 * NULL arguments and add
 * erro handlers
 * 3) Create separate logging   08/12/2019      08/12/2019    -
 * process
 * 4) Test logging process and  08/12/2019      08/12/2019    Reading from pipe a byte at a time is
 * inter-process communication                                more future proof as messages may
 *                                                            exceed the buffer length. Check for \0
 *                                                            !!!ISSUE!!! When closing the write end
 *                                                            of pipe, if child has not finished
 *                                                            reading all packets, it exits unfinished
 * 5) Finish up 2. and treat    08/12/2019      09/12/2019    No bug, when stdout is flushed with
 * race condition bug with pipe                               identical messages, program hangs
 *                                                            though log file sometimes losses 1 msg
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 ***************************************************************************************************/

/**
 * Includes
 **/
#define _GNU_SOURCE
#define BUILDING_GATEWAY
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "sensor_db.h"

/**
 * Global Variables
 **/
static pthread_rwlock_t * sbuffer_open_rwlock;
static pthread_mutex_t * ipc_pipe_mutex;
static int * sbuffer_open;
static int * retval;
static int * pfds;
static int readby;
static int num_parsed_data;

/**
 * Functions
 **/
// 
void storagemgr_init(storagemgr_init_arg_t * arg)
{
    sbuffer_open_rwlock = arg->sbuffer_rwlock;
    sbuffer_open = arg->sbuffer_flag;
    retval = arg->status;
    readby = arg->id;
    ipc_pipe_mutex = arg->pipe_mutex;
    pfds = arg->ipc_pipe_fd;
}

void storagemgr_parse_sensor_data(DBCONN * conn, sbuffer_t ** buffer)
{
    void * node = NULL;
    sensor_data_t data;
    int sbuffer_res = SBUFFER_SUCCESS;

    pthread_rwlock_rdlock(sbuffer_open_rwlock); // lock mutex to sbuffer_open shared data to prevent race condition during checking end of shared buffer
    while(sbuffer_res != SBUFFER_NO_DATA || *sbuffer_open) // use condition variable from writer thread to know when to terminate the readers
    {
        pthread_rwlock_unlock(sbuffer_open_rwlock);

        sbuffer_res = sbuffer_pop(*buffer, &node, &data, readby); // non-blocking, implementation takes care of thread-safety

        if(sbuffer_res != SBUFFER_SUCCESS) pthread_yield();
        else if(sbuffer_res == SBUFFER_SUCCESS) 
        {
            num_parsed_data++;

            #if (DEBUG_LVL > 1)
            printf("Storage Manager: sbuffer data available %"PRIu16" %g %ld\n", data.id, data.value, data.ts);
            fflush(stdout);
            #endif

            insert_sensor(conn, data.id, data.value, data.ts);
        }

        // usleep(100000);

        pthread_rwlock_rdlock(sbuffer_open_rwlock); // lock mutex to sbuffer_open shared data to prevent race condition during checking end of shared buffer
    }
    pthread_rwlock_unlock(sbuffer_open_rwlock);
}

DBCONN * init_connection(char clear_up_flag)
{
    sqlite3 * db;
    char * errmsg;
    char * send_buf;
    int rc = sqlite3_open(TO_STRING(DB_NAME), &db);
    
    if(rc != SQLITE_OK) // If connection failed, print to stderr pipe and to child process
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Unable to connect to SQL server", time(NULL));
        
        sqlite3_close(db);
        db = NULL;
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Connected to SQL server", time(NULL));
    }
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    
    if(db != NULL)
    {
        char * sql;
        if(clear_up_flag == 1)
        {
            sql =   "DROP TABLE IF EXISTS "TO_STRING(TABLE_NAME)";"
                    "CREATE TABLE "TO_STRING(TABLE_NAME)"(id INTEGER PRIMARY KEY ASC AUTOINCREMENT, sensor_id INTEGER, sensor_value DECIMAL(4,2), timestamp TIMESTAMP);";
        } else
        {
            sql =   "CREATE TABLE IF NOT EXISTS "TO_STRING(TABLE_NAME)"(id INTEGER PRIMARY KEY ASC AUTOINCREMENT, sensor_id INTEGER, sensor_value DECIMAL(4,2), timestamp TIMESTAMP);";
        }
        
        rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
        
        if(rc != SQLITE_OK) // If query failed, print to stderr and pass DB error message to child process
        {
            fprintf(stderr, "SQL error: %s\n", errmsg);
            fflush(stderr);

            asprintf(&send_buf, "%ld Storage Manager: %s", time(NULL), errmsg);
            
            sqlite3_free(errmsg);
            db = NULL;
        } else
        {
            asprintf(&send_buf, "%ld Storage Manager: New table "TO_STRING(TABLE_NAME)" created", time(NULL));
        }
        
        write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    } 

    return db;
}

void disconnect(DBCONN *conn)
{
    char * send_buf;
    int rc = sqlite3_close(conn);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to close. Database still busy\n");
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Unable to disconnect from SQL server - server busy", time(NULL));
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Disconnected from SQL server", time(NULL));
    }

    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

    #if (DEBUG_LVL > 0)
    printf("\nStorage Manager: parsed data %d times\n", num_parsed_data);
    fflush(stdout);
    #endif
}

int insert_sensor(DBCONN * conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts)
{
    char * errmsg;
    char * send_buf;
    char * sql;

    asprintf(&sql, "INSERT INTO "TO_STRING(TABLE_NAME)"(sensor_id, sensor_value, timestamp) VALUES(%hu, %e, %ld);", id, value, ts);
    
    int rc = sqlite3_exec(conn, sql, NULL, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Data insertion failed::%s", ts, errmsg);
        
        sqlite3_free(errmsg);
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Inserted new reading in %s", ts, TO_STRING(TABLE_NAME));
    }

    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

    return rc;
}

int find_sensor_all(DBCONN * conn, callback_t f)
{
    char * errmsg;
    char * send_buf;
    char * sql = "SELECT * FROM "TO_STRING(TABLE_NAME)" ORDER BY id ASC;";
    int rc = sqlite3_exec(conn, sql, f, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: All sensor query failed::%s", time(NULL), errmsg);
        
        sqlite3_free(errmsg);
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: All sensor query complete", time(NULL));
    }
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);

    return rc;
}

int find_sensor_by_value(DBCONN * conn, sensor_value_t value, callback_t f)
{
    char * errmsg;
    char * send_buf;
    char * sql;

    asprintf(&sql, "SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE sensor_value = %g ORDER BY id ASC;", value);
    
    int rc = sqlite3_exec(conn, sql, f, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Sensor query by value failed::%s", time(NULL), errmsg);

        sqlite3_free(errmsg);
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Sensor query by value complete", time(NULL)); 
    }
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    free(sql);

    return rc;
}

int find_sensor_exceed_value(DBCONN * conn, sensor_value_t value, callback_t f)
{
    char * errmsg;
    char * send_buf;
    char * sql;
    
    asprintf(&sql, "SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE sensor_value > %g ORDER BY id ASC;", value);
    
    int rc = sqlite3_exec(conn, sql, f, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Sensor query GT value failed::%s", time(NULL), errmsg);

        sqlite3_free(errmsg);
    } else 
    {
        asprintf(&send_buf, "%ld Storage Manager: Sensor query GT value complete", time(NULL));
    }
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    free(sql);

    return rc;
}

int find_sensor_by_timestamp(DBCONN * conn, sensor_ts_t ts, callback_t f)
{
    char * errmsg;
    char * send_buf;
    char * sql;

    asprintf(&sql, "SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE timestamp = %ld ORDER BY id ASC;", ts);
    
    int rc = sqlite3_exec(conn, sql, f, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Sensor query by timestamp failed::%s", time(NULL), errmsg);
        
        sqlite3_free(errmsg);
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Sensor query by timestamp complete", time(NULL));    
    }
    
    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    free(sql);

    return rc;
}

int find_sensor_after_timestamp(DBCONN * conn, sensor_ts_t ts, callback_t f)
{
    char * errmsg;
    char * send_buf;
    char * sql;

    asprintf(&sql, "SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE timestamp > %ld ORDER BY id ASC;", ts);
    
    int rc = sqlite3_exec(conn, sql, f, NULL, &errmsg);
    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        fflush(stderr);

        asprintf(&send_buf, "%ld Storage Manager: Sensor query GT timestamp failed::%s", time(NULL), errmsg);

        sqlite3_free(errmsg);
    } else
    {
        asprintf(&send_buf, "%ld Storage Manager: Sensor query GT timestamp complete", time(NULL));
    }

    write_to_pipe(ipc_pipe_mutex, pfds, send_buf);
    free(sql);

    return rc;
}