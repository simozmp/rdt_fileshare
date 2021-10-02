#ifndef PACKET_H
#define PACKET_H

#include <sys/types.h>
#include <stdint.h>

#include "gbn/config.h"

/*									TYPEDEFS								*/
typedef struct {
	int type;
	unsigned int seqn;
	unsigned int len;
	unsigned char payload[PCKDATASIZE];
	uint16_t checksum;
} datapkt_t;

typedef struct {
	int type;
	unsigned int seqn;
	uint16_t checksum;
} servicepkt_t;

enum pkt_type {
	DATA = 1,
	ACK,
	SYN,
	SYNACK,
	FIN,
	FINACK
};

/*								PROTOTYPES									*/

int make_datapkt(int seq, void *message, size_t message_len,
		datapkt_t* packet);
int make_servicepkt(int seq, const int type, servicepkt_t* packet);
int verify_datapkt(datapkt_t *pkt);
int verify_servicepkt(servicepkt_t *pkt);
void print_servicepkt(servicepkt_t *pkt);

#endif

