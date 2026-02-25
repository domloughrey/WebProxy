#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_URI_LEN 2048
#define CACHE_LIMIT 10  // Max number of entries in the cache

typedef struct CacheEntry {
    char *uri;
    char *response;
    size_t length;
    unsigned long use_time; // for LRU tracking
    struct CacheEntry *next;
} CacheEntry;

static unsigned long global_time = 0; // increment on every access
static int cache_size = 0; // Track number of entries

static CacheEntry *cache_head = NULL;

// Lookup a response in cache by URI
CacheEntry *cache_lookup(const char *uri) {
    CacheEntry *current = cache_head;
    while (current) {
        if (strcmp(current->uri, uri) == 0) {
            current->use_time = ++global_time;
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Add a new response to the cache
void cache_store(const char *uri, const char *response_data, size_t length) {
    CacheEntry *new_entry = malloc(sizeof(CacheEntry));
    if (!new_entry) return;

    new_entry->uri = strdup(uri);
    new_entry->response = malloc(length);
    if (!new_entry->response) {
        free(new_entry->uri);
        free(new_entry);
        return;
    }
    memcpy(new_entry->response, response_data, length);
    new_entry->length = length;
    new_entry->use_time = ++global_time;
    new_entry->next = cache_head;
    cache_head = new_entry;
    cache_size++;
}

// Returns true if the cache is full
int cache_is_full(void) {
    return cache_size >= CACHE_LIMIT;
}

// Finds the least recently used (LRU) cache entry
CacheEntry *find_lru_entry(void) {
    CacheEntry *current = cache_head;
    CacheEntry *lru = NULL;
    unsigned long min_time = ~0UL;

    while (current) {
        if (current->use_time < min_time) {
            min_time = current->use_time;
            lru = current;
        }
        current = current->next;
    }
    return lru;
}

// Removes a given cache entry from the list
void remove_cache_entry(CacheEntry *e) {
    if (!e || !cache_head) return;

    if (e == cache_head) {
        cache_head = e->next;
    } else {
        CacheEntry *prev = cache_head;
        while (prev->next && prev->next != e) {
            prev = prev->next;
        }
        if (prev->next == e) {
            prev->next = e->next;
        }
    }

    free(e->uri);
    free(e->response);
    free(e);
    cache_size--;
}

// Frees all cached entries from memory (optional cleanup)
void cache_free_all() {
    CacheEntry *current = cache_head;
    while (current) {
        CacheEntry *next = current->next;
        free(current->uri);
        free(current->response);
        free(current);
        current = next;
    }
    cache_head = NULL;
    cache_size = 0;
}