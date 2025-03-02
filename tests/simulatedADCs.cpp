/*P2Plqnt implementation of simulated 8-channel ADC.
 */
#define _VERSION "0.4.2 2025-02-23"// re-factored
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../include/defines.h"
#include "../include/pv.h"
#include "../include/transport.h"
//``````````````````Definitions```````````````````````````````````````````````
#define mega 1000000
#define ADC_Max_nChannels 8
#define ADC_Max_nSamples 1000
#define ADC_Max_value 4095


//`````````````````Global variables```````````````````````````````````````````
uint8_t DBG = 0; // Debugging verbosity level, 3 is highest.
extern char* VERSION;

//`````````````````File-scope variables
static uint16_t LoopReportMS = 10000;
static uint32_t requests_received = 0;
static uint32_t requests_received_since_last_periodic = 0;
static struct timespec ptimer_last_update, ptimer_now;

//``````````````````Helper functions``````````````````````````````````````````
int mssleep(long miliseconds);// defined in helpers.
static bool has_periodic_interval_elapsed(){
    int dms =   (ptimer_now.tv_sec - ptimer_last_update.tv_sec)*1000 +\
                (ptimer_now.tv_nsec - ptimer_last_update.tv_nsec)/1000000;
    if (dms < LoopReportMS)
        return false;
    ptimer_last_update.tv_sec = ptimer_now.tv_sec;
    ptimer_last_update.tv_nsec = ptimer_now.tv_nsec;
    return true;
}
//``````````````````Memory for array parameters```````````````````````````````
static int16_t adc_offsets[] = {1, 2, 3, 32000, -32000, 16, 17, 18};
static uint32_t perf[] = {0, 0}; 
enum PERFITEM {
    TRIG_COUNT,
    HOST_RPS,
};
//``````````````````Definitions of PVs````````````````````````````````````````
// Mandatory PVs
static PV pv_version = {"version",
    "simulatedADCs version", T_str, F_R};
static PV pv_run = 	{"run",
    "Start/Stop the streaming of measurements", T_str, F_WED};

// Auxiliary PVs
static PV pv_debug = {"debug",
    "Show debugging messages", 	T_B, F_WEI};
static PV pv_sleep = 	{"sleep",
    "Sleep in the program loop", T_u4, F_R, "ms"};
static PV pv_perf = {"perf",
    "Performance counters. TrigCount, RPS in main loop", T_u4ptr, F_RI};

// ADC-related PVs
static PV pv_adc_offsets = {"adc_offsets",// not implemented in MCUFEC
    "Offsets of all ADC channels", T_i2ptr, F_WE, "counts"};
static PV pv_adc_reclen = {"adc_reclen",
    "Record length. Number of samples of each ADC", T_u2, F_WE};
static PV pv_adc_srate = {"adc_srate",
    "Sampling rate of ADCs", T_u4, F_WE, "Hz"};
static PV pv_adcs = {"adcs",
    "Two-dimentional array[adc#][samples] of ADC samples", T_u2ptr, F_R, "counts"};

// List of active PVs
static PV* _PVs[] = {
  &pv_version,
  &pv_run,
  &pv_debug,
  &pv_sleep,
  &pv_perf,
  &pv_adc_offsets,
  &pv_adc_reclen,
  &pv_adc_srate,
  &pv_adcs,
};
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
// Update ADCs, Called every cycle.
static uint16_t adc_samples[ADC_Max_nChannels*ADC_Max_nSamples];
static void update_adcs(uint32_t base){
    int nsamples = pv_adc_reclen.value.u2;
    // Update ADC data
    for (uint32_t iadc=0; iadc<pv_adcs.shape[0]; iadc++){
        for (uint32_t ii=0; ii<pv_adcs.shape[1]; ii++){
            adc_samples[iadc*nsamples + ii] = (base+iadc+ii) % pv_adcs.shape[1];
        }
    }
    // Update ADC timestamp
    pv_adcs.timestamp.tv_sec = ptimer_now.tv_sec;
    pv_adcs.timestamp.tv_nsec = ptimer_now.tv_nsec;
}
// Periodic update. Called every 10 s.
static uint32_t host_rps;
static uint32_t trig_count;
static void periodic_update(){
    if(DBG>=1)printf("periodic_update @ %i s, host_rps=%i, run: %s\n",
        ptimer_now.tv_sec, host_rps, pv_run.value.str);
    perf[TRIG_COUNT] = trig_count;
    perf[HOST_RPS] = host_rps;
    pv_perf.timestamp.tv_sec = ptimer_now.tv_sec;
    pv_perf.timestamp.tv_nsec = ptimer_now.tv_nsec;
}
//``````````````````Setters````````````````````````````````````````````````````
static int pv_debug_setter(){
    DBG = pv_debug.value.u2;
    printf(">pv_debug_setter %i \n", DBG);
    return 0;
}
// Necessary functions, defined in p2plant
extern void init_encoder(bool subscription);
extern void close_encoder();
extern void send_encoded_buffer();
extern bool plant_client_alive;
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
//`````````````````Entries for main loop``````````````````````````````````````
int plant_init(){
    // Called to initialize PVs, return number of PVs served.
    printf("simulatedADCs %s\n",_VERSION);
    pv_version.set(_VERSION);
    printf("pv_version: %s\n", pv_version.value.str);

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
    pv_adcs.set_shape(nch, nsamples);
    update_adcs(0);
    if(DBG>=2){ 
        printf("ADC:\n");
        int16_t* i2idx = (int16_t*)(adc_samples);
        for (uint32_t ii = 0; ii<(pv_adcs.shape[0]*pv_adcs.shape[1]); ii++){
            printf("%3i,",*i2idx++);
        }
        printf("\n");
    }
    pv_adcs.set(adc_samples);
    PVs = _PVs;
    NPV = (sizeof(_PVs)/sizeof(PV*));
    return NPV;
}
int plant_update()
// Called to update PVs and stream them to client
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

            //`````Stream PVs to client```````````````````````````````````````
            init_encoder(true);
            // Specify here which PVs (e.g. adcs and perf) to deliver.
            // Function reply_value() is defined in pv.h
            reply_value("adcs");
            reply_value("perf");
            close_encoder();
            send_encoded_buffer();
            //,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
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
    clock_gettime(CLOCK_REALTIME, &ptimer_last_update);

    // Main loop
    while (msglen != 0){
        cycle_count++;
        clock_gettime(CLOCK_REALTIME, &ptimer_now);// latch time
        if (has_periodic_interval_elapsed()){
            host_rps = (cycle_count - cycle_count_prev)*1000/LoopReportMS;
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
