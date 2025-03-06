/*`````````````````````````````````````````````````````````````````````````````
* Send/receive data to client, using IPC message queue.
* Note, If insufficient space is available in the queue, 
* then the default behavior of msgsnd() is to block until
* space becomes available.
*/
#include <stdio.h>

//#include <sys/ipc.h> 
#include <sys/msg.h>

#include "../include/defines.h"

//``````````````````Transport variables````````````````````````````````````````
// structure for message queue
//#define MESG_BUFFER_SIZE 10000 //size of typical IP packet
struct MESG_BUFFER {// For IPC communications
    long mesg_type; 
    uint8_t mesg_buf[];
}; 
static MESG_BUFFER *recvBuffer;

static int msgid_rcv, msgid_snd;
static int msglen = 1;
static uint32_t recvBufSize = 0;

//``````````````````Transport functions````````````````````````````````````````
int transport_init(uint8_t *buf, uint32_t bufsz){
    recvBufSize = bufsz;
    recvBuffer = (MESG_BUFFER*) buf;
    
    key_t key; 
    // ftok to generate unique key
    key = ftok("/tmp/ipcbor.ftok", 65);
    if (key == -1){
        printf("TrI:ERR. Could not create IPC Message Key. Please do: 'touch  /tmp/ipcbor.ftok'\n");
        return 1;
    }
    printf("TrI:ftok key: %i\n",key);
    //recvBuffer->mesg_type = 27;// 27 is arbitrary
    recvBuffer->mesg_type = 1;// ISSUE: other than 1 does not work for msgsnd
    // msgget creates a recvBuffer queue and returns identifier 
    msgid_rcv = msgget(key, 0666 | IPC_CREAT);
    msgid_snd = msgget(key+1, 0666 | IPC_CREAT);
    printf("TrI:IPC Message Output Queue id_rcv=%i, id_snd=%i \n", msgid_rcv, msgid_snd);

    //Purge any pending messages
    uint8_t *msg = NULL;
    int ii = 0;

    while (transport_recv(&msg) > 0){
        ii += 1;
        //printf("TrI: purging: %s\n", msg);
    }
    if (ii) printf("TrI: Purged: %i messages\n", ii);

    return 0;
}
int transport_recv(uint8_t **msg){
    msglen = msgrcv(msgid_rcv, recvBuffer, recvBufSize, 1, IPC_NOWAIT);
    //printf("TrI:Transport Received %i bytes: `%s`\n", msglen, recvBuffer->mesg_buf);
    if (msglen == 0){
        msgctl(msgid_rcv, IPC_RMID, NULL);
        printf("TrI:Message queue destroyed\n");
        assert(msglen != 0 && "TrI:Message queue destroyed");
    }
    *msg = (recvBuffer->mesg_buf);
    return msglen; 
}
//TODO Eliminate sendBuffer and extra copy
#define sendBuffer_size 15000
struct {
    long mesg_type = 1; // ISSUE: other than 1 does not work for msgsnd
    uint8_t mesg_buf[sendBuffer_size];
} sendBuffer;
int transport_send(uint8_t *msg, size_t msgsz){
    assert(msgsz < sendBuffer_size && "Send buffer overflow");
    memcpy(sendBuffer.mesg_buf, msg, msgsz);
    return msgsnd(msgid_snd, &sendBuffer, msgsz, 0);//, IPC_NOWAIT);
}
