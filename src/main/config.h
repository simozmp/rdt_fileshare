#ifndef CONFIG_H
#define CONFIG_H

//	Server address and port
#define SERV_ADDR			"127.0.0.1"
#define SERV_PORT			5193

//	Max promptline supported by application
#define MAXLINE				1024

//	Max command supported by application
#define MAXCOMMAND			10

//	Max argument supported by application
#define MAXARGUMENT			50

//	Size of file chunks to send
#define FILE_CHUNK_SIZE		500000

#endif
