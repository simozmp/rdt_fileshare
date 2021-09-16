#include "gbn/rcv_buffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

unsigned char* rcv_buffer = NULL;
int buffer_size = -1;
int max_size = -1;
int init = 0;

pthread_mutex_t rcv_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rcv_buffer_change = PTHREAD_COND_INITIALIZER;

int rcv_buffer_init(int dimension);
ssize_t rcv_buffer_fetch(const void* buffer, size_t len);
ssize_t rcv_buffer_write(const void* buffer, size_t len);
void rcv_buffer_clear();
int rcv_buffer_destroy();


/*
 *	Allocate memory for the queue, with given dimensions (must be greater than 0)
 *
 */
int rcv_buffer_init(int dimension) {
	
	int return_value = -1;

	errno = 0;			// Zero out errrno

	if(dimension > 0) {

		//	Atomic rcv_buffer manipulation start--------------------------------
		pthread_mutex_lock(&rcv_buffer_mutex);

		// Destroy the existing stream if already initialized
		if(init)
			rcv_buffer_destroy();

		// Allocate memory
		rcv_buffer = malloc(dimension*sizeof(unsigned char));
		
		// Check malloc errors
		if(rcv_buffer != NULL) {
			buffer_size = 0;
			return_value = max_size = dimension;
			init = 1;
		} // else, errno will already be set, so nothing to do here

		pthread_mutex_unlock(&rcv_buffer_mutex);
		//	Atomic rcv_buffer manipulation end----------------------------------

	} else
		errno = ERANGE;

	return return_value;
}

/*
 *	Fetch the first len chars from the stream, and copies it into buffer.
 *
 *	Returns the size of data fetched, or -1 in error case (errno is set
 *	appropriately).
 *
 */
ssize_t rcv_buffer_fetch(const void* buffer, size_t len) {

	ssize_t return_value = -1;
	size_t tocpy = 0;

	errno = 0;		//	Zero out errno

	//	Error handling
	if(buffer == NULL)
		errno = EFAULT;		//	"Bad address"

	else if(init != 1)		//	If buffer not initialized
		errno = ENODATA;	//	"No data available", couldn't find a better fit

	else {

		//	Atomic rcv_buffer manipulation start--------------------------------
		pthread_mutex_lock(&rcv_buffer_mutex);

		// Waiting for stream to be ready
		while(!(buffer_size > 0))
			if(pthread_cond_wait(&rcv_buffer_change, &rcv_buffer_mutex) != 0)
				return -1;	//	errno will be set by the syscall

		//	Evaluate the actual lenght of the content to be copied
		tocpy = buffer_size > len ? len : buffer_size;

		//	Copying stream content to buf
		memcpy((void*) buffer, (void*) rcv_buffer, tocpy);

		//	Removing copied data from the buffer
		if(buffer_size > tocpy) {

			//	Flushing read data by moving left remaining stream content
			memmove((void*) rcv_buffer, (void*) rcv_buffer+tocpy,
					buffer_size-tocpy);
			buffer_size -= tocpy;

		} else {
			
			//	Clearing the entire buffer
			memset(rcv_buffer, 0, max_size);
			buffer_size = 0;
		
		}

		pthread_mutex_unlock(&rcv_buffer_mutex);
		//	Atomic rcv_buffer manipulation end----------------------------------

		return_value = tocpy;
	}

	return return_value;
}

/*
 *	Append the content pointed by *buffer (of lenght len) to the rcv_buffer.
 *
 *	Returns the data actually written in the queue, or -1 in error case
 *	(in that case errno is set properly).
 *
 */
ssize_t rcv_buffer_write(const void* newdata, size_t len) {

	ssize_t return_value = -1;

	errno = 0;		//	Zero out errno

	//	Error handling
	if(init == 0)			//	If buffer not initialized
		errno = ENODATA;	//	"No data available", couldn't find a better fit
	
	else if(len + buffer_size > max_size)
		errno = ENOBUFS;	//	"No buffer space available"
	
	else {

		//	Atomic rcv_buffer manipulation start--------------------------------
		pthread_mutex_lock(&rcv_buffer_mutex);

		//	Append the new data into the buffer
		memcpy(rcv_buffer + buffer_size, newdata, len);

		//	Update the size of the buffer
		buffer_size += len;
		
		return_value = len;

		// Signal the rcv_buffer change
		pthread_cond_broadcast(&rcv_buffer_change);


		pthread_mutex_unlock(&rcv_buffer_mutex);
		//	Atomic rcv_buffer manipulation end----------------------------------
	}
	
	return return_value;
}

/*
 *	This function resets the rcv_buffer.
 *
 */
void rcv_buffer_clear() {

	if(init) {
		//	Atomic rcv_buffer manipulation start--------------------------------
		pthread_mutex_lock(&rcv_buffer_mutex);
	
		memset(rcv_buffer, 0, max_size);
		buffer_size = 0;

		pthread_mutex_unlock(&rcv_buffer_mutex);
		//	Atomic rcv_buffer manipulation end----------------------------------
	}
}

/*
 *	This function destroys the current rcv_buffer.
 *
 */
int rcv_buffer_destroy() {

	int return_value = -1;

	if(init) {

		//	Atomic rcv_buffer manipulation start--------------------------------
		pthread_mutex_lock(&rcv_buffer_mutex);

		init = 0;
		free(rcv_buffer);
		buffer_size = -1;
		max_size = -1;
		return_value = 0;

		pthread_mutex_unlock(&rcv_buffer_mutex);
		//	Atomic rcv_buffer manipulation end----------------------------------
	}

	return return_value;
}
