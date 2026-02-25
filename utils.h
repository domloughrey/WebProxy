#ifndef UTILS_H
#define UTILS_H

void read_connection(int client_fd);
int connect_to_origin(const char *host);
void send_all(int fd, const char *buf, int len);

#endif