/*Parameter server firmware of the emulated MCUFEC.
 */
#define ParmSimulator_VERSION "0.1.4 2025-02-09"//
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../include/defines.h"
#include "../include/pv.h"
#include "../include/transport.h"
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
#define MCU_VERSION "MCU: " MCU_FAMILY MCU_CORE ", clock:" STRINGIZE(SYSCLK) ",baudrate:" STRINGIZE(TTY_BAUDRATE)
#define ADC_Max_nChannels 8
#define ADC_Max_nSamples 1000
#define ADC_Max_value 4095


//`````````````````Global variables```````````````````````````````````````````
uint8_t DBG = 1; // Debugging verbosity level, 3 is highest. Could be changed in firmvare 
extern char* VERSION;
//`````````````````File-scope variables
static uint16_t LoopReportMS = 10000;
static uint32_t requests_received = 0;
static uint32_t requests_received_since_last_periodic = 0;
static struct timespec ptimer_start, ptimer_end;
//``````````````````Helper functions```````````````````````````````````````````
int mssleep(long miliseconds);// from defines
static bool has_periodic_interval_elapsed(){
    clock_gettime(CLOCK_REALTIME, &ptimer_end);
    int dms =   (ptimer_end.tv_sec - ptimer_start.tv_sec)*1000 +\
                (ptimer_end.tv_nsec - ptimer_start.tv_nsec)/1000000;
    if (dms < LoopReportMS)
        return false;
    ptimer_start.tv_sec = ptimer_end.tv_sec;
    ptimer_start.tv_nsec = ptimer_end.tv_nsec;
    return true;
}
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
static void update_adcs(uint32_t base){
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
int plant_init(){
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
    NPV = (sizeof(_PVs)/sizeof(PV*));
    return NPV;
}
static uint32_t trig_count;
static void periodic_update(){
    if(DBG>=1)printf("periodic_update @ %i s, host_rps=%i, run: %s\n",
        host_rps_time[0], host_rps, pv_run.value.str);
    perf[TRIG_COUNT] = trig_count;
    perf[HOST_RPS] = host_rps;
    pv_perf.timestamp.tv_sec = host_rps_time[0];
    pv_perf.timestamp.tv_nsec = host_rps_time[1];
}

/*extern struct timespec ptimer_end;
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
*/
// defined in parmain
void init_encoder(bool subscription);
void close_encoder();
void send_encoded_buffer();
extern bool plant_client_alive;

static void subscriptionDelivery(){
    init_encoder(true);
    reply_value("adc");
    close_encoder();
    send_encoded_buffer();
}
int plant_update()
// Called to update process variables
{
    if (pv_sleep.value.u2 != 0){
        mssleep(pv_sleep.value.u2);}
    if (not plant_client_alive){
        return 0;}
    
    //if (is_triggered(10)){
    if (true){
        if(starts_with(pv_run.value.str, "start")){
            trig_count++;
            if(DBG>=1)printf("Trigger %u\n",trig_count);
            update_adcs(trig_count);
            subscriptionDelivery();
        }
    }
    return 0;
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
//`````````````````Main loop``````````````````````````````````````````````````
int main(){
{
    printf("Plant version %s\n",VERSION);
    int msglen = 1;
    uint8_t *msg = NULL;
    int cycle_count = 0;
    int cycle_count_prev = 0;

    plant_init();
    if (NPV == 0){
        printf("ERR: no parameters served\n");
        return 1;
    }
    printf("Defined %i parameters\n", NPV);
    if (transport_init()) exit(1);
    clock_gettime(CLOCK_REALTIME, &ptimer_start);

    // Main loop
    while (msglen != 0){
        cycle_count++;

        if (has_periodic_interval_elapsed()){
            host_rps = (cycle_count - cycle_count_prev)*1000/LoopReportMS;
            host_rps_time[0] = ptimer_end.tv_sec;
            host_rps_time[1] = ptimer_end.tv_nsec;
            printf("ADC:rps=%i reqs:%u,%u, trig:%u client:%i\n",host_rps, requests_received, requests_received_since_last_periodic, trig_count, plant_client_alive);
            periodic_update();
            cycle_count_prev = cycle_count;
            if (requests_received == requests_received_since_last_periodic){
                if (plant_client_alive == true){
                    printf("ADC:Client have been disconnected.\n");}
                plant_client_alive = false;
            }            requests_received_since_last_periodic = requests_received;
        }

        // check if request arrived from the client
        msglen = transport_recv(&msg);
        if (msglen == -1){ // no requests
            plant_update();
            continue;
        }

        requests_received++;
        if (plant_client_alive == false){
            printf("ADC:Client is re-connected.\n");}
        plant_client_alive = true;
        plant_process_request(msg, msglen);
    }
return 0;
}}
