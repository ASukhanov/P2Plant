/*``````````````````Definition of Process Variables```````````````````````````` 
 */
#ifndef PV_H
#define PV_H
#include "defines.h"
#include <stdlib.h>
#include <time.h>

extern uint8_t DBG; //defined in parmain
extern uint16_t NPV; //defined in firmware part

#if PLATFORM == PLATFORM_STM32
    #include <cstdint>
    #include <cstdlib>
    //``````````````````Message for TTY communications`````````````````````````
    #define TTY_CR '\r'
    #define TTY_LF '\n'
    char TTY_CRLF[] = "\r\n";
    #define TTY_TXBUF_SIZE 512
    struct UARTMsg_TTY {
        struct UARTMsg_Header h;
        char msg[TTY_TXBUF_SIZE];
        };
#endif

#define MinI32 -2147483648
#define MaxI32 2147483647

union VALUE {//numpy-like coding
int8_t      b;
uint8_t     B;
int16_t     i2;
uint16_t    u2;
int32_t     i4;
uint32_t    u4;
char*       str;
uint8_t*    Bptr;
uint16_t*   u2ptr;
int16_t*    i2ptr;
int32_t*    i4ptr;
uint32_t*   u4ptr;
};

//CBOR Tag map, it should be enumerated synchronously with the VALUETYPE
struct CBORTagDType {
    uint32_t tag;
    uint8_t valueType;
    char    txt[8];
}tagTxt[] = {//https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml
    {0, T_b,    "int8"},
    {0, T_B,    "uint8"},
    {0, T_i2,   "int16"},
    {0, T_u2,   "uint16"},
    {0, T_i4,   "int32"},
    {0, T_u4,   "uint32"},
    {72,T_str,  "char*"},
    {64,T_Bptr, "uint8*"},
    {69,T_u2ptr,"uint16*"},
    {77,T_i2ptr,"int16*"},
    {78,T_i4ptr,"int32*"},
    {70,T_u4ptr,"uint32*"},
};

//``````````````````Message header for all UART communications`````````````````
struct UARTMsg_Header {
  uint16_t l; //message length in bytes
  char d;     //data description, bits[0:1] number of bytes per item minus 1, bits[2:5]: number of channels minus 1
  char id;    //data ID, '<' for JSON-formatted TTY replies, 'A' for ADC
};

enum UARTMsg_ID {
	UARTID_TTY = '<', //JSON formatted message
	UARTID_ADC = 'A', //Binary data from ADCs
};
//``````````````````Process Variables``````````````````````````````````````````
enum FEATURES {// feature bits like in ADO architecture
    F_W = 0x0001, //writable
    F_R = 0x0002, //readable
    F_D = 0x0004, //discrete
    F_A = 0x0008, //archivable
    F_C = 0x0010, //config
    F_I = 0x0020, //diagnostic
    F_s = 0x0040, //savable
    F_r = 0x0080, //restorable
    F_E = 0x0100, //editable
};
char FEATURE_LETTERS[] = "WRDACIsrE";

#define F_WE  (F_R | F_W | F_E | F_s | F_r)
#define F_WED (F_R | F_W | F_E | F_s | F_r | F_D )
#define F_WEI (F_R | F_W | F_E | F_I)
#define F_RI  (F_R | F_I)

CborEncoder* pRootEncoder;
void encode_error(CborEncoder* encoder, const char* key, const char* value){
    /* Encode one map entry to root_encoder map.
     * This is main function to report an error by sreaming it to client.
     */
    cbor_encode_text_stringz(encoder, key);
    CborEncoder amap;
    cbor_encoder_create_map(encoder, &amap, 1);
    cbor_encode_text_stringz(&amap, "ERR");
    cbor_encode_text_stringz(&amap, value);
    cbor_encoder_close_container(encoder, &amap);
}
void encode_shape(CborEncoder* encoder, uint32_t* shape){
    cbor_encode_text_stringz(encoder, "shape");
    CborEncoder array_container;
    cbor_encoder_create_array(encoder, &array_container, CborIndefiniteLength);
    for (uint ii=0; ii<MAX_DIMENSION; ii++){
        if (shape[ii] <= 0) break;
        cbor_encode_uint(&array_container, shape[ii]);
    }
    cbor_encoder_close_container(encoder, &array_container);
}
void encode_taggedBuffer(CborEncoder* encoder, uint32_t tag, 
        const void* byteString, int n){
    if(DBG>=1) printf(">tagged buffer %i %i\n", tag, n);
    cbor_encode_tag(encoder, (CborTag)tag);
    cbor_encode_byte_string(encoder, (const uint8_t*) byteString, n);
}
#if PLATFORM == PLATFORM_LINUX
void encode_timestamp(CborEncoder* encoder, TD_timestamp* ts){
    cbor_encode_text_stringz(encoder, "t");
    encode_taggedBuffer(encoder, tagTxt[T_u4ptr].tag, ts, sizeof(TD_timestamp));
}
#endif
void encode_ndarray(CborEncoder* encoder, void* arr, uint type, uint32_t* shape){
    // Encode multi dimensional array
    int n = array_length(shape);
    encode_shape(encoder, shape);
    cbor_encode_text_stringz(encoder, "v");
    switch (type){
    case T_u2ptr:
    case T_i2ptr:{ n *= 2; break;} 
    case T_u4ptr:
    case T_i4ptr:{ n *= 4; break;}
    default:{assert(false);}
    }
    encode_taggedBuffer(encoder, tagTxt[type].tag, (uint8_t*)arr, n);
}

class PV { // Parameter object
  public:
	char name[32];
	char desc[128];
	uint8_t type;   // VALUETYPE
    uint32_t shape[MAX_DIMENSION] = {1,0,0,0};   // shape of the parameter value array
    uint32_t bufsize = 0; //for writable parameter it is size of the bytestring 
	uint16_t fbits; // FEATURES
	char units[8];  // Units
	int32_t opLow;  // Lower limit of the value 
	int32_t opHigh; // High limit of the value
	char *legalValues;
	VALUE value;
    TD_timestamp timestamp = {0, 0};// seconds, nanoseconds
    bool subscribed = false;
    int (*setter)() = NULL; //Setter function

	PV(const char *aname, const char *adesc, const uint8_t atype,
			const uint16_t afbits = F_R, const char *aunits = "",
			int32_t aopLow = MinI32, int32_t aopHi = MaxI32, char *lv = NULL){
		value.u4 = 0;
        strncpy(name, aname, 16);
		strncpy(desc, adesc, 128);
		type = atype;
		fbits = afbits;
		strncpy(units, aunits, sizeof(units));
		opLow = aopLow;
		if (opLow < 0 and (type==T_u4 or type==T_u2 or type==T_B))
			opLow = 0;
		opHigh = aopHi;
		legalValues = lv;
        
        // initiate timestamp
        struct timespec tim;
        clock_gettime(CLOCK_REALTIME, &tim);
        timestamp.tv_sec = tim.tv_sec;
        timestamp.tv_nsec = tim.tv_nsec;
        //printf("t of %s: %li, %li\n",name, tim.tv_sec, tim.tv_nsec);
	};
    void set_shape(uint x, uint y=0, uint z=0, uint v=0){
        shape[0] = x; shape[1] = y; shape[2] = z; shape[3] = v;
    }
    int _call_setter(){
        int r = 0;
        if (setter != NULL){
            r = (*setter)();
        }
        return r;
    }
    int set(int vv) {
        VALUE v;
        v.i4 = vv;
        //printf("setting int %s=%i\n",name,v.i4);
		if(type == T_u4){
			if((TD_u4)opLow > v.u4 or v.u4 > (TD_u4)opHigh){
                encode_error(pRootEncoder, name, "Off limit setting");
				return 1;
            }
			value.u4 = v.u4;
			return _call_setter();
		}
		if(opLow > v.i4 or v.i4 > opHigh){
            encode_error(pRootEncoder, name, "Off limit setting");
			return 1;
        }
        //printf("type %i\n",type); 
		switch (type){
		case T_b: 	{value.b = v.b; break;}
		case T_B:	{value.B = v.B; break;}
		case T_i2:	{value.i2 = v.i2; break;}
		case T_u2:	{value.u2 = v.u2; break;}
		case T_i4:	{value.i4 = v.i4; break;}
		}
		return _call_setter();
	}
    int set(const char* str){
        if(DBG>=1)printf(">set_str %s[%li] %s\n", name, strlen(str), str);
        assert(type == T_str);
        if(legalValues != NULL){
            if(strstr(legalValues, str) == NULL) {
                encode_error(pRootEncoder, name, "Illegal value");
                return 0;
            }}
        if (value.str != NULL){
            free(value.str);}
        uint n = strlen(str)+1;
        value.str = (char *) malloc(n);
        strncpy(value.str, str, n);
        return _call_setter();
	}
    int set_ptr(void* pvalue){
        if(DBG>=1)printf("setting ptr* %s, shape (%i,%i,%i,%i)\n", name, shape[0], shape[1], shape[2], shape[3]);
        value.Bptr = (TD_Bptr)pvalue;
        return _call_setter();
    }
    //TODO: the body is the same for different set()
	int set(int8_t* pvalue){
        return set_ptr(pvalue);
    }
	int set(uint8_t* pvalue){
        return set_ptr(pvalue);
    }
	int set(int16_t* pvalue){
        return set_ptr(pvalue);
    }
	int set(uint16_t* pvalue){
        return set_ptr(pvalue);
    }
	int set(int32_t* pvalue){
        return set_ptr(pvalue);
    }
	int set(uint32_t* pvalue){
        return set_ptr(pvalue);
    }
    int set_tagged(CborTag tag, const void* buf, uint nbytes){
        if(DBG>=1){
            printf(">parm_set_tagged %s, %li, %i:\n", name, tag, nbytes);
            dumpbytes((const uint8_t*)buf, nbytes);  printf("\n");
        }
        uint itype = 0;
        for (; tagTxt[itype].tag != tag; itype++);
        assert(itype == tagTxt[itype].valueType);// just checking
        switch (itype){
        case T_Bptr:
        case T_u2ptr:
        case T_i2ptr:
        case T_i4ptr:
        case T_u4ptr:
        {
            if(nbytes > bufsize){
                encode_error(pRootEncoder, name, "Value size is too large");
                return 0;}
            memcpy(value.Bptr, buf, nbytes);
            break;
        }
        default:
            assert(false);
        }
        return 0;
    }
    CborError val2cbor(CborEncoder *pencoder){
        if(DBG>=1)printf(">val2cbor\n");
        CborEncoder map_values;
        CborError r;
        r = cbor_encode_text_stringz(pencoder, name);
        assert(r==CborNoError);
        cbor_encoder_create_map(pencoder, &map_values, CborIndefiniteLength);
        switch (type){
        case T_b:   {
            cbor_encode_text_stringz(&map_values, "v");
            cbor_encode_int(&map_values, value.b);
            break;
        }case T_B:  {
            cbor_encode_text_stringz(&map_values, "v");
            cbor_encode_uint(&map_values, value.B);
            break;
        }case T_u2: {
            cbor_encode_text_stringz(&map_values, "v");
            cbor_encode_uint(&map_values, value.u2);
            break;
        }case T_i2: {
            if (array_length(shape) == 1){
                cbor_encode_text_stringz(&map_values, "v");
                r = cbor_encode_int(&map_values, value.i2);
            }else{
                if (value.i4 == 0){
                    cbor_encoder_close_container(pencoder, &map_values);
                    encode_error(pencoder, name, "PV_i2 was not initialized");
                    return CborNoError;
                }
            }
            break;
        }case T_u4: {
            cbor_encode_text_stringz(&map_values, "v");
            cbor_encode_uint(&map_values, value.u4);
            break;
        }case T_i4: {
            cbor_encode_text_stringz(&map_values, "v");
            cbor_encode_int(&map_values, value.i4);
            break;
        }case T_str: {
            if (value.i4 == 0){
                cbor_encoder_close_container(pencoder, &map_values);
                encode_error(pencoder, name, "PV_str was not initialized");
                return CborNoError;
            }
            cbor_encode_text_stringz(&map_values, "v");
            r = cbor_encode_text_stringz(&map_values, value.str);
            assert(r==CborNoError);
            break;
        }case T_i2ptr:
        case T_u2ptr:{
            if (value.i4 == 0){
                cbor_encoder_close_container(pencoder, &map_values);
                encode_error(pencoder, name, "PV_i2ptr was not initialized");
                return CborNoError;
            }            
            //printf("encode_uint16Array %s\n", name);
            encode_ndarray(&map_values, value.u2ptr, type, shape);
            break;
        }case T_i4ptr:
        case T_u4ptr:{
            if (value.i4 == 0){
                cbor_encoder_close_container(pencoder, &map_values);
                encode_error(pencoder, name, "PV_i4ptr was not initialized");
                return CborNoError;
            }
            encode_ndarray(&map_values, value.u4ptr, type, shape);
            break;
        }default: {
            //printf("ERR in val2cbor %s\n", name);
            cbor_encoder_close_container(pencoder, &map_values);
            encode_error(pencoder, name, "Not supported type of parameter");
            return CborNoError;
        }
        }
        encode_timestamp(&map_values, &timestamp);
        cbor_encoder_close_container(pencoder, &map_values);
        return CborNoError;
    }
    CborError info2cbor(CborEncoder *pencoder){
        CborEncoder map_values;
        CborError r;
        char fbitString[sizeof(FEATURE_LETTERS)+1];
        char *fbitsPtr = fbitString;
        r = cbor_encode_text_stringz(pencoder, name);
        //printf("info2cbor r=%i, %i\n", (int)r, (int) CborNoError);
        assert(r==CborNoError);
        cbor_encoder_create_map(pencoder, &map_values, CborIndefiniteLength);
        cbor_encode_text_stringz(&map_values, "desc");
        cbor_encode_text_stringz(&map_values, desc);
        cbor_encode_text_stringz(&map_values, "type");
        //cbor_encode_uint(&map_values, type);
        cbor_encode_text_stringz(&map_values, tagTxt[type].txt);

        encode_shape(&map_values, shape);
        
        cbor_encode_text_stringz(&map_values, "fbits");
        //cbor_encode_uint(&map_values, fbits);
        for(int i = 0, b = 1; i < (int)sizeof(FEATURE_LETTERS); i++){
            if(fbits & b){
                *fbitsPtr++ = FEATURE_LETTERS[i];}
            b <<= 1;
        }
        *fbitsPtr = 0;
        cbor_encode_text_stringz(&map_values, fbitString);

        if(strlen(units) > 0){
            cbor_encode_text_stringz(&map_values, "units");
            cbor_encode_text_stringz(&map_values, units);
        }
        if(opLow != MinI32){
            cbor_encode_text_stringz(&map_values, "opLow");
            cbor_encode_int(&map_values, opLow);
        }
        if(opHigh != MaxI32){
            cbor_encode_text_stringz(&map_values, "opHigh");
            cbor_encode_int(&map_values, opHigh);
        }
        if(legalValues != NULL){
            cbor_encode_text_stringz(&map_values, "legalValues");
            cbor_encode_text_stringz(&map_values, legalValues);
        }
        cbor_encoder_close_container(pencoder, &map_values);
        return CborNoError;
    }
};
//``````````````````Parameter handling`````````````````````````````````````````
static PV** PVs;
static PV* pvof(const char* pvname){
	PV* pv = NULL;
    //printf(">pvof %i\n",NPV);
	if (pvname == NULL){
        encode_error(pRootEncoder, "?", "PV is not provided");
		return NULL;
	}
	for(int i=0; i < NPV; i++){
		if (strcmp(PVs[i]->name, pvname)==0){
			pv = PVs[i];}
	}
	if (pv == NULL){
        encode_error(pRootEncoder, pvname, "Wrong PV name");
		return NULL;
	}
	return pv;
}
static int reply_value(const char* pvname){
    if(DBG>=1)printf(">reply_value %s\n", pvname);
	PV* pv = pvof(pvname);
	if (pv == NULL){
        return CborNoError;
	}
    return (int) (pv->val2cbor(pRootEncoder));
}
static int reply_info(const char* pvname){
    if (strcmp(pvname, "*") == 0){
        CborEncoder alist;
        if(DBG>=2)printf(">create_map %i\n",NPV);
        cbor_encode_text_stringz(pRootEncoder, "*");
        cbor_encoder_create_map(pRootEncoder, &alist, NPV);
        for (int i=0; i < NPV; i++){
            if(DBG>=2)printf("encode %s\n",PVs[i]->name);
            cbor_encode_text_stringz(&alist, PVs[i]->name);
            cbor_encode_text_stringz(&alist, PVs[i]->desc);
        }
        cbor_encoder_close_container(pRootEncoder, &alist);
        return 0;
    }
    PV* pv = pvof(pvname);
	if (pv == NULL){
        return 0;
	}
    if(DBG>=2)printf(">reply info %s\n", pvname);
    return (int) (pv->info2cbor(pRootEncoder));
	return 0;
}
int parm_init_reply(CborEncoder* pencoder){
    pRootEncoder = pencoder;
    return 0;
}    
int parm_info(const char* parmName){
    if(DBG>=2)printf(">parm_info %s\n", parmName);
    return reply_info(parmName);
}
int parm_get(const char* parmName){
    if(DBG>=2)printf(">parm_get %s\n", parmName);
    return reply_value(parmName);
}
int parm_set(const char* parmName, CborType type, 
  const void* pvalue, uint count){
    if(DBG>=1) printf("set %s, type %i\n", parmName, type);
    PV* pv = pvof(parmName);
	if (pv == NULL){
        return 0;
	}    
    switch (type){
    case CborTextStringType:{
        if(DBG>=1)printf(">parm_set_text(%s,%s)\n", parmName, (const char*) pvalue);
        return pv->set(((const char*)pvalue));
        break;
    }case CborIntegerType:{
        if(DBG>=1)printf(">parm_set_int(%s,%i)\n", parmName, *(int*)(pvalue));
        return pv->set(((int*)(pvalue))[0]);
    }case CborArrayType:{
        if(DBG>=1)printf(">parm_set_Array(%s[%i],[%i,...,%i)\n", parmName, count,
          ((int*)(pvalue))[0], ((int*)pvalue)[count-1]);
        if (pv->type == T_i4ptr){
            return pv->set((int32_t*)pvalue);
        }
        if (pv->type == T_i2ptr){ 
            return pv->set((int16_t*)pvalue);//TODO: convert int[] to int16_t[]
        }
        break;
    }
    default:
        // Should never get here
        printf("ERR: Not supported type %i of %s value\n", type, parmName); 
        encode_error(pRootEncoder, parmName, "ERR: Not supported type of value");
        break;
    }
    return 0; // If not 0 then assert will be raised and program aborted
}
int parm_set_tagged(const char* parmName, CborTag tag, const void* buf,
                    uint count){
    PV* pv = pvof(parmName);
	if (pv == NULL){
        return 0;
	}    
    return pv->set_tagged(tag, buf, count);
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
#endif //PV_H
