#ifndef _CONNECTION_INC
#define _CONNECTION_INC

#include "segel.h"

struct connection_struct
{
    int connfd; // The connection fd
    int job_id; // The unique id of this connection.
};


// ********** Connection List ********** //
typedef enum ConnectionRes_t 
{
    CONNECTION_SUCCESS = 0,
    CONNECTION_OUT_OF_MEMORY,
    CONNECTION_EMPTY,
    CONNECTION_NOT_FOUND = 404
} ConnectionRes;

typedef struct connection_struct* ConnectionStruct;
typedef struct _node
{
    ConnectionStruct info;
    struct _node* next;
    struct _node* prev;
} *c_node;
typedef struct connection_list* ConnectionList;

// Return a ConnectionList object on success, NULL on fail.
ConnectionList connCreateList();

// Destory the list. Never fails.
void connDestroyList(ConnectionList list);

/**
 * Push an entry with a copy of info to the head of the list.
 * Return CONNECTION_OUT_OF_MEMORY if allocation fails,
 * Otherwise return CONNECTION_SUCCESS. 
 */
ConnectionRes connPushHead(ConnectionList list, ConnectionStruct info);

/**
 * Pop an entry from the head of the list.
 * Return CONNECTION_EMPTY if allocation fails,
 * Otherwise return CONNECTION_SUCCESS. 
 */
ConnectionRes connPopHead(ConnectionList list, int to_free);

/**
 * Push an entry with a copy of info to the tail of the list.
 * Return CONNECTION_OUT_OF_MEMORY if allocation fails,
 * Otherwise return CONNECTION_SUCCESS.
 */
ConnectionRes connPushTail(ConnectionList list, ConnectionStruct info);

/**
 * Pop an entry from the tail of the list.
 * Return CONNECTION_EMPTY if allocation fails,
 * Otherwise return CONNECTION_SUCCESS. 
 */
ConnectionRes connPopTail(ConnectionList list, int to_free);

/**
 * Remove an entry (together with its node) that matches
 * the given job_id from the list.
 */
void connRemoveById(ConnectionList list, int job_id);

/**
 * Returns an ConnectionStruct that matches the given job_id in the list.
 * Otherwise (if not found), return NULL.
 */
ConnectionStruct connGetById(ConnectionList list, int job_id);

/**
 * Get a reference to the first entry of the list.
 * Return NULL if the list is empty.
 */
ConnectionStruct connGetFirst(ConnectionList list);

/**
 * Get a reference to the last entry of the list.
 * Return NULL if the list is empty.
 */
ConnectionStruct connGetLast(ConnectionList list);

/**
 * Return the size of the list.
 */
int connGetSize(ConnectionList list);

// ********** Parallel Queue ********** //
// ***** (using connection list) ****** //
typedef struct parallel_q* ParallelQ;

/**
 * Create a parallel queue instance and return a pointer to the created object.
 * Return NULL if allocation failed.
 */  
ParallelQ parallelCreateQueue();

/**
 * Enqueue a new ConnectionStruct to the parallel queue.
 * Return CONNECTION_OUT_OF_MEMORY if allocation failed,
 * Otherwise, return CONNECTION_SUCCESS.
 */ 
ConnectionRes parallelEnqueue(ParallelQ queue, ConnectionStruct info);

/**
 * Dequeue a ConnectionStruct from the parallel queue.
 * If the list is empty, wait until another thread adds a ConnectionStruct to the list.
 * Returns a reference to the ConnectionStruct that was dequeued.
 */ 
ConnectionStruct parallelDequeue(ParallelQ queue);

/**
 * Get a reference to the ConnectionStruct matching the job_id in the list.
 * Return NULL if the job id was not found.
 */
ConnectionStruct parallelGetConnectionStruct(ParallelQ queue, int job_id);

/**
 * Remove an entry (together with its node) that matches
 * the given job_id from the parallel queue.
 */
void parallelRemoveById(ParallelQ queue, int job_id);

#endif