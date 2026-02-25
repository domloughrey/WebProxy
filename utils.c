#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "utils.h"
#include "cache.h"

#define BUF_SIZE 16384
#define MAX_HEADER 8192
#define MAX_LINE 8192
#define MAX_RESPONSE_SIZE (100 * 1024)
#define MAX_TOTAL_SIZE 65536

#define HTTP_PORT "80"

void read_connection(int client_fd) {
    int total_read = 0;
    char buffer[BUF_SIZE];
    char buffer_copy[BUF_SIZE];

    // Keep reading data from the client until we find the end of the HTTP headers
    while (total_read < BUF_SIZE) {
        // Read data into the buffer from the client socket
        int bytes = recv(client_fd, buffer + total_read, BUF_SIZE - total_read, 0);
        if (bytes <= 0) return; // If the client disconnected or there was an error, stop
        total_read += bytes;

        // Look for the end of the headers (HTTP headers end with \r\n\r\n)
        if (strstr(buffer, "\r\n\r\n")) break; // Found the end of the header, stop reading
    }

    memcpy(buffer_copy, buffer, total_read);

    // Find last header line for logging
    char *header_end = strstr(buffer, "\r\n\r\n");
    if (!header_end) return;

    // Start from the end and move backwards to find the start of the last header line
    char *last_line = header_end;
    while (last_line > buffer) {
        if (last_line[-2] == '\r' && last_line[-1] == '\n') {
            // Found previous CRLF
            break;
        }
        last_line--;
    }

    // Copy the last line into a new string (without the \r\n at the end)
    char tail[MAX_LINE];
    size_t tail_len = header_end - last_line;
    if (tail_len >= sizeof(tail)) {
        tail_len = sizeof(tail) - 1;
    }
    strncpy(tail, last_line, tail_len);
    tail[tail_len] = '\0';

    printf("Request tail %s\n", tail); fflush(stdout);

    // Extract Host and request URL
    char method[16], url[1024], version[16];
    sscanf(buffer, "%15s %1023s %15s", method, url, version);

    char host[MAX_LINE] = {0};
    char *line = strtok(buffer_copy, "\r\n");
    const char *full_host = NULL;

    while (line) {
        if (strncasecmp(line, "Host: ", 6) == 0) {
            full_host = line + 6;
            if (*full_host == '[') {
                const char *end = strchr(full_host, ']');
                if (end) {
                    size_t len = end - full_host - 1;
                    strncpy(host, full_host + 1, len < sizeof(host) ? len : sizeof(host) - 1);
                    host[len] = '\0';
                }
            } else {
                const char *colon = strchr(full_host, ':');
                size_t len = colon ? (size_t)(colon - full_host) : strlen(full_host);
                strncpy(host, full_host, len < sizeof(host) ? len : sizeof(host) - 1);
                host[len] = '\0';
            }
            break;
        }
        line = strtok(NULL, "\r\n");
    }

    if (strlen(host) == 0) return;

    char host_for_log[MAX_LINE];
    if (full_host) {
        strncpy(host_for_log, full_host, sizeof(host_for_log) - 1);
        host_for_log[sizeof(host_for_log) - 1] = '\0';
    }

    // Check if this URI has already been cached.
    CacheEntry *entry = cache_lookup(url);
    if (entry) {
        printf("Serving localhost %s from cache\n", url);
        send_all(client_fd, entry->response, entry->length);
        return;
    }

    // If cache is full, evict the least recently used entry
    if (cache_is_full()) {
        CacheEntry *evicted = find_lru_entry();
        printf("Evicting localhost %s from cache\n", evicted->uri);
        remove_cache_entry(evicted);
    }

    printf("GETting %s %s\n", host_for_log, url); fflush(stdout);

    // Rewrite the request line
    char *path = strstr(url, "://");
    path = path ? strchr(path + 3, '/') : NULL;
    if (!path) path = "/";

    // printf("Parsed path: %s\n", path); fflush(stdout);

    char new_request[BUF_SIZE];
    int offset = snprintf(new_request, sizeof(new_request), "%s %s %s\r\n", method, path, version);

    // Append the rest of the headers
    char *headers_start = strstr(buffer, "\r\n");
    if (!headers_start) {
        return;
    }

    headers_start += 2;

    char *headers_end = strstr(headers_start, "\r\n\r\n");
    int header_len = headers_end ? (headers_end + 4 - headers_start) : strlen(headers_start);

    strncat(new_request + offset, headers_start, header_len);

    // Connect to origin server
    int origin_fd = connect_to_origin(host);
    if (origin_fd < 0) {
        return;
    } 

    // Send the rewritten request
    send_all(origin_fd, new_request, strlen(new_request));

    // Stream response headers and find Content-Length
    char response_buf[BUF_SIZE];
    int body_started = 0;
    int content_length = -1;
    int received_body = 0;

    char *response_buf_total = malloc(MAX_TOTAL_SIZE);
    int total_bytes = 0;

    while (1) {
        int bytes = recv(origin_fd, response_buf, BUF_SIZE, 0);
        if (bytes <= 0) break;

        // Safe null termination
        int safe_index = (bytes < BUF_SIZE) ? bytes : (BUF_SIZE - 1);
        response_buf[safe_index] = '\0';

        // Find Content-Length and print it once
        if (!body_started) {
            // Safe null-termination for header parsing
            int safe_index = (bytes < BUF_SIZE) ? bytes : (BUF_SIZE - 1);
            response_buf[safe_index] = '\0';
        
            char *header_end = strstr(response_buf, "\r\n\r\n");
            if (header_end) {
                body_started = 1;
                header_len = header_end + 4 - response_buf;
        
                char *cl = strcasestr(response_buf, "Content-Length: ");
                if (cl) {
                    sscanf(cl + 16, "%d", &content_length);
                    printf("Response body length %d\n", content_length); fflush(stdout);
                }
            }
        }

        send_all(client_fd, response_buf, bytes);

        if (total_bytes + bytes < MAX_TOTAL_SIZE) {
            memcpy(response_buf_total + total_bytes, response_buf, bytes);
            total_bytes += bytes;
        }

        if (body_started && content_length > 0) {
            int body_bytes_this_chunk = bytes;
            if (received_body == 0 && header_len > 0) {
                body_bytes_this_chunk -= header_len;
            }
            received_body += body_bytes_this_chunk;
        
            if (received_body >= content_length) {
                break;
            }
        }
    }
    close(origin_fd);

    // Only cache if it's a GET request, response is < 100 KiB, and URI is small
    if (strcmp(method, "GET") == 0 && total_bytes <= 102400 && strlen(url) < 2000) {
        // Now store the new response
        cache_store(url, response_buf_total, total_bytes);
    }
    free(response_buf_total);
}


int connect_to_origin(const char *host) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC; 

    if (getaddrinfo(host, HTTP_PORT, &hints, &res) != 0) {
        return -1;
    }

    int origin_fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        origin_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (origin_fd < 0) continue;
        if (connect(origin_fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(origin_fd);
        origin_fd = -1;
    }

    freeaddrinfo(res);
    return origin_fd;
}

void send_all(int fd, const char *buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(fd, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) break;
        total_sent += sent;
    }
}