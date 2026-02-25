#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

typedef struct CacheEntry {
    char *uri;
    char *response;
    size_t length;
    struct CacheEntry *next;
} CacheEntry;

CacheEntry *cache_lookup(const char *uri);
void cache_store(const char *uri, const char *response_data, size_t length);
void remove_cache_entry(CacheEntry *e);
void cache_free_all(void);
CacheEntry *find_lru_entry(void);
int cache_is_full(void);


#endif