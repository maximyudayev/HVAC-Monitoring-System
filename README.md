# HVAC Remote Monitoring System
The repository contains POSIX multi-threaded, socket source code for the system design assignment of the System Software course at KU Leuven - Campus GroupT in academic year 2019-2020, taught by the awesome Stef :wink:

## System Design Choice Motivation
For my shared buffer implementation I decided to use rwlock for data structure and another rwlock for a shared variable indicating the EOF in shared buffer (written to from connmgr, read from datamgr and storagemgr).

The choice for shared variable is motivated by the fact that I want both reader threads to be able to do their corresponding processing logic independently from one another, without having to wait for each other as they both only check if the shared buffer is not closed (hence readers can preempt each other as neither changes the variable). The writer thread (connmgr) is blocked if it tries to acquire the lock when it wants to indicate the EOF of shared buffer until it gets the CPU. At that point it has exclusive access to the shared variable and the readers must block before acquiring the read lock.

For the data structure itself, either mutex or rwlock would be sufficient, but I decided to use rwlock to be able to read the structure in the print function from multiple threads, without forcing them to wait until the other finishes reading the entire structure. However, this is only for convenience, inserting and popping from the structure is all the same fro mutex and popping regardless. The other motivation to use rwlock was the higher chance of adding some additional functions to the shared buffer, i.e. search functions for which case it would be more logical to use rwlock preventing threads from blocking each other when simply reading the structure. Scalability?

There is no deadlock as all threads release the locks immediately after obtaining the piece of data and updating the pointer, and there is no interference between other synchronization methods resembling issue with eating philosophers (each getting one chopstick and dying). 

Because only 1 writer can access the structure at a time with a rwlock synchronization choice, the other one has to wait, hence no two writers can corrupt the data; readers do not write to the data ignoring this convention, which is the responsibility of the developer, me.

The locking spreads only for operations on the shared buffer and does not encapsulate the time-consuming IO operations of each corresponding thread, I.e waiting on DB query completion. The thread acquiring the lock to the shared buffer does so practically immediately since the granularity is limited to the data structure while the type of structure (single linked list) implies that there is no time consuming looping through the data structure, but instead an easy and simply 2-3 step pointer operations. Because of this, the starvation chance is reduced; of course it is perhaps limited by the number of threads trying to access the structure (perhaps 500 threads may experience starvation), but eventually when the writer concludes and shuts down, the readers will each process the entire buffer before terminating, hence the program will process all the data as it must, but only with a delay governed by the number of threads. Also, the writer is prioritized in rwlock, further supporting this case. For the reader threads, they process the data when it becomes available and are primarily slowed down by corresponding IO’s, much less so by the pthread scheduling decisions. 

I believe this approach is the most efficient as the readers can access the data and utilize it at their own pace while it makes it easy to add additional logic to perform while shared buffer is empty I.e retrying the queue of failed DB queries. Given that rwlock prioritizes the writer, which is the key chain link receiving data, it allows the writer to add data without downtime and chance of missing new data packets from it sockets. Evaluation of Pros and Cons for not using condition variable for indicating non-empty shared buffer is provided on the timing diagram. But in short it was to promote and allow to add additional functionality in non-blocking scenario when buffer is empty like resending the queue of failed DB queries, which would not otherwise be possible with a condition variable forcing a thread to wait until data is available.

## Images
Top-Level System Overview (Vandeurzen et al., 2019):
![Top-Level System Overview](https://github.com/maximyudayev/HVAC-Monitoring-System/blob/main/images/top_level_system_overview.png)

Main Process Overview (Vandeurzen et al., 2019):
![Main Process Overview](https://github.com/maximyudayev/HVAC-Monitoring-System/blob/main/images/main_process_overview.png)

Multithreaded CPU Usage:
![Multithreaded CPU Usage](https://github.com/maximyudayev/HVAC-Monitoring-System/blob/main/images/HVAC_timing.png)

Memory Layout - Threads:
![Memory Layout of Threads](https://github.com/maximyudayev/HVAC-Monitoring-System/blob/main/images/Exercise1_Memory_Layout_Threads.png)

Memory Layout - Log Process:
![Memory Layout of Log Process](https://github.com/maximyudayev/HVAC-Monitoring-System/blob/main/images/Exercise1_Memory_Layout_Log_Process.png)

Source is under GPL3 and can be freely used for personal parallel IoT projects where this can be of use.
