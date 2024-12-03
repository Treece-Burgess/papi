//-----------------------------------------------------------------------------
// This program must be compiled using a special makefile:
// make -f ROCM_SMI_Makefile rocm_smi_writeTests.out 
//-----------------------------------------------------------------------------
#define __HIP_PLATFORM_HCC__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "papi.h"
#include <hip/hip_runtime.h>
#include <unistd.h>
#include "rocm_smi.h"   // Need some enumerations.

#include "force_init.h"

#define CHECK(cmd) \
{\
    hipError_t error  = cmd;\
    if (error != hipSuccess) { \
        fprintf(stderr, "error: '%s'(%d) at %s:%d\n", hipGetErrorString(error), error,__FILE__, __LINE__); \
        exit(EXIT_FAILURE);\
	  }\
}

// THIS MACRO EXITS if the papi call does not return PAPI_OK. Do not use for routines that
// return anything else; e.g. PAPI_num_components, PAPI_get_component_info, PAPI_library_init.
#define CALL_PAPI_OK(papi_routine)                                                        \
    do {                                                                                  \
        int _papiret = papi_routine;                                                      \
        if (_papiret != PAPI_OK) {                                                        \
            fprintf(stderr, "%s:%d macro: PAPI Error: function " #papi_routine " failed with ret=%d [%s].\n", \
                    __FILE__, __LINE__, _papiret, PAPI_strerror(_papiret));               \
            exit(-1);                                                                     \
        }                                                                                 \
    } while (0);


#define MEMORY_ALLOCATION_CALL(var)                                     \
    do {                                                                \
        if (var == NULL) {                                              \
            fprintf(stderr, "%s:%d: Error: Memory Allocation Failed \n",\
                    __FILE__, __LINE__);                                \
            exit(-1);                                                   \
        }                                                               \
    } while (0);  


#define MAX_DEVICES    (32)
#define BLOCK_SIZE     (1024)
#define GRID_SIZE      (512)
#define BUF_SIZE       (32 * 1024)
#define ALIGN_SIZE     (8)
#define SUCCESS        (0)
#define NUM_METRIC     (18)
#define NUM_EVENTS     (2)
#define MAX_SIZE       (64*1024*1024)   // 64 MB

typedef union
{
    long long ll;
    unsigned long long ull;
    double    d;
    void *vp;
    unsigned char ch[8];
} convert_64_t;

typedef struct {
    char name[128];
    long long value;
} eventStore_t;

int eventsFoundCount = 0;               // occupants of the array.
int eventsFoundMax;                     // Size of the array.
int eventsFoundAdd = 32;                // Blocksize for increasing the array.
int deviceCount=0;                      // Total devices seen.
int deviceEvents[32] = {0};             // Number of events for each device=??.
eventStore_t *eventsFound = NULL;       // The array.

//-----------------------------------------------------------------------------
// HIP routine: Square each element in the array A and write to array C.
//-----------------------------------------------------------------------------
template <typename T>
__global__ void
vector_square(T *C_d, T *A_d, size_t N)
{
    size_t offset = (blockIdx.x * blockDim.x + threadIdx.x);
    size_t stride = blockDim.x * gridDim.x ;

    for (size_t i=offset; i<N; i+=stride) {
        C_d[i] = A_d[i] * A_d[i];
    }
}

// Show help.
//-----------------------------------------------------------------------------
static void printUsage()
{
    printf("Demonstrate use of ROCM API write routines.\n");
    printf("This program has no options, it will use PAPI to read/write/read\n");
    printf("rocm_smi writable settings and report the results.              \n");
} // end routine.


//-----------------------------------------------------------------------------
// Interpret command line flags.
//-----------------------------------------------------------------------------
void parseCommandLineArgs(int argc, char *argv[])
{
    if(argc < 2) return;

    if((strcmp(argv[1], "--help") == 0) || 
       (strcmp(argv[1], "-help") == 0)  || 
       (strcmp(argv[1], "-h") == 0)) {
        printUsage();
        exit(0);
    }
} // end routine.

//-----------------------------------------------------------------------------
// Main program.
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    int devices, device, i = 0;
    char str[64];
    (void) device;
    (void) str;

    // Parse command line arguments
    parseCommandLineArgs(argc, argv);

    // fprintf(stderr, "Setup PAPI counters internally (PAPI)\n");
    int EventSet = PAPI_NULL;
    int eventCount;
    int ret;
    int k, m, cid=-1;
    (void) m;

    /* PAPI Initialization */
    ret = PAPI_library_init(PAPI_VER_CURRENT);
    if(ret != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI_library_init failed, ret=%i [%s]\n", 
            ret, PAPI_strerror(ret));
        exit(-1);
    }

    printf("PAPI version: %d.%d.%d\n", 
        PAPI_VERSION_MAJOR(PAPI_VERSION), 
        PAPI_VERSION_MINOR(PAPI_VERSION), 
        PAPI_VERSION_REVISION(PAPI_VERSION));
    fflush(stdout);

    // Find rocm_smi component index.
    k = PAPI_num_components();                                          // get number of components.
    for (i=0; i<k && cid<0; i++) {                                      // while not found,
        PAPI_component_info_t *aComponent = 
            (PAPI_component_info_t*) PAPI_get_component_info(i);        // get the component info.     
        if (aComponent == NULL) {                                       // if we failed,
            fprintf(stderr,  "PAPI_get_component_info(%i) failed, "
                "returned NULL. %i components reported.\n", i,k);
            exit(-1);    
        }

       if (strcmp("rocm_smi", aComponent->name) == 0) cid=i;            // If we found our match, record it.
    } // end search components.

    if (cid < 0) {                                                      // if no PCP component found,
        fprintf(stderr, "Failed to find rocm_smi component among %i "
            "reported components.\n", k);
        PAPI_shutdown();
        exit(-1); 
    }

    printf("Found ROCM_SMI Component at id %d\n", cid);

    // Add events at a GPU specific level ... eg rocm:::device=0:Whatever
    eventCount = 0;
    int eventsRead=0;
    (void) eventsRead;

   // Begin enumeration of all events.

    long long value=0;                                              // The only value we read.
    std::string eventName;
    eventName = "rocm_smi:::NUMDevices";

    force_rocm_smi_init(cid);

    CALL_PAPI_OK(PAPI_create_eventset(&EventSet)); 
    CALL_PAPI_OK(PAPI_assign_eventset_component(EventSet, cid)); 
    ret = PAPI_add_named_event(EventSet, eventName.c_str());  
    if (ret == PAPI_OK) {
        CALL_PAPI_OK(PAPI_start(EventSet));
        CALL_PAPI_OK(PAPI_stop(EventSet, &value));
        devices = value;
        printf("Found %i devices.\n", devices);
    } else {
        fprintf(stderr, "FAILED to add event '%s', ret=%i='%s'.\n", eventName.c_str(), ret, PAPI_strerror(ret));
        CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));          // Delete all events in set.
        CALL_PAPI_OK(PAPI_destroy_eventset(&EventSet));         // destroy the event set.
        exit(-1);
    }

    // Do something.
    CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));              // Delete all events in set.


    long long curmax[2];
    CALL_PAPI_OK(PAPI_start(EventSet));
    CALL_PAPI_OK(PAPI_stop(EventSet, curmax));
    printf("Fan speed: current=%lli maximum=%lli.\n", curmax[0], curmax[1]);
    CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));              // Delete all events in set.

    curmax[0]=128;
    eventName = "rocm_smi:::device=0:sensor=0:fan_speed";
    ret = PAPI_add_named_event(EventSet, eventName.c_str());
    if (ret != PAPI_OK) {
        fprintf(stderr, "FAILED to add event '%s', ret=%i='%s'.\n", eventName.c_str(), ret, PAPI_strerror(ret));
        CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));          // Delete all events in set.
        exit(-1);
    }

    CALL_PAPI_OK(PAPI_start(EventSet));
    ret = PAPI_write(EventSet, curmax);
    if ( ret != PAPI_OK ) {
        PAPI_stop(EventSet, curmax);                                // Must be stopped.
        PAPI_cleanup_eventset(EventSet);                            // Empty it.
        PAPI_destroy_eventset(&EventSet);                           // Release memory.
        fprintf(stderr, "PAPI_write failure returned %i, = %s.\n", ret, PAPI_strerror(ret));
    } else {
        printf("Call succeeded to set fan_speed to %llu RPM.\n", curmax[0]);
    }

    // Now try to read it. 
    CALL_PAPI_OK(PAPI_stop(EventSet, &value));
    printf("After set, read-back of fan value is %lli.\n", value);

    CALL_PAPI_OK(PAPI_cleanup_eventset(EventSet));              // Delete all events in set.
    CALL_PAPI_OK(PAPI_destroy_eventset(&EventSet));             // destroy the event set.

    printf("Finished All Events.\n");

    PAPI_shutdown();                                            // Returns no value.
    return(0);                                                  // exit OK.
} // end MAIN.
