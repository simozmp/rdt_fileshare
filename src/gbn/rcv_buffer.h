#ifndef RCV_BUF
#define RCV_BUF

/*									INCLUDES								*/

#include <stdlib.h>
#include <string.h>

int rcv_buffer_init(int dimension);
ssize_t rcv_buffer_fetch(const void* buffer, size_t len);
ssize_t rcv_buffer_write(const void* buffer, size_t len);
int rcv_buffer_destroy();

#endif