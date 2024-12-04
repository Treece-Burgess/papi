/****************************/
/* THIS IS OPEN SOURCE CODE */
/****************************/

/**
 * @file    power_report_rocm.cpp
 * CVS:     $Id$
 * @author Tony Castaldo (tonycastaldo@icl.utk.edu)
 * @author Treece Burgess (tburgess@icl.utk.edu) (updated in Winter 2024 to streamline test for GitHub CI;
 *                                                changes tested on Dopamine at ICL with an AMD EPYC 7413)
 *
 * @brief

 * This file reads power limits using ROCM_SMI and writes them
 * periodically to an output file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "papi.h"
#include "papi_test.h"

#include "force_init.h"

// --------- GLOBALS -----------
#define NUM_EVENTS 32               /* Max number of GPUs on a node this code can handle. */
int  Interval = 100;                /* set to read every 100 milliseconds */
int  Duration = 5;                  /* set to run for a total of 5 seconds*/
int  DeviceCount = 0;
double ValueScale = 1000000.;       /* Reports are in millionths of watts. */ 

/* obtain the number of AMD devices on the machine */
void rocmGetDeviceCount(long long *deviceCount) 
{
    int EventSet = PAPI_NULL;
    int retval, devCntEventCode;

    retval = PAPI_event_name_to_code("rocm_smi:::NUMDevices", &devCntEventCode);
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
    }

    retval = PAPI_create_eventset(&EventSet);
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_create_eventset failure", retval);
    }

    retval = PAPI_add_event(EventSet, devCntEventCode);     // Add the event in.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_add_event failure", retval);
    }

    retval = PAPI_start(EventSet);                          // Start the event set.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_start failure", retval);
    }

    retval = PAPI_stop(EventSet, deviceCount);               // Stop and get value.
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval);
    }

    PAPI_cleanup_eventset(EventSet);                        // get rid of this set.
} // end Get Devices.

// Host function
int main( int argc, char** argv )
{

    int retval, i, j;
    int EventSet = PAPI_NULL;
    long long values[NUM_EVENTS];                       // For reading either limit or current power.
    char PowerEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    char minEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    char maxEventName[NUM_EVENTS][PAPI_MAX_STR_LEN];
    int powerEvents[NUM_EVENTS];                        // PAPI codes for current power events.
    int minEvents[NUM_EVENTS];
    int maxEvents[NUM_EVENTS];
    long long minSetting[NUM_EVENTS];
    long long maxSetting[NUM_EVENTS];
    int PowerEventCount = 0, minEventCount = 0, maxEventCount = 0;
    const PAPI_component_info_t *cmpinfo;
    char event_name[PAPI_MAX_STR_LEN];

    /* PAPI Initialization */
    retval = PAPI_library_init( PAPI_VER_CURRENT );
    if( retval != PAPI_VER_CURRENT ) {
        fprintf(stderr, "PAPI_library_init failure returned %i [%s].\n", retval, PAPI_strerror(retval));
        exit(-1);
    }

    /* print the current PAPI library version */
    printf( "PAPI_VERSION : %4d %6d %7d\n",
            PAPI_VERSION_MAJOR( PAPI_VERSION ),
            PAPI_VERSION_MINOR( PAPI_VERSION ),
            PAPI_VERSION_REVISION( PAPI_VERSION ) );

    /* search for the rocm_smi component */ 
    int numcmp = PAPI_num_components();
    int cid = 0;
    for (cid = 0; cid < numcmp; cid++) {
        cmpinfo = PAPI_get_component_info(cid);
        if (cmpinfo == NULL) {
            test_fail(__FILE__, __LINE__,"PAPI_get_component_info failed", -1);
        } else {
            if (strstr( cmpinfo->name, "rocm_smi" )) break;
        }
    }

    /* check to make sure we found the rocm_smi component */
    if (cid==numcmp) {
        test_fail(__FILE__, __LINE__, "rocm_smi component was not found", 0);
    }

    /* make sure that rocm_smi has been initialized */
    force_rocm_smi_init(cid);
    if (cmpinfo->disabled) {
        test_fail(__FILE__, __LINE__, "rocm_smi component is disabled", 0);
    }

    /* get the total number of AMD devices on the machine */
    long long llDC;
    rocmGetDeviceCount(&llDC);
    DeviceCount = (int) llDC;
    printf("AMD Device Count: %d.\n", DeviceCount);
    
    if (DeviceCount < 1) {
        test_fail(__FILE__, __LINE__, "No AMD devices found", 0);
    } 

    if (DeviceCount > NUM_EVENTS) {
        fprintf(stderr, "There are %i GPUs found; this code cannot handle more than %i.\n", DeviceCount, NUM_EVENTS);
        exit(-1);
    } 

    // Scan events to find rocm power events.
    int code = PAPI_NATIVE_MASK;
    int ii=0;
    char *ss;
    int did;
    int event_modifier = PAPI_ENUM_FIRST;
    for (ii = 0; ii < cmpinfo->num_native_events; ii++) {
        retval = PAPI_enum_cmp_event( &code, event_modifier, cid );
        event_modifier = PAPI_ENUM_EVENTS;
        if ( retval != PAPI_OK ) test_fail( __FILE__, __LINE__, "PAPI_enum_cmp_event failure", retval );

        /* convert rocm_smi event to code */
        retval = PAPI_event_code_to_name( code, event_name );
        if (retval != PAPI_OK) {
            test_fail(__FILE__, __LINE__, "PAPI_event_code_to_name failure", retval);
        }

        /* get the device id for indexing */
        ss = strstr(event_name, "device=");                       // Look for the device id.
        if (ss == NULL) continue;                                 // Not a valid name.
        did = atoi(ss+7);                                         // convert it.
        if (did >= DeviceCount) continue;                         // Invalid device count.

        /* current average power consumption in microwatts */
        ss = strstr(event_name, "power_average:");
        if (ss != NULL) {
            strncpy(PowerEventName[did], event_name, PAPI_MAX_STR_LEN);
            PowerEventName[did][PAPI_MAX_STR_LEN-1]=0;
            PowerEventCount++;
            continue;
        }

        /* power cap Minimum settable value, in microwatts */
        ss = strstr(event_name, "power_cap_range_min:");
        if (ss != NULL) {
            strncpy(minEventName[did], event_name, PAPI_MAX_STR_LEN);
            minEventName[did][PAPI_MAX_STR_LEN-1]=0;
            minEventCount++;
            continue;
        }

        /* power cap maximim settable value, in microwatts */
        ss = strstr(event_name, "power_cap_range_max");
        if (ss != NULL) {
            strncpy(maxEventName[did], event_name, PAPI_MAX_STR_LEN);
            maxEventName[did][PAPI_MAX_STR_LEN-1]=0;
            maxEventCount++;
            continue;
        }
    } // end of for each event. 


    if (PowerEventCount != DeviceCount || 
          minEventCount != DeviceCount ||
          maxEventCount != DeviceCount) {
        fprintf(stderr, "Too few ROCM_SMI events found; %d devices, %i PowerEvents, %i maxEvents, %i minEvents. Aborting\n",
                DeviceCount, PowerEventCount, minEventCount, maxEventCount);
        exit(-1);
    }

    /* for each device, convert the rocm_smi power native event names to an event code */
    for(i = 0; i < DeviceCount; i++) {
        /* power_average */
        retval = PAPI_event_name_to_code( ( char * )PowerEventName[i], &powerEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }

        /* power_cap_range_min */
        retval = PAPI_event_name_to_code( ( char * )minEventName[i], &minEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }

        /* power_cap_range_max */
        retval = PAPI_event_name_to_code( ( char * )maxEventName[i], &maxEvents[i] );
        if( retval != PAPI_OK ) {
            test_fail(__FILE__, __LINE__, "PAPI_event_name_to_code failure", retval);
        }
    }

    /* create a PAPI eventset to be used to add events for the power average, 
       power cap mininum, and power cap maximum */
    retval = PAPI_create_eventset( &EventSet );
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_create_eventset failure", retval);
    }

    /* get the minimum values we can set each device to */
    retval = PAPI_add_events(EventSet, minEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_add_events (minEvents) failure", retval); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_start failure", retval);
    }

    retval = PAPI_stop(EventSet, minSetting);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval); 
    }

    /* cleanup eventset to be used to get maximum values */
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_cleanup_eventset failure", retval); 
    }
    
    /* get the maximum values we can set each device to */
    retval = PAPI_add_events(EventSet, maxEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_add_events (maxEvents) failure", retval); 
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_start failure", retval); 
    }

    retval = PAPI_stop(EventSet, maxSetting);
    if( retval != PAPI_OK ) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval);
    }

    /* print out the values for the minimum and maximum we can set each device to */
    for (i = 0; i < DeviceCount; i++) {
        printf("Device %i: MinSetting=%.6f, MaxSetting=%.6f.\n", i, (minSetting[i]/ValueScale), (maxSetting[i])/ValueScale);
    }

    /* cleanup eventset to be used to get average power readings  */
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_cleanup_eventset failure", retval);
    }   

    /* monitor rocm_smi power_average readings */
    retval = PAPI_add_events(EventSet, powerEvents, DeviceCount);
    if( retval != PAPI_OK ) {
        PAPI_cleanup_eventset(EventSet);
        PAPI_destroy_eventset(&EventSet);
        test_fail(__FILE__, __LINE__, "PAPI_add_events failure (for power reading events)", retval);
    }

    retval = PAPI_start(EventSet);
    if( retval != PAPI_OK ) {
        PAPI_cleanup_eventset(EventSet);
        PAPI_destroy_eventset(&EventSet);
        test_fail(__FILE__, __LINE__, "PAPI_start failure", retval);
        
    }

    //--------------------------------------------------------------------------
    // Main part of program, the reading loop.
    //--------------------------------------------------------------------------
    int runCount = 0;
    long long t1, t2;
    double elapsedSec;
    t1 = PAPI_get_real_nsec();                                  // Get the start time.

    /* collect header to printed for time and each accounted for device */
    char col_header[PAPI_HUGE_STR_LEN] = "Time(s) ";
    for (i = 0; i < DeviceCount; i++) {
        snprintf(col_header + strlen(col_header), PAPI_MIN_STR_LEN, "\t%s %d", "Device", i);
    }
    printf("%s\n", col_header);

    /* we run until we hit the runtime duration set with the variable Duration */
    while (1) {
        usleep(Interval*1000);                                  // .. Wait (Interval given in mS, function arg is uS).
        t2 = PAPI_get_real_nsec();                              // .. Find end time.
        PAPI_read(EventSet, values);                            // .. Read instantaneous power consumption.
        elapsedSec = ((double) (t2-t1))/1.e09;                  // .. convert elapsed nanoseconds to seconds.
        printf("%.6f", elapsedSec);                             // .. print the time elapsed in seconds
        for (i=0; i<DeviceCount; i++) {                         // .. for each device
            printf("\t%.6f", (values[i]/ValueScale));           // .... print out a value
        }
        printf("\n");                                           /* Finish the line */

        runCount++;                                             // Count a run.
        if (Duration > 0 && elapsedSec >= Duration) break;      // Exit if time is up.
    }

    retval = PAPI_stop(EventSet, values);
    if(retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_stop failure", retval);
    }

    /* print the total number of reads for the allotted duration  */
    fprintf(stderr, "Total reads: %i.\n", runCount);

    /* cleanup event set */
    retval = PAPI_cleanup_eventset(EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_cleanup_eventset failure", retval);
    }

    /* destroy the eventset  */
    retval = PAPI_destroy_eventset(&EventSet);
    if (retval != PAPI_OK) {
        test_fail(__FILE__, __LINE__, "PAPI_destroy_eventset failure", retval);
    }

    /* if we make it here everything ran successfully */
    PAPI_shutdown();
    test_pass(__FILE__);

    return 0;
} // end main.
