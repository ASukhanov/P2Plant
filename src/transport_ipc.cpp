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
#include "../include/transport.h"

//``````````````````Transport variables````````````````````````````````````````
static MESG_BUFFER rcv_buffer; 
int msgid_rcv, msgid_snd;
int msglen = 1;

//``````````````````Transport functions````````````````````````````````````````
int transport_init(void){
    key_t key; 
    // ftok to generate unique key
    key = ftok("/tmp/ipcbor.ftok", 65);
    if (key == -1){
        printf("TrI:ERR. Could not create IPC Message Key. Please do: 'touch  /tmp/ipcbor.ftok'\n");
        return 1;
    }
    printf("TrI:ftok key: %i\n",key);
    //rcv_buffer.mesg_type = 27;// 27 is arbitrary
    rcv_buffer.mesg_type = 1;// ISSUE: other than 1 does not work for msgsnd
    // msgget creates a rcv_buffer queue and returns identifier 
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
    msglen = msgrcv(msgid_rcv, &rcv_buffer, sizeof(rcv_buffer), 1, IPC_NOWAIT);
    //printf("TrI:Transport Received %i bytes: `%s`\n", msglen, rcv_buffer.mesg_text);
    if (msglen == 0){
        msgctl(msgid_rcv, IPC_RMID, NULL);
        printf("TrI:Message queue destroyed\n");
    }
    *msg = (rcv_buffer.mesg_text);
    return msglen; 
}
int transport_send(uint8_t *msg, size_t msgsz){
    memcpy(rcv_buffer.mesg_text, msg, msgsz);
    return msgsnd(msgid_snd, &rcv_buffer, msgsz, 0);//, IPC_NOWAIT);
}
