#include "../../include/proxy.h"

/* Work queue item */
typedef struct work_item {
    void (*function)(void *);
    void *argument;
    struct work_item *next;
} work_item_t;

/* Work queue */
typedef struct work_queue {
    work_item_t *head;
    work_item_t *tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} work_queue_t;

/* Thread pool structure */
struct thread_pool {
    pthread_t *threads;
    int num_threads;
    work_queue_t queue;
    volatile int shutdown;
    pthread_mutex_t mutex;
};

static void* worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    work_item_t *item;

    while (1) {
        pthread_mutex_lock(&pool->queue.mutex);

        /* Wait for work or shutdown signal */
        while (pool->queue.count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue.cond, &pool->queue.mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue.mutex);
            break;
        }

        /* Get work item */
        item = pool->queue.head;
        if (item) {
            pool->queue.head = item->next;
            if (pool->queue.head == NULL) {
                pool->queue.tail = NULL;
            }
            pool->queue.count--;
        }

        pthread_mutex_unlock(&pool->queue.mutex);

        /* Execute work */
        if (item) {
            item->function(item->argument);
            free(item);
        }
    }

    return NULL;
}

thread_pool_t* thread_pool_create(int num_threads) {
    thread_pool_t *pool = malloc(sizeof(thread_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->shutdown = 0;

    /* Initialize work queue */
    pool->queue.head = NULL;
    pool->queue.tail = NULL;
    pool->queue.count = 0;
    pthread_mutex_init(&pool->queue.mutex, NULL);
    pthread_cond_init(&pool->queue.cond, NULL);
    pthread_mutex_init(&pool->mutex, NULL);

    /* Create worker threads */
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            fprintf(stderr, "[ERROR] Failed to create thread %d\n", i);
            pool->num_threads = i;
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    printf("[INFO] Thread pool created with %d worker threads\n", num_threads);
    return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* Signal shutdown */
    pthread_mutex_lock(&pool->queue.mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->queue.cond);
    pthread_mutex_unlock(&pool->queue.mutex);

    /* Wait for all threads to finish */
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    /* Clean up remaining work items */
    work_item_t *item = pool->queue.head;
    while (item) {
        work_item_t *next = item->next;
        free(item);
        item = next;
    }

    /* Destroy mutexes and condition variables */
    pthread_mutex_destroy(&pool->queue.mutex);
    pthread_cond_destroy(&pool->queue.cond);
    pthread_mutex_destroy(&pool->mutex);

    free(pool->threads);
    free(pool);

    printf("[INFO] Thread pool destroyed\n");
}

int thread_pool_submit(thread_pool_t *pool, void (*func)(void*), void *arg) {
    if (!pool || !func) {
        return -1;
    }

    /* Create work item */
    work_item_t *item = malloc(sizeof(work_item_t));
    if (!item) {
        return -1;
    }

    item->function = func;
    item->argument = arg;
    item->next = NULL;

    /* Add to queue */
    pthread_mutex_lock(&pool->queue.mutex);

    if (pool->queue.tail) {
        pool->queue.tail->next = item;
    } else {
        pool->queue.head = item;
    }
    pool->queue.tail = item;
    pool->queue.count++;

    pthread_cond_signal(&pool->queue.cond);
    pthread_mutex_unlock(&pool->queue.mutex);

    return 0;
}
