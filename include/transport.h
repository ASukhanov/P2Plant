/*``````````````````Transport definitions``````````````````````````````````````
*/
/*
#include <sys/ipc.h> 
#include <sys/msg.h> 
*/
// structure for message queue 
struct MESG_BUFFER { 
    long mesg_type; 
    uint8_t mesg_text[1500]; //size of typical IP packet
}; 
int transport_init(void);
int transport_recv(uint8_t **msg);
int transport_send(uint8_t *msg, size_t msgsz);
