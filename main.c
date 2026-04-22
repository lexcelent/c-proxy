#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

// strlen() function
#include <string.h>

// close() function
#include <unistd.h>

// atoi() function
#include <stdlib.h>

// hints, DNS-resolver
#include <netdb.h>

// poll() function
#include <poll.h>

// threads
#include <pthread.h>

#define PORT 8081
#define BUFFER_SIZE 8192


/* structures */


typedef struct task {
    void (*function)(void*);    // pointer to func
    void* argument;             // function argument
    struct task *next;          // next task
} task_t;

typedef struct thread_pool {
    pthread_t* threads;         // threads array
    task_t* queue_head;           // head of task queue
    task_t* queue_tail;           // tail of task queue
    int queue_size;
    pthread_mutex_t mutex;      // wait for tasks in queue
    pthread_cond_t condition;   // condition
} thread_pool_t;


/* signatures */


void* worker_loop(void* arg);
void handle_connection_wrapper(void* arg);
void handle_connection(int client_fd);


/* implementation of thread pool with tasks */


// thread_pool_create creates new thread pool with N threads
thread_pool_t* thread_pool_create(int count) {
    // allocate memory for struct pool
    thread_pool_t* pool = (thread_pool_t*)malloc(sizeof(thread_pool_t));
    if (!pool) {
        return NULL;
    }

    // init some things in struct
    pool->queue_head = NULL;
    pool->queue_tail = NULL;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->condition, NULL);

    // allocate memory for N-threads
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * count);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    // now create threads
    for (int i = 0; i < count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_loop, pool) != 0) {
            pthread_cond_broadcast(&pool->condition);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->condition);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

// worker_loop is a loop for threads to wait new tasks
void* worker_loop(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        // wait while no tasks in queue
        // so basically we wait for new tasks
        // in infinite loop
        while (pool->queue_size == 0) {
            pthread_cond_wait(&pool->condition, &pool->mutex);
        }

        // extract task.
        // work with linked list
        task_t* task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            if (!pool->queue_head) {
                pool->queue_tail = NULL;
            }
            pool->queue_size--;
        }

        pthread_mutex_unlock(&pool->mutex);

        // now we call function
        if (task) {
            task->function(task->argument);
            free(task);
        }
    }

    return NULL;
}

// thread_pool_add_task creates new task and adds it to queue
int thread_pool_add_task(thread_pool_t* pool, void (*function)(void*), void* argument) {
    // error handling
    if (!pool || !function) {
        return -1;
    }

    task_t* new_task = (task_t*)malloc(sizeof(task_t));
    if (!new_task) {
        return -1;
    }

    new_task->function = function;
    new_task->argument = argument;
    new_task->next = NULL;

    pthread_mutex_lock(&pool->mutex);

    if (pool->queue_tail) {
        pool->queue_tail->next = new_task;
    } else {
        // empty queue
        pool->queue_head = new_task;
    }
    pool->queue_tail = new_task;
    pool->queue_size++;

    pthread_cond_signal(&pool->condition);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}


int main() {
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "error creating socket\n");
        return -1;
    }

    // server address settings
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        fprintf(stderr, "bind socket to address failed\n");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) == -1) {
        fprintf(stderr, "listen failed\n");
        close(server_fd);
        return -1;
    }

    printf("listening on port %d\n", PORT);

    // create thread pool
    thread_pool_t* pool = thread_pool_create(4);    

    // now handle client connections
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
    
        if (client_fd == -1) {
            fprintf(stderr, "error accept client connection\n");
            continue;
        }

        // We need to do this because of void* in function arguments
        int* fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) {
            close(client_fd);
            continue;
        }
        *fd_ptr = client_fd;

        // handle connection
        thread_pool_add_task(pool, handle_connection_wrapper, fd_ptr);

        //handle_connection(client_fd);
    }

    close(server_fd);

    return 0;
}

// handle_connection_wrapper will free(arg)
void handle_connection_wrapper(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    handle_connection(client_fd);
    close(client_fd);
}

void handle_connection(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};

    char cmdConnect[] = "CONNECT";

    // We need only first line of HTTP-request packet
    // CONNECT www.host.com:443 HTTP1/1\r\n\r\n
    int bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read == 0) {
        // EOF or closed connection
        return;
    }
    
    if (bytes_read < 0) {
        fprintf(stderr, "error read socket\n");
        return;
    }

    // last symbol
    // buffer[bytes_read] = '\0';

    // if not HTTP CONNECT - close
    if (strncmp(buffer, cmdConnect, 7) != 0) {
        return;
    }

    // parse "CONNECT www.host.com:443 HTTP1/1\r\n\r\n" line
    char* first_space = strchr(buffer, ' ');
    char* colon = strchr(first_space + 1, ':');
    char* second_space = strchr(first_space + 1, ' ');
    
    char host[100] = {0};
    char port[6] = {0};

    int host_len = (int)(colon - first_space);
    strncpy(host, first_space + 1, host_len - 1);
    host[host_len] = '\0';

    int port_len = (int)(second_space - colon);
    strncpy(port, colon + 1, port_len - 1);
    port[port_len] = '\0';

    // create connect to host:port
    int target_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (target_fd == -1) {
        fprintf(stderr, "error creating socket\n");
        return;
    }

    int port_i = atoi(port);

    // target address settings
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port_i)
    };

    // resolve domain name
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        // send error to client_fd
        close(target_fd);
        return;
    }

    struct sockaddr_in *addr = (struct sockaddr_in*)res->ai_addr;
    address.sin_addr = addr->sin_addr;
    freeaddrinfo(res);

    // finally, do CONNECT
    if (connect(target_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        printf("connection with the target server failed\n");
        close(target_fd);
        return;
    }

    // connected
    const char *response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_fd, response, strlen(response), 0);

    // proxying
    struct pollfd fds[2];
    char buf_from_client[BUFFER_SIZE];
    char buf_from_target[BUFFER_SIZE];

    fds[0].fd = client_fd;
    fds[0].events = POLLIN;
    fds[1].fd = target_fd;
    fds[1].events = POLLIN;

    // timeout in ms
    int timeout = 5000; 

    while (1) {
        int ret = poll(fds, 2, timeout);
        if (ret == 0) {
            fprintf(stderr, "poll timeout\n");
            break;
        } else if (ret < 0) {
            fprintf(stderr, "error poll\n");
            break;
        }

        // send from client to server
        if (fds[0].revents & POLLIN) {
            int n = read(client_fd, buf_from_client, BUFFER_SIZE);
            if (n <= 0) {
                fprintf(stderr, "error read from client in proxying\n");
                break;
            }
            if (send(target_fd, buf_from_client, n, 0) < 0) {
                fprintf(stderr, "error send to target in proxying\n");
                break;
            }
        }

        // send from server to client
        if (fds[1].revents & POLLIN) {
            int n = read(target_fd, buf_from_target, BUFFER_SIZE);
            if (n <= 0) {
                fprintf(stderr, "error read from target\n");
                break;
            }
            if (send(client_fd, buf_from_target, n, 0) < 0) {
                fprintf(stderr, "error send to client\n");
                break;
            }
        }

        // safety
        if ((fds[0].revents & (POLLERR | POLLHUP)) || (fds[1].revents & (POLLERR | POLLHUP))) {
            break;
        }
    }
    close(target_fd);
}

