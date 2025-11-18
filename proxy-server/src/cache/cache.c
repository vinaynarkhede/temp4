#include "../../include/proxy.h"
#include <sys/queue.h>

/* Cache entry structure */
struct cache_entry {
    char *key;
    void *data;
    size_t size;
    time_t expiry;
    LIST_ENTRY(cache_entry) hash_entries;
    TAILQ_ENTRY(cache_entry) lru_entries;
};

/* Hash table bucket */
LIST_HEAD(hash_bucket, cache_entry);

/* Cache structure */
typedef struct {
    struct hash_bucket *buckets;
    size_t num_buckets;
    TAILQ_HEAD(lru_list, cache_entry) lru;
    size_t total_size;
    size_t max_size;
    pthread_mutex_t mutex;
    int hits;
    int misses;
} cache_t;

static cache_t *g_cache = NULL;

/* Simple hash function */
static unsigned int hash_key(const char *key, size_t num_buckets) {
    unsigned int hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % num_buckets;
}

int cache_init(size_t size_mb) {
    g_cache = malloc(sizeof(cache_t));
    if (!g_cache) {
        return -1;
    }

    memset(g_cache, 0, sizeof(cache_t));

    g_cache->max_size = size_mb * 1024 * 1024;
    g_cache->num_buckets = 1024;
    g_cache->total_size = 0;
    g_cache->hits = 0;
    g_cache->misses = 0;

    /* Allocate hash table */
    g_cache->buckets = calloc(g_cache->num_buckets, sizeof(struct hash_bucket));
    if (!g_cache->buckets) {
        free(g_cache);
        g_cache = NULL;
        return -1;
    }

    /* Initialize hash buckets */
    for (size_t i = 0; i < g_cache->num_buckets; i++) {
        LIST_INIT(&g_cache->buckets[i]);
    }

    /* Initialize LRU list */
    TAILQ_INIT(&g_cache->lru);

    pthread_mutex_init(&g_cache->mutex, NULL);

    printf("[INFO] Cache initialized: %zu MB capacity\n", size_mb);
    return 0;
}

static void cache_entry_destroy(cache_entry_t *entry) {
    if (!entry) {
        return;
    }

    if (entry->key) {
        free(entry->key);
    }

    if (entry->data) {
        free(entry->data);
    }

    free(entry);
}

static void cache_evict_lru(void) {
    if (TAILQ_EMPTY(&g_cache->lru)) {
        return;
    }

    /* Get least recently used entry */
    cache_entry_t *entry = TAILQ_FIRST(&g_cache->lru);

    /* Remove from LRU list */
    TAILQ_REMOVE(&g_cache->lru, entry, lru_entries);

    /* Remove from hash table */
    unsigned int bucket = hash_key(entry->key, g_cache->num_buckets);
    LIST_REMOVE(entry, hash_entries);

    /* Update size */
    g_cache->total_size -= entry->size;

    /* Destroy entry */
    cache_entry_destroy(entry);
}

int cache_get(const char *key, void **data, size_t *size) {
    if (!g_cache || !key || !data || !size) {
        return -1;
    }

    pthread_mutex_lock(&g_cache->mutex);

    unsigned int bucket = hash_key(key, g_cache->num_buckets);
    cache_entry_t *entry;

    /* Search in hash table */
    LIST_FOREACH(entry, &g_cache->buckets[bucket], hash_entries) {
        if (strcmp(entry->key, key) == 0) {
            /* Check if expired */
            if (entry->expiry > 0 && time(NULL) > entry->expiry) {
                /* Entry expired, remove it */
                TAILQ_REMOVE(&g_cache->lru, entry, lru_entries);
                LIST_REMOVE(entry, hash_entries);
                g_cache->total_size -= entry->size;
                cache_entry_destroy(entry);
                g_cache->misses++;
                pthread_mutex_unlock(&g_cache->mutex);
                return -1;
            }

            /* Move to end of LRU (most recently used) */
            TAILQ_REMOVE(&g_cache->lru, entry, lru_entries);
            TAILQ_INSERT_TAIL(&g_cache->lru, entry, lru_entries);

            /* Copy data */
            *data = malloc(entry->size);
            if (!*data) {
                pthread_mutex_unlock(&g_cache->mutex);
                return -1;
            }
            memcpy(*data, entry->data, entry->size);
            *size = entry->size;

            g_cache->hits++;
            pthread_mutex_unlock(&g_cache->mutex);
            return 0;
        }
    }

    g_cache->misses++;
    pthread_mutex_unlock(&g_cache->mutex);
    return -1;
}

int cache_put(const char *key, const void *data, size_t size, int ttl) {
    if (!g_cache || !key || !data || size == 0) {
        return -1;
    }

    pthread_mutex_lock(&g_cache->mutex);

    /* Don't cache if size exceeds max */
    if (size > g_cache->max_size / 2) {
        pthread_mutex_unlock(&g_cache->mutex);
        return -1;
    }

    unsigned int bucket = hash_key(key, g_cache->num_buckets);

    /* Check if key already exists */
    cache_entry_t *entry;
    LIST_FOREACH(entry, &g_cache->buckets[bucket], hash_entries) {
        if (strcmp(entry->key, key) == 0) {
            /* Update existing entry */
            void *new_data = malloc(size);
            if (!new_data) {
                pthread_mutex_unlock(&g_cache->mutex);
                return -1;
            }

            memcpy(new_data, data, size);

            /* Update size */
            g_cache->total_size -= entry->size;
            g_cache->total_size += size;

            /* Free old data and update */
            free(entry->data);
            entry->data = new_data;
            entry->size = size;
            entry->expiry = (ttl > 0) ? time(NULL) + ttl : 0;

            /* Move to end of LRU */
            TAILQ_REMOVE(&g_cache->lru, entry, lru_entries);
            TAILQ_INSERT_TAIL(&g_cache->lru, entry, lru_entries);

            pthread_mutex_unlock(&g_cache->mutex);
            return 0;
        }
    }

    /* Evict entries if necessary */
    while (g_cache->total_size + size > g_cache->max_size) {
        cache_evict_lru();
    }

    /* Create new entry */
    entry = malloc(sizeof(cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&g_cache->mutex);
        return -1;
    }

    entry->key = strdup(key);
    entry->data = malloc(size);
    if (!entry->key || !entry->data) {
        cache_entry_destroy(entry);
        pthread_mutex_unlock(&g_cache->mutex);
        return -1;
    }

    memcpy(entry->data, data, size);
    entry->size = size;
    entry->expiry = (ttl > 0) ? time(NULL) + ttl : 0;

    /* Add to hash table */
    LIST_INSERT_HEAD(&g_cache->buckets[bucket], entry, hash_entries);

    /* Add to LRU list (most recently used) */
    TAILQ_INSERT_TAIL(&g_cache->lru, entry, lru_entries);

    g_cache->total_size += size;

    pthread_mutex_unlock(&g_cache->mutex);
    return 0;
}

void cache_cleanup(void) {
    if (!g_cache) {
        return;
    }

    pthread_mutex_lock(&g_cache->mutex);

    /* Clear all entries */
    cache_entry_t *entry;
    while ((entry = TAILQ_FIRST(&g_cache->lru))) {
        TAILQ_REMOVE(&g_cache->lru, entry, lru_entries);
        cache_entry_destroy(entry);
    }

    free(g_cache->buckets);

    int hits = g_cache->hits;
    int misses = g_cache->misses;
    int total = hits + misses;
    float hit_rate = total > 0 ? (float)hits / total * 100.0f : 0.0f;

    pthread_mutex_unlock(&g_cache->mutex);
    pthread_mutex_destroy(&g_cache->mutex);

    printf("[INFO] Cache statistics: Hits=%d, Misses=%d, Hit Rate=%.2f%%\n",
           hits, misses, hit_rate);

    free(g_cache);
    g_cache = NULL;
}
