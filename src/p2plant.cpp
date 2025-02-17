/*````````````````Base functions of a P2Plant.
It supposed to run on a bare metal firmware (STM32 MCU, CommonPlatform hardware).
Supported commands: info, get, set.
The 'subscribe' command is considered unnecessary. The 'run start/stop' should
handle the subscription activation.
*/
const char* VERSION = "0.5.0 2025-02-14";//removed IPC_NOWAIT from msgsnd
#include <stdio.h>
#include <stdlib.h>// for free()

#include "../include/defines.h"
#include "../../tinycbor/src/cborjson.h"
#include "../include/transport.h"

//``````````````````Globals````````````````````````````````````````````````````
extern uint8_t DBG; // Defined in main program
uint16_t NPV = 0;// Number of parameters
bool plant_client_alive = true;// if not, then subscription will be suspended
static uint32_t transport_send_failure = 0;
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
// cbor_parser storage 
static uint8_t encoder_buf[1500];
//``````````````````Command handlers```````````````````````````````````````````

enum PARM_COMMAND {// like in ADO architecture
    PARM_CMD_INFO = 0,
    PARM_CMD_GET = 1,
    PARM_CMD_SET = 2,
    PARM_CMD_SUBSCRIBE = 3,
};

static int parm_dispatch(int cmd, const char* parmName, CborValue* value=NULL){
    int ret = 1;
    switch (cmd) {
    case PARM_CMD_INFO: {
        ret = parm_info(parmName);
        break;
        }
    case PARM_CMD_GET: {
        ret = parm_get(parmName);
        break;
        }
    /*case PARM_CMD_SUBSCRIBE: {
        ret = plant_subscribe(parmName);
        }
        break;*/
    }
    return ret;
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
//`````````````````````````````````````````````````````````````````````````````
//                  CBOR functions
#define CBOR_CHECK(a, str, goto_tag, ret_value, ...)                              \
    do                                                                            \
    {                                                                             \
        if ((a) != CborNoError)                                                   \
        {                                                                         \
            printf("P2P:ERR:%s ",str);                                                \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

static void indent(int nestingLevel)
{
    while (nestingLevel--) {
        if(DBG>=2) printf("  ");
    }
}
void dumpbytes(const uint8_t *buf, size_t len)
{
    while (len--) {
        printf("%02X ", *buf++);
    }
}
//Decode CBOR data manually
static CborParser root_parser;
static CborEncoder root_encoder;
static CborEncoder branch_encoder;
static CborError parse_cbor_buffer(CborValue *it, int nestingLevel)
{
    static int parm_cmd = 0;
    static CborTag parm_tag = 0;
    static char parName[80];
    CborError ret = CborNoError;
    int hosterror = 0;
    int item = 0;
    while (!cbor_value_at_end(it)) {
        CborType type = cbor_value_get_type(it);
        indent(nestingLevel);
        switch (type) {
        case CborArrayType: {
            CborValue recursed;
            assert(cbor_value_is_container(it));
            item = 0;
            if(DBG>=2) puts("Array[");
            if(DBG>=3) printf("array nesting %i\n",nestingLevel);
            ret = cbor_value_enter_container(it, &recursed);
            CBOR_CHECK(ret, "enter container failed", err, ret);
            ret = parse_cbor_buffer(&recursed, nestingLevel + 1);
            CBOR_CHECK(ret, "recursive dump failed", err, ret);
            ret = cbor_value_leave_container(it, &recursed);
            CBOR_CHECK(ret, "leave container failed", err, ret);
            indent(nestingLevel);
            if(DBG>=2) puts("]");
            if(DBG>=3) printf("<nesting %i\n",nestingLevel);
            continue;
        }
        case CborMapType: {
            CborValue recursed;
            assert(cbor_value_is_container(it));
            if(DBG>=2) puts("Map{");
            if(DBG>=3) printf("map nesting %i\n",nestingLevel);
            ret = cbor_value_enter_container(it, &recursed);
            CBOR_CHECK(ret, "enter container failed", err, ret);
            ret = parse_cbor_buffer(&recursed, nestingLevel + 1);
            CBOR_CHECK(ret, "recursive dump failed", err, ret);
            ret = cbor_value_leave_container(it, &recursed);
            CBOR_CHECK(ret, "leave container failed", err, ret);
            indent(nestingLevel);
            if(DBG>=2) puts("}");
            continue;
        }
        case CborIntegerType: {
            int64_t val;
            ret = cbor_value_get_int64(it, &val);
            CBOR_CHECK(ret, "parse int64 failed", err, ret);
            item++;
            if(DBG>=2) printf("%lld\n", (long long)val);
            if(DBG>=3) printf("nesting %i, item %i\n",nestingLevel, item);
            if (parm_cmd == PARM_CMD_SET){
                if (nestingLevel == 3){
                    hosterror = parm_set(parName, type, &val, 1);
                    assert(!hosterror);
                }            }
            break;
        }
        case CborByteStringType: {
            uint8_t *buf;
            size_t n;
            ret = cbor_value_dup_byte_string(it, &buf, &n, it);
            CBOR_CHECK(ret, "parse byte string failed", err, ret);
            item++;
            if(DBG>=3){
                printf("Bytes[%li]\n",n);
                dumpbytes(buf, n);
                puts("");
            }
            if(nestingLevel == 3 && parm_tag != 0){
                parm_set_tagged(parName, parm_tag, buf, n);}
            free(buf);
            continue;
        }
        case CborTextStringType: {
            char *buf;
            size_t n;
            ret = cbor_value_dup_text_string(it, &buf, &n, it);
            CBOR_CHECK(ret, "parse text string failed", err, ret);
            item++;
            //if(DBG>=2) puts(buf);
            if(DBG>=3) printf("text level %i, item %i: `%s`\n",nestingLevel,item,buf);
            if(nestingLevel == 1){
                if (starts_with(buf, "info")){
                    parm_cmd = PARM_CMD_INFO;
                }else if (starts_with(buf, "get")){
                    parm_cmd = PARM_CMD_GET;
                }else if (starts_with(buf, "set")){
                    parm_cmd = PARM_CMD_SET;
                /*}else if (starts_with(buf, "subscribe")){
                    parm_cmd = PARM_CMD_SUBSCRIBE;*/
                }else{
                    //CBOR_CHECK(1, "Wrong command", err, ret);
                    printf("P2P:ERR: Wrong command `%s`\n",buf);
                    cbor_encode_text_stringz(&branch_encoder, "ERR: Wrong command");
                    return CborUnknownError;
                }
                if(DBG>=3) printf("Command started: %s, enum:%i\n", buf, parm_cmd);
            }else if (nestingLevel == 2){
                ret = (CborError) (parm_dispatch(parm_cmd, buf));
                CBOR_CHECK(ret, "dispatch failed\n", err, ret);
            }else if (parm_cmd == PARM_CMD_SET && nestingLevel == 3){
                //Save parName for set command
                if (item == 1){
                    strncpy(parName, buf, sizeof(parName));
                }else if(item == 2){
                    hosterror = parm_set(parName, type, buf, 1);
                    assert(!hosterror);
                }
            }
            free(buf);
            continue;
        }
        case CborTagType: {
            CborTag tag;
            ret = cbor_value_get_tag(it, &tag);
            CBOR_CHECK(ret, "parse tag failed", err, ret);
            if(DBG>=2) printf("Tag(%lld)\n", (long long)tag);
            if(nestingLevel == 3){
                parm_tag = tag;}
            break;
        }
        case CborSimpleType: {
            uint8_t type;
            ret = cbor_value_get_simple_type(it, &type);
            CBOR_CHECK(ret, "parse simple type failed", err, ret);
            if(DBG>=2) printf("simple(%u)\n", type);
            break;
        }
        case CborNullType:
            if(DBG>=2) puts("null");
            break;
        /*
        case CborUndefinedType:
            if(DBG>=2) puts("undefined");
            break;
        case CborBooleanType: {
            bool val;
            ret = cbor_value_get_boolean(it, &val);
            CBOR_CHECK(ret, "parse boolean type failed", err, ret);
            if(DBG>=2) puts(val ? "true" : "false");
            break;
        }
        case CborHalfFloatType: {
            uint16_t val;
            ret = cbor_value_get_half_float(it, &val);
            CBOR_CHECK(ret, "parse half float type failed", err, ret);
            item++;
            if(DBG>=2) printf("__f16(%04x)\n", val);
            break;
        }
        case CborFloatType: {
            float val;
            ret = cbor_value_get_float(it, &val);
            CBOR_CHECK(ret, "parse float type failed", err, ret);
            item++;
            if(DBG>=2) printf("%g\n", val);
            break;
        }
        case CborDoubleType: {
            double val;
            ret = cbor_value_get_double(it, &val);
            if(DBG>=1) printf("Double %g\n",val); 
            CBOR_CHECK(ret, "parse double float type failed", err, ret);
            item++;
            if(DBG>=2) printf("%g\n", val);
            break;
        }
        case CborInvalidType: {
            ret = CborErrorUnknownType;
            CBOR_CHECK(ret, "unknown cbor type", err, ret);
            break;
        }
        */
        default: {
            encode_error(&branch_encoder, parName, "in parse_cbor_buffer: Not supported type");
            break;
        }
        }
        ret = cbor_value_advance_fixed(it);
        CBOR_CHECK(ret, "fix value failed", err, ret);
    }
    return CborNoError;
err:
    return ret;
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
//``````````````````Main loop helper functions`````````````````````````````````
void init_encoder(bool subscription=false){
    cbor_encoder_init(&root_encoder, encoder_buf, sizeof(encoder_buf), 0);
    cbor_encoder_create_array(&root_encoder, &branch_encoder, CborIndefiniteLength);
    if(subscription){
        cbor_encode_text_stringz(&branch_encoder, "Subscription");}
}
void close_encoder(){
    cbor_encoder_close_container(&root_encoder, &branch_encoder);
}
void send_encoded_buffer(){
    CborValue it;
    int buflen = cbor_encoder_get_buffer_size(&root_encoder, encoder_buf);
    if(DBG>=2) printf("P2P:encoded buffer size %i:\n", buflen);
    if (buflen == 0)
        return;
    if(DBG>=2){
        for (int i=0; i<buflen; i++){
            printf("%i,",encoder_buf[i]);}
    }
    //printf("P2P >send\n");
    // Program will be blocked if client exits. T
    int r = transport_send(encoder_buf, buflen);
    if(DBG>=2) printf("P2P <sent\n");
    if (r == 0){
        transport_send_failure = 0;
        if(DBG>=2){
            printf("P2P:Replied: ");
            cbor_parser_init((const uint8_t*) encoder_buf, buflen, 0, &root_parser, &it);
            cbor_value_to_json(stdout, &it, 0);
            printf("\n");
        }
    }else{
        transport_send_failure++;
        printf("WARNING_P2P:transport_send_failure # %i\n", transport_send_failure);
        if (plant_client_alive && transport_send_failure > 100){
            printf("ERROR_P2P:Client have been disconnected due to transport_send_failure.\n");
            plant_client_alive = false;
        }
    }
}
void plant_process_request(const uint8_t* msg, int msglen){
    CborValue it;

    if(DBG>=2){
        printf("\nP2P:Received %i bytes:\n", msglen);
        dumpbytes(msg, msglen);
        printf("\n");
    }
    // Parse incoming message
    cbor_parser_init(msg, msglen, 0, &root_parser, &it);
    if(DBG>=1){
        printf("P2P:Request received: ");
        // Dump the values in JSON format
        cbor_value_to_json(stdout, &it, 0);
        puts("\n");}

    // Initialize encoder to build the reply and open main map
    init_encoder();
    parm_init_reply(&branch_encoder);

    // Decode CBOR data, fill the reply and close the main map
    if(DBG>=2) puts("````````````````````Parsing:");
    parse_cbor_buffer(&it, 0);
    close_encoder();
    if(DBG>=2) puts(",,,,,,,,,,,,,,,,,,,,Parsing finished");
    send_encoded_buffer();
}

