/*
 * test_snd_buf.c
 *
 *  Created on: 9 set 2021
 *      Author: simozmp
 */


#include "gbn/snd_buf.h"
#include "gbn/packet.h"
#include <stdio.h>

int main() {

	datapkt_t pkt;

	snd_buffer_init(5);

	for(int i=0; i<6; i++) {
		pkt.seqn = i;
		snd_buffer_push(&pkt);
		printf("Pushed pkt n. %d. Buffer content: %s\n", pkt.seqn, snd_buf_p());
	}

	for(int i=0; i<6; i++) {
		snd_buf_ack(i);
		printf("Acked pkt n. %d. Buffer content: %s\n", i, snd_buf_p());
	}

	for(int i=0; i<6; i++) {
		pkt.seqn = i;
		snd_buffer_push(&pkt);
		printf("Pushed pkt n. %d. Buffer content: %s\n", pkt.seqn, snd_buf_p());
	}


	snd_buf_ack(3);


	printf("Acked pkt n. %d. Buffer content: %s\n", 3, snd_buf_p());


	return 0;
}
