#include <stdlib.h>
#include "../../tinycbor/src/cbor.h"
#include "../include/defines.h"
#include <time.h>// for nanosleep

//``````````````````Helper functions```````````````````````````````````````````
#include <ctype.h> // needed for tolower
int my_index( const int a[], size_t size, int value )
{
    size_t index = 0;
    while ( index < size && a[index] != value ) ++index;
    return ( index == size ? -1 : index );
}
bool starts_with(const char *str1, const char *str2){
	return strncmp(str1, str2, strlen(str2)) == 0;
}
bool ends_with(const char *str1, const char *str2){
	int l2 = strlen(str2);
	int l1 = strlen(str1);
	return strncmp(&(str1[l1-l2]), str2, l2) == 0;
}
char* lower(char* dst, char* src){
	for (int i=0; src[i]; i++){
		dst[i] = tolower(src[i]);
	}
	return dst;
}
int array_length(uint32_t* shape){
    // Length of an multi-dimensional array with given shape. 
    // The shape sequence should finish with 0
    int l = shape[0];
    for (uint ii=1; ii<MAX_DIMENSION; ii++){
        if (shape[ii] <= 0) break;
        l *= shape[ii];
    }
    return l;
}
//``````````````````Helper functions```````````````````````````````````````````
int mssleep(long miliseconds){
    // Millisecond sleep. The function call alone takes 50 us
    struct timespec rem;
    struct timespec req= {
       (int)(miliseconds / 1000),     // secs (Must be Non-Negative)
       (miliseconds % 1000) * 1000000 // nano (Must be in range of 0 to 999999999)
    };
    //printf("s,ns = %li,%li\n", req.tv_sec, req.tv_nsec);
    return nanosleep(&req , &rem);
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
