#include "gbn/snd_buf.h"
#include "gbn/gbn_utils.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

//	List node
struct bufnode_t{
	datapkt_t pkt;					//	datapkt
	struct timespec snd_time;		//	Time of last send attempt for the pkt
	struct bufnode_t *next;			//	Pointer to the next node
};

//	An integer to keep the total number of pkt nodes in the list
int snd_buf_capacity = 0;

//	An integer to keep the number of valid pkts in the list
int snd_buf_len = 0;

//	Pointers to the head and tail nodes
struct bufnode_t* snd_buffer_tail = NULL;
struct bufnode_t* snd_buffer_head = NULL;

pthread_mutex_t snd_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 *	Allocate the buffer (as a circular linked list)
 *
 *	Returns 0 in case of success, -1 if buf already initiated.
 *
 */
int snd_buffer_init(int size) {
	
	int return_value;

	if(snd_buf_capacity == 0) {		//	Init

		//	Creating first node
		snd_buffer_head = (struct bufnode_t*) malloc(sizeof(struct bufnode_t));
		memset(snd_buffer_head, 0, sizeof(struct bufnode_t));
		snd_buffer_tail = snd_buffer_head;

		snd_buf_capacity = 1;

		//	Create the remaining size-1 nodes
		for(int i = 0; i < size-1; i++) {

			//	Adding a new node
			snd_buffer_tail->next = (struct bufnode_t*) malloc(sizeof(struct bufnode_t));

			memset(snd_buffer_tail->next, 0, sizeof(struct bufnode_t));

			//	Making snd_buffer_tail point the new tail
			snd_buffer_tail = snd_buffer_tail->next;

			//	Writing reference to head
			snd_buffer_tail->next = snd_buffer_head;

			snd_buf_capacity++;
		}

		
		return_value = 0;
	} else
		return_value = -1;

	return return_value;
}

/*
 *	This functions destroys the list by deleting each node. Returns 0 on success.
 *
 */
int snd_buffer_destroy() {
	
	int return_value;
	struct bufnode_t *next_head;

	if(snd_buf_capacity > 0) {

		for(int i = 0; i<snd_buf_capacity; i++) {
			next_head = snd_buffer_head->next;
			free(snd_buffer_head);
			snd_buffer_head = next_head;
		}

		return_value = snd_buf_capacity = 0;

	} else
		return_value = -1;

	return return_value;
}

/*
 *	This functions appends the pkt as a new bufnode into the buffer.
 *
 *	Returns 0 in case of success, -1 in case of error (errno is set
 *	appropriately in this case).
 *
 */
int snd_buffer_push(datapkt_t *new_pkt) {
	
	int return_value;
	struct bufnode_t* cursor;

	errno = 0;		//	Zero out errno

	pthread_mutex_lock(&snd_buf_mutex);

	//	Buffer not init-ed check
	if(snd_buf_capacity > 0) {
		//	Buffer full check
		if(snd_buf_len < snd_buf_capacity) {
			//	Reaching the first empty node
			cursor = snd_buffer_head;
			for(int i=0; i<snd_buf_len; i++)
				cursor = cursor->next;

			//	Insert in the new packet in the last position of the list
			memcpy(&cursor->pkt, new_pkt, sizeof(datapkt_t));

			snd_buf_len++;

			return_value = 0;
		} else {
			errno = ENOBUFS;
			return_value = -1;
		}

	} else {
		errno = EIO;
		log_write("snd_buf(): Buffer not init'ed!\n");
		return_value = -1;
	}

	pthread_mutex_unlock(&snd_buf_mutex);

	return return_value;
}

/*
 *	Saves a snd_buf presentation string into the result buffer.
 *	Pkts are represented with their seqn.
 *
 *	Returns the pointer to result.
 *
 */
char* snd_buf_p(char* result) {

	int i;
	struct bufnode_t* cursor;

	pthread_mutex_lock(&snd_buf_mutex);

	/*
	 *	Allocate space for a partial presentation, a string containing 8 characters
	 *	(maximum, in case of integers between 1000~9999) for each of the middle nodes
	 *	(for the last one even less than 8 chars). It is used to prevent sprintf
	 *	issues by overlapping source and destination
	 */
	char* partial_result = malloc(sizeof(char)*((snd_buf_capacity * 10)+1));

	result[0] = '\0';
	partial_result[0] = '\0';

	cursor = snd_buffer_head;

	i=0;

	do {
		if(i<snd_buf_len)
			sprintf(result, "%s[%d]->", partial_result, cursor->pkt.seqn);
		else
			sprintf(result, "%s[_]->", partial_result);

		strcpy(partial_result, result);

		cursor = cursor->next;
		i++;
	} while(cursor != snd_buffer_head);

	result[strlen(result)-2] = '\0';

	free(partial_result);

	pthread_mutex_unlock(&snd_buf_mutex);

	return result;
}


/*
 *	Copies a packet of the window (at position base + offset) into pkt
 *
 */
int snd_buf_get(datapkt_t *pkt, int offset) {

	int return_value = -1;

	struct bufnode_t* cursor = snd_buffer_head;

	pthread_mutex_lock(&snd_buf_mutex);

	if(offset >= 0 && offset < snd_buf_len) {

		//	Moving cursor to requested node (the "offset-th" next node from head)
		for(int i=0; i<offset; i++)
			cursor = cursor->next;

		memcpy(pkt, &cursor->pkt, sizeof(datapkt_t));

		return_value = 0;
	} else
		return_value = -1;

	pthread_mutex_unlock(&snd_buf_mutex);

	return return_value;
}

/*
 *	This functions acks the packet with ack_seqn from the buffer. This results
 *	in deleting each packet in the buffer, up to the acked packet (included).
 *
 *	The function returns a struct timespec value, which represents the RTT
 *	(in-flight time) of the acked packet.
 *
 *	If no pkt with given seqn is found in the list, all the fields in the returning
 *	structure will be set to 0
 *
 */
struct timespec snd_buf_ack(int ack_seqn) {
	int i;
	struct timespec return_value, now;

	pthread_mutex_lock(&snd_buf_mutex);

	timespec_get(&now, TIME_UTC);

	return_value.tv_nsec = 0;
	return_value.tv_sec = 0;

	//	For each node in the buffer
	for(i=0; i < snd_buf_len; i++)
		//	Compare the seqn
		if(snd_buffer_head->pkt.seqn != ack_seqn) {
			//	Rotate the list of 1 node if seqn not matching
			snd_buffer_head = snd_buffer_head->next;
			snd_buffer_tail = snd_buffer_tail->next;
		} else {
			//	When matching seqn, reduce the lenght by (i+1) nodes
			snd_buf_len -= (i+1);

			//	Collect the RTT information by calculating "now - snd_time"
			return_value = ts_diff(now, snd_buffer_head->snd_time);

			break;
		}

	//	Make the last rotation to pass over the acked packet
	//	(or to restore initial state if matching seqn not found)
	snd_buffer_head = snd_buffer_head->next;
	snd_buffer_tail = snd_buffer_tail->next;

	pthread_mutex_unlock(&snd_buf_mutex);

	return return_value;
}

/*
 *	This functions marks the packet with given seqn as sent,
 *	with current timestamp. This is gonna be called asap after a packet send.
 *
 *	The function returns -1 if no pkt with given seqn is found, or 0
 *	in case of success
 *
 */
int snd_buf_mark_snt(int seqn_to_mark) {
	int i, return_value = -1;

	pthread_mutex_lock(&snd_buf_mutex);

	//	Starting with a cursor pointing to the head node
	struct bufnode_t *cursor = snd_buffer_head;

	//	For each valid node in the buffer
	for(i=0; i < snd_buf_len; i++)
		//	Compare the seqn
		if(cursor->pkt.seqn != seqn_to_mark) {
			//	Rotate the list of 1 node if seqn not matching
			cursor = cursor->next;
		} else {
			//	When matching seqn, reduce the lenght by (i+1) nodes
			timespec_get(&cursor->snd_time, TIME_UTC);

			return_value = 0;
			break;
		}

	pthread_mutex_unlock(&snd_buf_mutex);

	return return_value;
}
