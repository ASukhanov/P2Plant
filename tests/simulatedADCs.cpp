/*Parameter server firmware of the emulated MCUFEC.
 */
#define ParmSimulator_VERSION "0.1.3 2024-04-01"// host_rps removed, icluded in perf.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../include/defines.h"
#include "../include/pv.h"

//``````````````````Helper functions```````````````````````````````````````````
int mssleep(long miliseconds)
{
   struct timespec rem;
   struct timespec req= {
       (int)(miliseconds / 1000),     // secs (Must be Non-Negative)
       (miliseconds % 1000) * 1000000 // nano (Must be in range of 0 to 999999999)
   };
   //printf("s,ns = %li,%li\n", req.tv_sec, req.tv_nsec);
   return nanosleep(&req , &rem);
   return 0;
}
//``````````````````Definitions````````````````````````````````````````````````
#define mega 1000000
#define TTY_BAUDRATE 7372800
#define SYSCLK 170000000 //system clock, Hz
#define STRINGIZE_NX(A) #A
#define STRINGIZE(A) STRINGIZE_NX(A)
#define MCU_FAMILY "STM32G"
#define MCU_STM32 'G'
#define MCU_CORE "431"
//#define MCU_VERSION "{\"MCU\":\"" MCU_FAMILY MCU_CORE"\", \"soft\":\"" VERSION"\", \"clock\":" STRINGIZE(SYSCLK) ", \"baudrate\":" STRINGIZE(TTY_BAUDRATE) "}"
#define MCU_VERSION "MCU: " MCU_FAMILY MCU_CORE ", soft:" VERSION ", clock:" STRINGIZE(SYSCLK) ",baudrate:" STRINGIZE(TTY_BAUDRATE)
#define ADC_Max_nChannels 8
#define ADC_Max_nSamples 1000
#define ADC_Max_value 4095

//``````````````````Memory for array parameters````````````````````````````````
static int16_t adc_offsets[] = {1, 2, 3, 32000, -32000, 16, 17, 18};
static uint32_t perf[] = {0, 0}; 
enum PERFITEM {
    TRIG_COUNT,
    HOST_RPS,
};
//``````````````````Definitions of PVs`````````````````````````````````````````
// They are initialized in the main()
static PV pv_version = {"version",
    "MCU and software version, system clock", T_str, F_R};
//PV pv_clock =   {"clock", "System clock", 	T_u4, 1, F_R, "Hz"};
static PV pv_debug = {"debug",
    "Show debugging messages", 	T_B, F_WEI};
static PV pv_run = 	{"run",
    "For experts. Start/Stop board peripherals except ADCs", T_str, F_WED};
static PV pv_sleep = 	{"sleep",
    "Sleep in the program loop", T_u4, F_R, "ms"};
static PV pv_perf = {"perf",
    "Performance counters. TrigCount, RPS in main loop", T_u4ptr, F_RI};

//``````````````````ADCs```````````````````````````````````````````````````````
static PV pv_adc_offsets = {"adc_offsets",// not implemented in MCUFEC
    "Offsets of all ADC channels", T_i2ptr, F_WE, "counts"};
static PV pv_adc_reclen = {"adc_reclen",
    "Record length. Number of samples of each ADC", T_u2, F_WE};
static PV pv_adc_srate = {"adc_srate",
    "Sampling rate of ADCs", T_u4, F_WE, "Hz"};
static PV pv_adc = {"adc",
    "Two-dimentional array[adc#][samples] of ADC samples", T_u2ptr, F_R, "counts"};
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
static PV* _PVs[] = {
  &pv_version,
  &pv_debug,
  &pv_run,
  &pv_sleep,
  &pv_perf,
  &pv_adc_offsets,
  &pv_adc_reclen,
  &pv_adc_srate,
  &pv_adc,
};
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
static uint16_t adc_samples[ADC_Max_nChannels*ADC_Max_nSamples];
void update_adcs(uint32_t base){
    int nsamples = pv_adc_reclen.value.u2;
    for (uint32_t iadc=0; iadc<pv_adc.shape[0]; iadc++){
        for (uint32_t ii=0; ii<pv_adc.shape[1]; ii++){
            adc_samples[iadc*nsamples + ii] = (base+iadc+ii) % pv_adc.shape[1];
        }
    }
}
//``````````````````Setters````````````````````````````````````````````````````
int pv_debug_setter(){
    DBG = pv_debug.value.u2;
    printf(">pv_debug_setter %i \n", DBG);
    return 0;
}
//``````````````````Command handlers```````````````````````````````````````````
extern uint32_t host_rps;// defined in parmfirm
extern uint32_t host_rps_time[2];
extern PV** PVs;
int parm_init(){
    // Initialize parameters, return number of parameters served
    printf("ParmSimulator %s\n", ParmSimulator_VERSION);
    pv_version.set(MCU_VERSION);
    printf("pv_version: %s\n", pv_version.value.str);

    //``````````````Initialization of parameters
    pv_run.set("stop");
    pv_run.legalValues = (char*)"start,stop";
    pv_sleep.opLow = 0;
    pv_sleep.opHigh = 10000;
    pv_sleep.set(1);
    pv_perf.set(perf);
    pv_perf.set_shape(sizeof(perf)/sizeof(perf)[0]);

    pv_debug.setter = pv_debug_setter;

    int nch = ADC_Max_nChannels;
    pv_adc_offsets.set_shape(nch);
    pv_adc_offsets.set(adc_offsets);
    pv_adc_offsets.bufsize = sizeof(adc_offsets);

    // initialize ADCs
    pv_adc_reclen.set(80);
    pv_adc_srate.set(100000);
    int nsamples = pv_adc_reclen.value.u2;
    pv_adc.set_shape(nch, nsamples);
    update_adcs(0);
    if(DBG>=2){ 
        printf("ADC:\n");
        int16_t* i2idx = (int16_t*)(adc_samples);
        for (uint32_t ii = 0; ii<(pv_adc.shape[0]*pv_adc.shape[1]); ii++){
            printf("%3i,",*i2idx++);
        }
        printf("\n");
    }
    pv_adc.set(adc_samples);
    PVs = _PVs;
    return (sizeof(_PVs)/sizeof(PV*));
}
extern uint32_t trig_count;
void parm_periodic_update(){
    if(DBG>=1)printf("parm_periodic_update @ %i s, host_rps=%i\n",host_rps_time[0],host_rps);
    perf[TRIG_COUNT] = trig_count;
    perf[HOST_RPS] = host_rps;
    pv_perf.timestamp.tv_sec = host_rps_time[0];
    pv_perf.timestamp.tv_nsec = host_rps_time[1];
    if(DBG>=1)printf("run: %s\n",pv_run.value.str);
}

extern struct timespec ptimer_end;
static struct timespec last = {0,0};
bool is_triggered(uint interval_msec){
    bool r = false;
    //printf("t:%li,%li %li,%li, %i\n", ptimer_end.tv_sec, ptimer_end.tv_nsec,
    //    last.tv_sec, last.tv_nsec, trig_count);
    if (ptimer_end.tv_sec > last.tv_sec + interval_msec/1000){
        r = true;
    }else{
        if (ptimer_end.tv_nsec > last.tv_nsec + interval_msec*1000000){
            r = true;}
    }
    if (r == true){
        last.tv_sec = ptimer_end.tv_sec;
        last.tv_nsec = ptimer_end.tv_nsec;
        //printf("True\n");
    }
    return r;
}
// defined in parmain
void init_encoder(bool subscription);
void close_encoder();
void send_encoded_buffer();
extern bool client_alive;

void subscriptionDelivery(){
    init_encoder(true);
    reply_value("adc");
    close_encoder();
    send_encoded_buffer();
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
int parm_processing()
{
    if (pv_sleep.value.u2 != 0){
        mssleep(pv_sleep.value.u2);}//the function call alone takes 50 us
    if (not client_alive){
        return 0;}
    if (is_triggered(10)){
        if(starts_with(pv_run.value.str, "start")){
            trig_count++;
            if(DBG>=1)printf("Trigger %u\n",trig_count);
            update_adcs(trig_count);
            subscriptionDelivery();
        }
    }
    return 0;
}

