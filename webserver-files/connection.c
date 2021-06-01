#include "connection.h"

struct connection_list
{
    c_node head;
    c_node tail;
    int size;
};

struct parallel_q
{
    ConnectionList list;
    pthread_mutex_t* global_m;
    pthread_cond_t* cond;
};

static void freeNode(c_node node)
{
    if(node == NULL)
    {
        return;
    }
    if(node->info != NULL)
    {
        free(node->info);
    }
    free(node);
}

// ********** Connection List ********** //

ConnectionList connCreateList()
{
    ConnectionList list = malloc(sizeof(*list));
    c_node head = malloc(sizeof(*head));
    c_node tail = malloc(sizeof(*tail));
    if(!list || !head || !tail)
    {
        if(list)
        {
            free(list);
        }
        if(head)
        {
            free(head);
        }
        if(tail)
        {
            free(tail);
        }
        return NULL;
    }
    
    head->info = NULL;
    head->next = tail;
    head->prev = NULL;

    tail->info = NULL;
    tail->next = NULL;
    tail->prev = head;

    list->head = head;
    list->tail = tail;
    list->size = 0;
}

void connDestroyList(ConnectionList list)
{
    if(list == NULL)
    {
        return;
    }
    assert(list->head != NULL);
    assert(list->tail != NULL);
    if(list->size > 0 && list->head->next != list->tail)
    {
        c_node it = list->head->next;
        while(it != list->tail)
        {
            connPopHead(list, 1);
            it = list->head->next;
        }
    }
    free(list->head);
    free(list->tail);
    free(list);
    return;
}

ConnectionRes connPushHead(ConnectionList list, ConnectionStruct info)
{
    assert(list);
    assert(info);
    ConnectionStruct entry = malloc(sizeof(*entry));
    c_node new_node = malloc(sizeof(*new_node));

    if(!entry || !new_node)
    {
        return CONNECTION_OUT_OF_MEMORY;
    }

    *entry = *info; // Copy the info into the allocated entry

    c_node tmp = list->head->next; // Push the entry
    list->head->next = new_node;
    new_node->next = tmp;
    new_node->prev = list->head;
    tmp->prev = new_node;

    list->size++; // Inc the size of the list

    return CONNECTION_SUCCESS;
}

ConnectionRes connPopHead(ConnectionList list, int to_free)
{
    assert(list);
    assert(list->size >= 0);
    if(list->size == 0)
    {
        return CONNECTION_EMPTY;
    }

    c_node first = list->head->next;
    list->head->next = first->next;
    first->next->prev = list->head;

    if(to_free) freeNode(first);

    list->size--;

    return CONNECTION_SUCCESS;
}

ConnectionRes connPushTail(ConnectionList list, ConnectionStruct info)
{
    assert(list);
    assert(info);
    ConnectionStruct entry = malloc(sizeof(*entry));
    c_node new_node = malloc(sizeof(*new_node));

    if(!entry || !new_node)
    {
        return CONNECTION_OUT_OF_MEMORY;
    }

    *entry = *info; // Copy the info into the allocated entry

    c_node tmp = list->tail->prev; // Push the entry
    list->tail->prev = new_node;
    new_node->prev = tmp;
    new_node->next = list->tail;
    tmp->next = new_node;

    list->size++; // Inc the size of the list

    return CONNECTION_SUCCESS;
}

ConnectionRes connPopTail(ConnectionList list, int to_free)
{
    assert(list);
    assert(list->size >= 0);
    if(list->size == 0)
    {
        return CONNECTION_EMPTY;
    }

    c_node last = list->tail->prev;
    list->tail->prev = last->prev;
    last->prev->next = list->tail;

    if(to_free) freeNode(last);

    list->size--;

    return CONNECTION_SUCCESS;
}

/**
 * Return the node matching job_id in the list, on fail return null.
 */ 
static c_node connGetNodeById(ConnectionList list, int job_id)
{
    assert(list != NULL);
    assert(list->size >= 0);
    if(list->size == 0)
    {
        return NULL;
    }

    c_node curr = list->head->next;
    while(curr != list->tail)
    {
        if(curr->info->job_id == job_id)
        {
            return curr;
        }
    }
    return NULL;
}

void connRemoveById(ConnectionList list, int job_id)
{
    c_node node = connGetNodeById(list, job_id);
    if(node == NULL)
    {
        return;
    }
    c_node prev = node->prev;
    c_node next = node->next;
    prev->next = next; // Update the new pointers around node.
    next->prev = prev;

    freeNode(node);
}

ConnectionStruct connGetById(ConnectionList list, int job_id)
{
    c_node result = connGetNodeById(list, job_id);
    if(result == NULL)
    {
        return NULL;
    }
    return result->info;
}

ConnectionStruct connGetFirst(ConnectionList list)
{
    assert(list);
    assert(list->size >= 0);
    if(list->size == 0)
    {
        return NULL;
    }
    return list->head->next;
}

ConnectionStruct connGetLast(ConnectionList list)
{
    assert(list);
    assert(list->size >= 0);
    if(list->size == 0)
    {
        return NULL;
    }
    return list->tail->prev;
}

int connGetSize(ConnectionList list)
{
    assert(list);
    assert(list->size >= 0);
    return list->size;
}

// ********** Parallel Queue ********** //
// ***** (using connection list) ****** //
ParallelQ parallelCreateQueue()
{
    ParallelQ queue = malloc(sizeof(*queue));
    ConnectionList list = connCreateList();
    pthread_mutex_t* global_m = malloc(sizeof(*global_m));
    pthread_cond_t* cond = malloc(sizeof(*cond));
    if(!queue || !list || !global_m || !cond)
    {
        if(queue)
        {
            free(queue);
        }
        if(list)
        {
            connDestroyList(list);
        }
        if(global_m)
        {
            free(global_m);
        }
        if(cond)
        {
            free(cond);
        }
        return NULL;
    }

    pthread_mutex_init(global_m, NULL);
    pthread_cond_init(cond, NULL);

    queue->list = list;
    queue->global_m = global_m;
    queue->cond = cond;
}

ConnectionRes parallelEnqueue(ParallelQ queue, ConnectionStruct info)
{
    pthread_mutex_lock(queue->global_m);
    // Add to the queue and save the ConnectionRes to return it: <CRITICAL>
    ConnectionRes res = connPushTail(queue->list, info);
    pthread_cond_signal(queue->cond);
    // <CRITICAL-END>
    pthread_mutex_unlock(queue->global_m);
    return res;
}

ConnectionStruct parallelDequeue(ParallelQ queue)
{
    ConnectionStruct res = NULL;
    pthread_mutex_lock(queue->global_m);
    while(connGetSize(queue->list) == 0)
    {
        pthread_cond_wait(queue->cond, queue->global_m);
    }

    // Save the ConnectionStruct and pop from the list: <CRITICAL>
    res = connGetById(queue->list, connGetFirst(queue->list)->job_id);
    connPopHead(queue->list, 0);
    // <CRITICAL-END>
    pthread_mutex_unlock(queue->global_m);
    return res;
}

ConnectionStruct parallelGetConnectionStruct(ParallelQ queue, int job_id)
{
    ConnectionStruct res = NULL;
    pthread_mutex_lock(queue->global_m);
    // Find and set the ConnectionStruct: <CRITICAL>
    res = connGetById(queue->list, job_id);
    // <CRITICAL-END>
    pthread_mutex_unlock(queue->global_m);
    return res;
}

void parallelRemoveById(ParallelQ queue, int job_id)
{
    pthread_mutex_lock(queue->global_m);
    // Remove from the list: <CRITICAL>
    connRemoveById(queue->list, job_id);
    // <CRITICAL-END>
    pthread_mutex_unlock(queue->global_m);
}