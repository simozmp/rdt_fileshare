#include "gbn/snd_buf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

struct bufnode_t{
	datapkt_t pkt;
	struct bufnode_t *next;
};


int snd_buf_capacity = 0;
int snd_buf_len = 0;

struct bufnode_t* snd_buffer_tail = NULL;
struct bufnode_t* snd_buffer_head = NULL;

/*
 *	Allocate the buffer (as a circular linked list)
 *
 *	Returns 0 in case of success, -1 if buf already initiated.
 *
 */
int snd_buffer_init(int size) {
	
	int return_value;

	if(snd_buf_capacity == 0) {

		//	Init

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
	struct bufnode_t *new_head;

	if(snd_buf_capacity > 0) {

		for(int i = 0; i<snd_buf_capacity; i++) {
			new_head = snd_buffer_head->next;
			free(snd_buffer_head);
			snd_buffer_head = new_head;
		}

		return_value = snd_buf_capacity = 0;

	} else
		return_value = -1;

	return return_value;
}

/*
 *	This functions appends the pkt as a new bufnode into the buffer
 *
 *
 */
int snd_buffer_push(datapkt_t *new_pkt) {
	
	int return_value;
	struct bufnode_t* cursor;

	errno = 0;		//	Zero out errno

	if(snd_buf_capacity > 0) {
		if(snd_buf_len == snd_buf_capacity) {
			errno = ENOBUFS;
			return_value = -1;
		} else {
			//	Reaching the first empty node
			cursor = snd_buffer_head;
			for(int i=0; i<snd_buf_len; i++)
				cursor = cursor->next;

			//	Insert in the new packet in the last position of the list
			memcpy(&cursor->pkt, new_pkt, sizeof(datapkt_t));

			snd_buf_len++;

			return_value = 0;
		}

	} else {
		errno = EIO;
		printf("Buffer not init'ed!\n");
		return_value = -1;
	}

	return return_value;
}

char* snd_buf_p(char* result) {

	int i;
	struct bufnode_t* cursor;

	//	Allocate space for the final message, a string containing 7 characters
	//	(maximum, in case of integers between 100~999) for each of the middle nodes
	//	(for the last one even less than 7 chars)
	//char* result = malloc(sizeof(char)*((snd_buf_capacity * 10)+1));
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

	return result;
}


/*
 *	Copies a packet of the window (at position base + offset) into pkt
 *
 */
int snd_buf_get(datapkt_t *pkt, int offset) {

	int return_value = -1;

	struct bufnode_t* cursor = snd_buffer_head;

	if(offset >= 0 && offset < snd_buf_len) {

		//	Moving cursor to requested node (the "offset-th" next node from head)
		for(int i=0; i<offset; i++)
			cursor = cursor->next;

		memcpy(pkt, &cursor->pkt, sizeof(datapkt_t));

		return_value = 0;
	} else
		return_value = -1;

	return return_value;
}

int snd_buf_ack(int ack_seqn) {
	int i, return_value = -1;

	errno = 0;		//	Zeroing out errno

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

			return_value = 0;
			break;
		}

	//	Make the last rotation to pass over the acked packet
	//	(or to restore initial state if matching seqn not found)
	snd_buffer_head = snd_buffer_head->next;
	snd_buffer_tail = snd_buffer_tail->next;

	return return_value;
}
