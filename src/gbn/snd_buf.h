#ifndef SND_BUF
#define SND_BUF

#include <pthread.h>
#include "gbn/packet.h"


int snd_buffer_init(int size);
int snd_buffer_destroy();
int snd_buffer_push(datapkt_t *pkt);	//	Pushes the packet
int snd_buf_get(datapkt_t *pkt, int offset);
struct timespec snd_buf_ack(int ack_seqn);
char* snd_buf_p();
int snd_buf_mark_snt(int pkt_to_mark);

#endif
