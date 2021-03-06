#include "segel.h"
#include "request.h"
#include "connection.h"

#define MIN_PORT 1025
#define POLICY_POS 4
#define CURRENTLY_DEBUGGING 0

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// ******************************************//
// Global mutex lock and condition variables:
pthread_mutex_t global_m;
pthread_cond_t  cond;
pthread_cond_t  cond_policy;
// ******************************************//
// Struct to pass arguments to the thread_do_work routine handler:
typedef struct thread_args
{
    ConnectionList to_do_list;
    ConnectionList busy_list;
    int thread_id;
} ThreadArgs;

// ******************************************//

void checkValidity(int port, int threads_num, int queue_size, char *argv[]);
void* threadDoWork(void* args);
void blockPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag);
void dhPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag);
void dtPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag);
void randomPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag);

static int randInt(int max);
static int myCeil(double num);

void getargs(int *port, int *threads_num, int *q_size, int argc, char *argv[])
{
    if (argc < 5) 
    {
        fprintf(stderr, "Usage: %s <port> <threads> <queue-size> <schedalg>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads_num = atoi(argv[2]);
    *q_size = atoi(argv[3]);
}

void checkValidity(int port, int threads_num, int queue_size, char *argv[])
{
    if(port < MIN_PORT)
    {
        fprintf(stderr, "Error: port must be a positive number larger than %d.\nYou entered: %s.\n", MIN_PORT, argv[1]);
        exit(1);
    }
    if(threads_num <= 0)
    {
        fprintf(stderr, "Error: threads_num must be a positive integer.\nYou entered: %s.\n", argv[2]);
        exit(1);
    }
    if(queue_size <= 0)
    {
        fprintf(stderr, "Error: queue_size must be a positive integer.\nYou entered: %s.\n", argv[3]);
        exit(1);
    }
    if(strcmp(argv[POLICY_POS], "block") && strcmp(argv[POLICY_POS], "dt") \
    && strcmp(argv[POLICY_POS], "dh") && strcmp(argv[POLICY_POS], "random"))
    {
        fprintf(stderr, "Error: schedalg must be one of the following: block|dt|dh|random\n");
        exit(1);
    }
}


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, threads_num, q_size, clientlen;
    bool skip_flag = false;
    struct sockaddr_in clientaddr;
    // to_do_list: List of requests waiting to be processed by a worker thread (buffer).
    // busy_list:  List of requests currently being worked on by a worker thread.
    ConnectionList to_do_list, busy_list;
    void (*overloadPolicy)(ConnectionList, ConnectionList, int, ConnectionStruct, bool*) = NULL;

    getargs(&port, &threads_num, &q_size, argc, argv);
    checkValidity(port, threads_num, q_size, argv); // If this fails the server will close.

    if(!strcmp(argv[POLICY_POS], "block"))
    {
        overloadPolicy = blockPolicy;
    }
    else if(!strcmp(argv[POLICY_POS], "dh"))
    {
        overloadPolicy = dhPolicy;
    }
    else if(!strcmp(argv[POLICY_POS], "dt"))
    {
        overloadPolicy = dtPolicy;
        skip_flag = true;
    }
    else if(!strcmp(argv[POLICY_POS], "random"))
    {
        overloadPolicy = randomPolicy;
    }
    
    // Initialize locks and condition variables:
    pthread_mutex_init(&global_m, NULL);
    pthread_cond_init(&cond, NULL);
    pthread_cond_init(&cond_policy, NULL);

    // Create the lists:
    if(!(to_do_list = connCreateList()))
    {
        perror("Error: to_do_list creation failed");
        return 1;
    }
    if(!(busy_list = connCreateList()))
    {
        perror("Error: busy_list creation failed");
        connDestroyList(to_do_list);
        return 1;
    }
    
    // Open the listening socket:
    listenfd = Open_listenfd(port);
    
    // Allocate threads array and args structs array:
    pthread_t *threads = (pthread_t*)malloc(threads_num * sizeof(*threads)); // Allocate space for the thread identifiers
    ThreadArgs *t_args = (ThreadArgs*)malloc(threads_num * sizeof(*t_args)); // Alocate space for the arguments of the threads
    if(threads == NULL)
    {
        perror("Error: threads allocation failed");
        connDestroyList(to_do_list);
        connDestroyList(busy_list);
        return 1;
    }
    if(t_args == NULL)
    {
        perror("Error: t_args allocation failed");
        connDestroyList(to_do_list);
        connDestroyList(busy_list);
        free(threads);
        return 1;
    }

    //  Actually create the threads:
    for(int i = 0; i < threads_num; i++)
    {
        // Insert the arguments
        t_args[i].to_do_list = to_do_list;
        t_args[i].busy_list = busy_list;
        t_args[i].thread_id = i;

        // Create the thread
        if(pthread_create(&threads[i], NULL, threadDoWork, &t_args[i]) != 0)
        {
            fprintf(stderr, "Error: thread number %d failed to create: %s\n", i, strerror(errno));
            pthread_mutex_lock(&global_m);
            threads_num--; // Try to work with one less thread if failed to create.
            pthread_mutex_unlock(&global_m);
            if(threads_num == 0)
            {
                fprintf(stderr, "Error: no thread managed to be created, aborting server creation.\n");
                connDestroyList(to_do_list);
                connDestroyList(busy_list);
                free(threads);
                free(t_args);
                exit(1);
            }
        }
    }

    int job_id = 0;
    while (1) 
    {
        bool skip_full_flag = false;
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        
        ConnectionStruct cd = (ConnectionStruct)malloc(sizeof(*cd));
        if(!cd)
        {
            perror("Error: connection struct allocation fail");
            continue;
        }
        cd->job_id = job_id++;
        cd->connfd = connfd;
        gettimeofday(&(cd->arrival), NULL); // This function is obsolete, better to use clock_gettime instead.
        
        pthread_mutex_lock(&global_m);
        // <CRITICAL>
        // Make sure there is enough space in the to_do_list:
        if(connGetSize(to_do_list) + connGetSize(busy_list) + 1 > q_size)
        {
            overloadPolicy(to_do_list, busy_list, q_size, cd, &skip_full_flag);
            if(skip_flag || skip_full_flag)
            {
                // <CRITICAL-END>
                pthread_mutex_unlock(&global_m);
                continue;
            }
        }
        // If we get here, there is enough space for one more connection in the buffer (to_do_list).
        // Add the ConnectionStruct to the to_do_list:
        ConnectionRes res = connPushTail(to_do_list, cd);
        if(res == CONNECTION_OUT_OF_MEMORY)
        {
            fprintf(stderr, "Error: failed pushing the request into queue: allocation fail\n");
            Close(connfd);
            free(cd);
            continue;
        }
        pthread_cond_signal(&cond);
        // <CRITICAL-END>
        pthread_mutex_unlock(&global_m);
    }
}

static int randInt(int max)
{
    int res = 0;
    static int feed = 251640;
    srand(time(NULL)*(++feed));
    res = (rand()*feed) % (max + 1);
    return abs(res);
}

static int myCeil(double num)
{
    int inum = (int)num;
    if((double)inum == num)
    {
        return inum;
    }
    return num + 1;
}

void* threadDoWork(void* args)
{
    ConnectionStruct res = NULL;
    ThreadArgs *t_args = ((ThreadArgs*)args);
    ThreadStats t_stats = (ThreadStats)malloc(sizeof(*t_stats));
    if(!t_stats)
    {
        fprintf(stderr, "Error: failed to allocate thread stats for thread %d: %s\nClosing thread.\n", t_args->thread_id, strerror(errno));
        pthread_exit(NULL);
    }
    
    // Initialize thread stats:
    t_stats->thread_id = t_args->thread_id;
    t_stats->thread_count = t_stats->thread_static = t_stats->thread_dynamic = 0;

    while(1)
    {
        pthread_mutex_lock(&global_m);
        while(connGetSize(t_args->to_do_list) == 0)
        {
            pthread_cond_wait(&cond, &global_m);
        }
        // <CRITICAL>
        // Pull the request from the to do list:
        res = connGetFirst(t_args->to_do_list);
        connPopHead(t_args->to_do_list, false);
        // Push the request to the busy list, embedded with the dispatch time:
        gettimeofday(&(res->dispatch), NULL); // This function is obsolete, better to use clock_gettime instead.
        connPushHead(t_args->busy_list, res);
        // <CRITICAL-END>
        pthread_mutex_unlock(&global_m);

        requestHandle(res, t_stats); // PROCESS THE REQUEST.
        Close(res->connfd);
        
        pthread_mutex_lock(&global_m);
        // <CRITICAL>
        connRemoveById(t_args->busy_list, res->job_id);
        pthread_cond_signal(&cond_policy);
        // <CRITICAL-END>
        pthread_mutex_unlock(&global_m);
    }
    
    return NULL;
}

// ***** Block Policy ***** //
void blockPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag)
{
    #if CURRENTLY_DEBUGGING == 1
        printf("Block policy entry -->\n");
    #endif

    while(connGetSize(to_do_list) + connGetSize(busy_list) + 1 > q_size)
    {
        pthread_cond_wait(&cond_policy, &global_m);
    }

    #if CURRENTLY_DEBUGGING == 1
        printf("<-- Block policy exit\n");
    #endif
}

// ****** DH Policy ****** //
void dhPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag)
{
    #if CURRENTLY_DEBUGGING == 1
        printf("DH policy entry -->\n");
    #endif
    if(connGetSize(to_do_list) == 0)
    {
        #if CURRENTLY_DEBUGGING == 1
            printf("<-- DH policy exit (dropped current request)\n");
        #endif

        Close(cd->connfd);
        free(cd);
        *skip_full_flag = true;
        return;
    }
    Close(connGetLast(to_do_list)->connfd);
    connPopTail(to_do_list, true);

    #if CURRENTLY_DEBUGGING == 1
        printf("<-- DH policy exit (dropped oldest request)\n");
    #endif
}

// ****** DT Policy ****** //
void dtPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag)
{
    #if CURRENTLY_DEBUGGING == 1
        printf("DT policy entry -->\n");
    #endif

    Close(cd->connfd);
    free(cd);

    #if CURRENTLY_DEBUGGING == 1
        printf("<-- DT policy exit (dropped current request)\n");
    #endif
}

// **** Random Policy **** //
void randomPolicy(ConnectionList to_do_list, ConnectionList busy_list, int q_size, ConnectionStruct cd, bool* skip_full_flag)
{
    #if CURRENTLY_DEBUGGING == 1
        printf("RANDOM policy entry -->\n");
    #endif

    int size = connGetSize(to_do_list);
    int to_remove = myCeil((double)size/4);
    ConnectionStruct tmp = NULL;
    
    if(size == 0)
    {
        #if CURRENTLY_DEBUGGING == 1
            printf("<-- RANDOM policy exit\n");
        #endif

        Close(cd->connfd);
        free(cd);
        *skip_full_flag = true;
        return;
    }

    int rand_index = 0;
    int job_id = -1;

    #if CURRENTLY_DEBUGGING == 1
        printf("RANDOM: %d/%d to remove\n", to_remove, size);
    #endif

    while(to_remove)
    {
        rand_index = randInt(size-1);
        tmp = connGetIthElement(to_do_list, rand_index);
        job_id = tmp->job_id;
        Close(tmp->connfd);
        connRemoveById(to_do_list, job_id);
        size--;
        to_remove--;

        #if CURRENTLY_DEBUGGING == 1
            printf("RANDOM: index %d removed (%d left)\n", rand_index, to_remove);
        #endif
    }

    #if CURRENTLY_DEBUGGING == 1
        printf("<-- RANDOM policy exit\n");
    #endif
}
// *********************** //