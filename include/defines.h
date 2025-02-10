/*``````````````````Commomn definitions for parmhost functions`````````````````
*/
#ifndef DEFINES_H
#define DEFINES_H
#define VERSION "0.3.0 2025-02-09"
#include "../../tinycbor/src/cbor.h"

#define PLATFORM_LINUX 1
#define PLATFORM_STM32 2
#define PLATFORM PLATFORM_LINUX

#define MAX_DIMENSION 4// max dimension of multi-dimensional arrays

//``````````````````Type definitions```````````````````````````````````````````
typedef	int8_t   TD_b;
typedef	uint8_t	 TD_B;
typedef uint16_t TD_u2;
typedef	int16_t	 TD_i2;
typedef	int32_t	 TD_i4;
typedef	uint32_t TD_u4;
typedef char* TD_str;
typedef uint8_t* TD_Bptr;
typedef uint16_t* TD_u2ptr;
typedef int16_t* TD_i2ptr;
typedef uint32_t* TD_u4ptr;
typedef int32_t* TD_i4ptr;

typedef struct TimeStamp { uint32_t tv_sec; uint32_t tv_nsec;} TD_timestamp;

enum VALUETYPE {
    T_b = 0,
    T_B = 1,
    T_i2 = 2,
    T_u2 = 3,
    T_i4 = 4,
    T_u4 = 5,
	T_str = 6,
    T_Bptr = 7,
    T_u2ptr = 8,
    T_i2ptr = 9,
    T_i4ptr = 10,
    T_u4ptr = 11,
};

//``````````````````Helper functions```````````````````````````````````````````
bool starts_with(const char *str1, const char *str2);
int array_length(uint32_t* shape);
void dumpbytes(const uint8_t *buf, size_t len);
void encode_error(CborEncoder* encoder, const char* key, const char* value);

//``````````````````Firmware-specific functions````````````````````````````````
//  entries for main loop
int plant_init();
int plant_processing();
void plant_periodic_update();

//  Plant's internal functions
int parm_init_reply(CborEncoder* encoder);
int parm_info(const char* parmName);
int parm_get(const char* parmName);
int parm_set(const char* parmName, CborType type, const void* pvalue,
                    unsigned int count);
int parm_set_tagged(const char* parmName, CborTag tag, const void* pvalue,
                    unsigned int count);
//int parm_subscribe(const char* parmName);

#endif //DEFINES_H
