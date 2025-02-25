/**
 * @file    cupti_events.c
 *
 * @author  Treece Burgess tburgess@icl.utk.edu (updated in 2024, redesigned to add device qualifier support.)
 */

#include <dlfcn.h>

//#include <papi.h>
#include "papi.h"
#include "papi_cupti_events.h"
#include "papi_cupti_common.h"
#include "cupti_utils.h"
#include "htable.h"

#include "cupti_events.h"
#include "cupti_metrics.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

/**
 * Event identifier encoding format:
 * +---------------------------------+-------+----+------------+
 * |         unused                  |  dev  | ql |   nameid   |
 * +---------------------------------+-------+----+------------+
 *
 * unused    : 34 bits 
 * device    : 7  bits ([0 - 127] devices)
 * qlmask    : 2  bits (qualifier mask)
 * nameid    : 21: bits (roughly > 2 million event names)
 */
#define EVENTS_WIDTH (sizeof(uint64_t) * 8)
#define DEVICE_WIDTH ( 7)
#define QLMASK_WIDTH ( 2) 
#define NAMEID_WIDTH (21)
#define UNUSED_WIDTH (EVENTS_WIDTH - DEVICE_WIDTH - QLMASK_WIDTH - NAMEID_WIDTH)
#define DEVICE_SHIFT (EVENTS_WIDTH - UNUSED_WIDTH - DEVICE_WIDTH)
#define QLMASK_SHIFT (DEVICE_SHIFT - QLMASK_WIDTH)
#define NAMEID_SHIFT (QLMASK_SHIFT - NAMEID_WIDTH)
#define DEVICE_MASK  ((0xFFFFFFFFFFFFFFFF >> (EVENTS_WIDTH - DEVICE_WIDTH)) << DEVICE_SHIFT)
#define QLMASK_MASK  ((0xFFFFFFFFFFFFFFFF >> (EVENTS_WIDTH - QLMASK_WIDTH)) << QLMASK_SHIFT)
#define NAMEID_MASK  ((0xFFFFFFFFFFFFFFFF >> (EVENTS_WIDTH - NAMEID_WIDTH)) << NAMEID_SHIFT)
#define DEVICE_FLAG  (0x1)

/* Functions needed by CUPTI Events API */
/* ... */

//TODO: Move event_info_t to utils file?
typedef struct {
    int device;
    int flags;
    int nameid;
} event_info_t;

static int num_gpus;

/* main event table to store metrics */
static cuptiu_event_table_t *cuptiu_table_p; // TODO: Possibly make this event take accessible from cupti_utils.h

static int get_ntv_events(cuptiu_event_table_t *evt_table, const char *evt_name, const char *evt_desc, int gpu_id);


static int evt_id_to_info(uint64_t event_id, event_info_t *info);
static int evt_id_create(event_info_t *info, uint64_t *event_id);

static void init_main_htable(void);
static int init_event_table(void);

// Events API Function Pointers
CUptiResult (*cuptiDeviceEnumEventDomainsPtr) (CUdevice device, size_t *arraySizeBytes, CUpti_EventDomainID *domainArray);
CUptiResult (*cuptiDeviceGetNumEventDomainsPtr) (CUdevice device, uint32_t *numDomains);
CUptiResult (*cuptiEventDomainEnumEventsPtr) (CUpti_EventDomainID eventDomain, size_t *sarraySizeBytes, CUpti_EventID *eventArray);
CUptiResult (*cuptiEventGetAttributePtr) (CUpti_EventID event, CUpti_EventAttribute attrib, size_t *valueSize, void *value);
CUptiResult (*cuptiEventDomainGetNumEventsPtr) (CUpti_EventDomainID eventDomain, uint32_t *numEvents);
CUptiResult (*cuptiEventGroupSetsCreatePtr) (CUcontext context, size_t eventIdArraySizeBytes, CUpti_EventID *eventIdArray, CUpti_EventGroupSets **eventGroupPasses);

// Metrics API Function Pointers
CUptiResult (*cuptiDeviceGetNumMetricsPtr) (CUdevice device, uint32_t *numMetrics);
CUptiResult (*cuptiDeviceEnumMetricsPtr) (CUdevice device, size_t *arraySizeBytes, CUpti_MetricID *metricArray);
CUptiResult (*cuptiMetricGetAttributePtr) (CUpti_MetricID metric, CUpti_MetricAttribute attrib, size_t *valueSize, void *value);
CUptiResult (*cuptiMetricCreateEventGroupSetsPtr) (CUcontext context, size_t metricIdArraySizeBytes, CUpti_MetricID *metricIdArray, CUpti_EventGroupSets **eventGroupPasses);


// Helper functions
int determine_dev_cc(int dev_id);

static int load_events_sym(void)
{

    char dlname[] = "libcupti.so";
    char lookup_path[PATH_MAX];

    /* search PAPI_CUDA_CUPTI for libcupti.so (takes precedent over PAPI_CUDA_ROOT) */
    char *papi_cuda_cupti = getenv("PAPI_CUDA_CUPTI");
    if (papi_cuda_cupti) {
        sprintf(lookup_path, "%s/%s", papi_cuda_cupti, dlname);
        dl_cupti = dlopen(lookup_path, RTLD_NOW | RTLD_GLOBAL);
    }   

    const char *standard_paths[] = { 
        "%s/extras/CUPTI/lib64/%s",
        "%s/lib64/%s",
        NULL,
    };  

    /* search PAPI_CUDA_ROOT for libcupti.so */
    char *papi_cuda_root = getenv("PAPI_CUDA_ROOT");
    if (papi_cuda_root && !dl_cupti) {
        dl_cupti = cuptic_load_dynamic_syms(papi_cuda_root, dlname, standard_paths);
    }   

    /* search linux default directories for libcupti.so */
    if (linked_cudart_path && !dl_cupti) {
        dl_cupti = cuptic_load_dynamic_syms(linked_cudart_path, dlname, standard_paths);
    }   

    /* last ditch effort to find libcupti.so */
    if (!dl_cupti) {
        dl_cupti = dlopen(dlname, RTLD_NOW | RTLD_GLOBAL);
        if (!dl_cupti) {
            ERRDBG("Loading libcupti.so failed. Try setting PAPI_CUDA_ROOT\n");
            goto fn_fail;
        }   
    }

    // Events API
    cuptiDeviceEnumEventDomainsPtr   = DLSYM_AND_CHECK(dl_cupti, "cuptiDeviceEnumEventDomains");
    cuptiDeviceGetNumEventDomainsPtr = DLSYM_AND_CHECK(dl_cupti, "cuptiDeviceGetNumEventDomains");
    cuptiEventDomainEnumEventsPtr    = DLSYM_AND_CHECK(dl_cupti, "cuptiEventDomainEnumEvents");
    cuptiEventGetAttributePtr        = DLSYM_AND_CHECK(dl_cupti, "cuptiEventGetAttribute");
    cuptiEventDomainGetNumEventsPtr  = DLSYM_AND_CHECK(dl_cupti, "cuptiEventDomainGetNumEvents");
    cuptiEventGroupSetsCreatePtr     = DLSYM_AND_CHECK(dl_cupti, "cuptiEventGroupSetsCreate");

    // Metrics API
    cuptiDeviceGetNumMetricsPtr        = DLSYM_AND_CHECK(dl_cupti, "cuptiDeviceGetNumMetrics");
    cuptiDeviceEnumMetricsPtr          = DLSYM_AND_CHECK(dl_cupti, "cuptiDeviceEnumMetrics");
    cuptiMetricGetAttributePtr         = DLSYM_AND_CHECK(dl_cupti, "cuptiMetricGetAttribute");
    cuptiMetricCreateEventGroupSetsPtr = DLSYM_AND_CHECK(dl_cupti, "cuptiMetricCreateEventGroupSets"); 

    cuptiGetVersionPtr = DLSYM_AND_CHECK(dl_cupti, "cuptiGetVersion");

    Dl_info info;
    dladdr(cuptiGetVersionPtr, &info);
    LOGDBG("CUPTI library loaded from %s\n", info.dli_fname);
    return PAPI_OK;
fn_fail:
    return PAPI_EMISC;



}

int cuptie_init(void)
{
    int papi_errno;

    papi_errno = load_events_sym();
    if (papi_errno != PAPI_OK) {
        printf("Failed to load events api.\n");
    }

    // Get the number of GPUs on the machine
    papi_errno = cuptic_device_get_count(&num_gpus);
    if (papi_errno != PAPI_OK) {
        return PAPI_EMISC;
    } 

    // Init the main htable
    init_main_htable();

    // Init the event table
    init_event_table();

    CUresult cuError = cuInitPtr(0);
    if (cuError != CUDA_SUCCESS) {
        return PAPI_EMISC;
    }

    return PAPI_OK;

    //cuptic_err_set_last("CUDA events API not implemented.");
    //return PAPI_ENOIMPL;
}

static void init_main_htable(void) 
{
    int i, val = 1, base = 2; 
 
    /* allocate (2 ^ NAMEID_WIDTH) metric names, this matches the 
       number of bits for the event encoding format */
    for (i = 0; i < NAMEID_WIDTH; i++) {
        val *= base;
    }    
   
    /* initialize struct */ 
    cuptiu_table_p = papi_malloc(sizeof(cuptiu_event_table_t));
    cuptiu_table_p->capacity = val; 
    cuptiu_table_p->count = 0; 
    cuptiu_table_p->events = papi_calloc(val, sizeof(cuptiu_event_t)); 
   
    /* initialize the main hash table for metric collection */ 
    htable_init(&cuptiu_table_p->htable);
}

int init_event_table(void)
{
    int dev_id, found, table_idx = 0;
    int papi_errno;

    //Declarations for Events API
    uint32_t numDomains, numEvents;
    size_t size;
    CUpti_EventDomainID *domainArray;
    CUpti_EventID *eventArray; 

    CUpti_EventGroupSets *eventGroupPasses = (CUpti_EventGroupSets *) calloc(1, sizeof(CUpti_EventGroupSets));


    // Event/Metric Metadata
    char evtDesc[PAPI_HUGE_STR_LEN], fullEvtDesc[PAPI_HUGE_STR_LEN];
    char evtName[PAPI_2MAX_STR_LEN], fullEvtName[PAPI_2MAX_STR_LEN];

    CUcontext ctx;
    CUdevice device;

    // Declarations specifically for the Metrics API
    uint32_t numMetrics;
    CUpti_MetricID *metricIdList;

    
    // Loop through all of the available devices on the machine
    for (dev_id = 0; dev_id < num_gpus; dev_id++) {
        // Skip devices that will require the Perfworks API to be profiled
        if (determine_dev_cc(dev_id) == 0) {
            continue;
        }
        //found = find_same_chipname(dev_id);
        /* unique device found, collect metadata  */
        //if (found == -1) {
            /* increment table index */
        //    if (dev_id > 0)
        //        table_idx++;

            /* for each unique device found, store both the total number of metrics and metric names */
            //cuptiu_table_p->avail_gpu_info[table_idx].num_metrics = getMetricNameBeginParams.numMetrics;
            //cuptiu_table_p->avail_gpu_info[table_idx].metric_names = getMetricNameBeginParams.ppMetricNames;
        //}
        /* device metadata already collected, set table index */
        //else {
            /* set table_idx to */
        //    table_idx = found;
        //}

        cudaCheckErrors( cuDeviceGetPtr(&device, dev_id), return PAPI_EMISC );
        // We have to create a Cuda Context to get the number of passes for an event or metric
        // this will be destroyed at the end
        cudaCheckErrors( cuCtxCreatePtr(&ctx, 0, dev_id), return PAPI_EMISC ); 

        // TODO: Swap PAPI_EMISC for PAPI_ESYS? Makes more sense to me overall. Get a second opinion.

        // Workflow for Events API //

        // Get the total number of Event Domains for a device
        cuptiCheckErrors( cuptiDeviceGetNumEventDomainsPtr(device, &numDomains),  return PAPI_EMISC );

        // Get the event domains for a device
        domainArray = (CUpti_EventDomainID *) calloc(numDomains, sizeof(CUpti_EventDomainID));
        size = numDomains * sizeof(CUpti_EventDomainID);
        cuptiCheckErrors( cuptiDeviceEnumEventDomainsPtr(device, &size, domainArray), return PAPI_EMISC );

        int i,j;
        // Go over the total number of domains found for the device
        for (i = 0; i < numDomains; i++) {
            // For each domain, get the total number of events 
            cuptiCheckErrors( cuptiEventDomainGetNumEventsPtr(domainArray[i], &numEvents), return PAPI_EMISC );

            // Allocate memory
            eventArray = (CUpti_EventID *) calloc(numEvents, sizeof(CUpti_EventID));
            size = numEvents * sizeof(CUpti_EventID);

            // For the domain, get the actual events
            cuptiCheckErrors( cuptiEventDomainEnumEventsPtr(domainArray[i], &size, eventArray), return PAPI_EMISC );

            // Go over the total number of events found in the domain for the device
            for (j = 0; j < numEvents; j++) {
                // Name attribute
                size = PAPI_2MAX_STR_LEN * sizeof(char);
                cuptiCheckErrors( cuptiEventGetAttributePtr(eventArray[j], CUPTI_EVENT_ATTR_NAME, &size, evtName), return PAPI_EMISC );
                snprintf(fullEvtName, PAPI_2MAX_STR_LEN, "event.%s", evtName);

                // Long description attribute
                size = PAPI_HUGE_STR_LEN * sizeof(char);
                cuptiCheckErrors( cuptiEventGetAttributePtr(eventArray[j], CUPTI_EVENT_ATTR_LONG_DESCRIPTION, &size, evtDesc), return PAPI_EMISC );

                // For each event, add the number of passes that are required to the description
                cuptiCheckErrors( cuptiEventGroupSetsCreatePtr(ctx, sizeof(CUpti_EventID), &eventArray[j], &eventGroupPasses), return PAPI_EMISC);
                snprintf(fullEvtDesc, PAPI_HUGE_STR_LEN, "%s. Numpass=%d", evtDesc, eventGroupPasses->numSets);
                
                papi_errno = get_ntv_events(cuptiu_table_p, fullEvtName, fullEvtDesc, dev_id);
                if (papi_errno != PAPI_OK) {
                    return papi_errno;
                }
            }
        }

        // Workflow for Metrics API

        // Get the number of metrics for the device
        cuptiCheckErrors( cuptiDeviceGetNumMetricsPtr(device, &numMetrics), return PAPI_EMISC );

        // Get the metrics for the device
        size = numMetrics * sizeof(CUpti_MetricID);
        metricIdList = (CUpti_MetricID *) calloc(numMetrics, sizeof(CUpti_MetricID));
        cuptiCheckErrors( cuptiDeviceEnumMetricsPtr(device, &size, metricIdList), return PAPI_EMISC );

        // For each metric get the name and description attribute
        for (i = 0; i < numMetrics; i++) {
            // Name attribute
            size = PAPI_2MAX_STR_LEN * sizeof(char);
            cuptiCheckErrors( cuptiMetricGetAttributePtr(metricIdList[i], CUPTI_METRIC_ATTR_NAME, &size, evtName), return PAPI_EMISC );
            snprintf(fullEvtName, PAPI_2MAX_STR_LEN, "metric.%s", evtName);

            // Long description attribute
            size = PAPI_HUGE_STR_LEN * sizeof(char);
            cuptiCheckErrors( cuptiMetricGetAttributePtr(metricIdList[i], CUPTI_METRIC_ATTR_LONG_DESCRIPTION, &size, evtDesc), return PAPI_EMISC );

            // For each metric, add the number of passes that are required to the description
            cuptiMetricCreateEventGroupSetsPtr(ctx, sizeof(CUpti_MetricID), &metricIdList[i], &eventGroupPasses);

            snprintf(fullEvtDesc, PAPI_HUGE_STR_LEN, "%s. Numpass=%d", evtDesc, eventGroupPasses->numSets);

            papi_errno = get_ntv_events(cuptiu_table_p, fullEvtName, fullEvtDesc, dev_id);
            if (papi_errno != PAPI_OK) {
                return papi_errno;
            }
        } 
        cuCtxDestroyPtr(ctx); 
    }

    return PAPI_OK;
}

int get_ntv_events(cuptiu_event_table_t *evt_table, const char *evt_name, const char *evt_desc, int gpu_id)
{
    int *count = &evt_table->count;
    cuptiu_event_t *events = evt_table->events;

    if (evt_name == NULL) {
        return PAPI_EINVAL;
    }

    if (evt_table->count >= evt_table->capacity) {
        printf("Table count is larger than allocated capacity.");
        return PAPI_EBUG;
    }

    cuptiu_event_t *event;
    if (htable_find(evt_table->htable, evt_name, (void **) &event) != HTABLE_SUCCESS) {
        event = &events[*count];
        (*count)++;

        snprintf(event->name, PAPI_2MAX_STR_LEN, "%s", evt_name);
        snprintf(event->desc, PAPI_HUGE_STR_LEN, "%s", evt_desc);

        if (htable_insert(evt_table->htable, evt_name, event) != HTABLE_SUCCESS) {
            return PAPI_ESYS;
        } 
    }
    cuptiu_dev_set(&event->device_map, gpu_id);

    return PAPI_OK;
}

/** @class cuptip_evt_code_to_info
  * @brief Takes a Cuda native event code and collects info such as Cuda native 
  *        event name, Cuda native event description, and number of devices. 
  * @param event_code
  *   Cuda native event code. 
  * @param *info
  *   Structure for member variables such as symbol, short description, and 
  *   long desctiption. 
*/
int cuptie_evt_code_to_info(uint64_t event_code, PAPI_event_info_t *info)
{

    int papi_errno, i, gpu_id;
    char description[PAPI_HUGE_STR_LEN];

    /* get the events nameid and flags */
    event_info_t inf;
    papi_errno = evt_id_to_info(event_code, &inf);
    if (papi_errno != PAPI_OK) {
        return papi_errno;
    }

    switch (inf.flags) {
        case (0):
            /* store details for the Cuda event */
            snprintf( info->symbol, PAPI_HUGE_STR_LEN, "%s", cuptiu_table_p->events[inf.nameid].name );
            snprintf( info->short_descr, PAPI_MIN_STR_LEN, "%s", cuptiu_table_p->events[inf.nameid].desc );
            snprintf( info->long_descr, PAPI_HUGE_STR_LEN, "%s", cuptiu_table_p->events[inf.nameid].desc );
            break;
        case DEVICE_FLAG:
        {
            int init_metric_dev_id;
            char devices[PAPI_MAX_STR_LEN] = { 0 };
            for (i = 0; i < num_gpus; ++i) {
                if (cuptiu_dev_check(cuptiu_table_p->events[inf.nameid].device_map, i)) {
                    /* for an event, store the first device found to use with :device=#, 
                       as on a heterogenous system events may not appear on each device */
                    if (devices[0] == '\0') {
                        init_metric_dev_id = i;
                    }

                    sprintf(devices + strlen(devices), "%i,", i);
                }
            }
            *(devices + strlen(devices) - 1) = 0;

            /* store details for the Cuda event */
            snprintf( info->symbol, PAPI_HUGE_STR_LEN, "%s:device=%i", cuptiu_table_p->events[inf.nameid].name, init_metric_dev_id );
            snprintf( info->short_descr, PAPI_MIN_STR_LEN, "%s masks:Mandatory device qualifier [%s]",
                     cuptiu_table_p->events[inf.nameid].desc, devices );
            snprintf( info->long_descr, PAPI_HUGE_STR_LEN, "%s masks:Mandatory device qualifier [%s]",
                      cuptiu_table_p->events[inf.nameid].desc, devices );
            break;
        }
        default:
            papi_errno = PAPI_EINVAL;
    }

    return papi_errno;
}


int cuptie_ctx_create(void *thr_info, cuptie_control_t *pctl)
{
    return PAPI_ENOIMPL;
}

int cuptie_ctx_start(cuptie_control_t ctl)
{
    return PAPI_ENOIMPL;
}

int cuptie_ctx_read(cuptie_control_t ctl, long long **values)
{
    return PAPI_ENOIMPL;
}

int cuptie_ctx_stop(cuptie_control_t ctl)
{
    return PAPI_ENOIMPL;
}

int cuptie_ctx_reset(cuptie_control_t ctl)
{
    return PAPI_ENOIMPL;
}

int cuptie_ctx_destroy(cuptie_control_t *pctl)
{
    return PAPI_ENOIMPL;
}

/** @class evt_id_create
  * @brief Create event ID. Function is needed for cuptip_event_enum.
  *
  * @param *info
  *   Structure which contains member variables of device, flags, and nameid.
  * @param *event_id
  *   Created event id.
*/

//TODO: Move both evt_id_create and evt_id_to_info out to possibly cupti_utils.h
int evt_id_create(event_info_t *info, uint64_t *event_id)
{

    *event_id  = (uint64_t)(info->device   << DEVICE_SHIFT);
    *event_id |= (uint64_t)(info->flags    << QLMASK_SHIFT);
    *event_id |= (uint64_t)(info->nameid   << NAMEID_SHIFT);

    return PAPI_OK;
}

/** @class evt_id_to_info
  * @brief Convert event id to info. Function is needed for cuptip_event_enum.
  *
  * @param event_id
  *   An event id.
  * @param *info
  *   Structure which contains member variables of device, flags, and nameid.
*/
int evt_id_to_info(uint64_t event_id, event_info_t *info)
{
    info->device   = (int)((event_id & DEVICE_MASK) >> DEVICE_SHIFT);
    info->flags    = (int)((event_id & QLMASK_MASK) >> QLMASK_SHIFT);
    info->nameid   = (int)((event_id & NAMEID_MASK) >> NAMEID_SHIFT);

    if (info->device >= num_gpus) {
        return PAPI_ENOEVNT;
    }    

    if (0 == (info->flags & DEVICE_FLAG) && info->device > 0) { 
        return PAPI_ENOEVNT;
    }    

    if (info->nameid >= cuptiu_table_p->count) {
        return PAPI_ENOEVNT;
    }

    return PAPI_OK;
}

int cuptie_evt_enum(uint64_t *event_code, int modifier)
{

    int papi_errno = PAPI_OK;
    event_info_t info;
    SUBDBG("ENTER: event_code: %lu, modifier: %d\n", *event_code, modifier);

    switch(modifier) {
        case PAPI_ENUM_FIRST:
            if(cuptiu_table_p->count == 0) { 
                papi_errno = PAPI_ENOEVNT;
                break;
            }    
            info.device = 0; 
            info.flags = 0; 
            info.nameid = 0; 
            papi_errno = evt_id_create(&info, event_code);
            break;
        case PAPI_ENUM_EVENTS:
            papi_errno = evt_id_to_info(*event_code, &info);
            if (papi_errno != PAPI_OK) {
                break;
            }    
            if (cuptiu_table_p->count > info.nameid + 1) { 
                info.device = 0; 
                info.flags = 0; 
                info.nameid++;
                papi_errno = evt_id_create(&info, event_code);
                break;
            }    
            papi_errno = PAPI_END;
            break;
        case PAPI_NTV_ENUM_UMASKS:
            papi_errno = evt_id_to_info(*event_code, &info);
            if (papi_errno != PAPI_OK) {
                break;
            }    
            if (info.flags == 0){
                info.device = 0; 
                info.flags = DEVICE_FLAG;
                papi_errno = evt_id_create(&info, event_code);
                break;
            }    
            papi_errno = PAPI_END;
            break;
        default:
            papi_errno = PAPI_EINVAL;
    }    
    SUBDBG("EXIT: %s\n", PAPI_strerror(papi_errno));
    return papi_errno;
}

int cuptie_evt_code_to_descr(uint64_t event_code, char *descr, int len) 
{
    return PAPI_ENOIMPL;
}

int cuptie_evt_name_to_code(const char *name, uint64_t *event_code)
{


    return PAPI_ENOIMPL;
}

int cuptie_evt_code_to_name(uint64_t event_code, char *name, int len)
{
    int papi_errno, str_len;
    event_info_t info;
    
    papi_errno = evt_id_to_info(event_code, &info);
    if (papi_errno != PAPI_OK) {
        return papi_errno;
    }

    switch (info.flags) {
        case (DEVICE_FLAG):
            str_len = snprintf(name, len, "%s:device=%i", cuptiu_table_p->events[info.nameid].name, info.device);
            if (str_len < 0 || str_len > len) {
                ERRDBG("String has not been completely written.\n");
                return PAPI_ESYS;
            }
            break;
        default:
            str_len = snprintf(name, len, "%s", cuptiu_table_p->events[info.nameid].name, info.device);
            if (str_len < 0 || str_len > len) {
                ERRDBG("String has not been completely written.\n");
                return PAPI_ESYS;
            }   
            break; 
   }
   return papi_errno;
}

int cuptie_shutdown(void)
{
    return PAPI_ENOIMPL;
}

int determine_dev_cc(int dev_id) 
{
    int cc_major;

    cudaDeviceGetAttributePtr(&cc_major, cudaDevAttrComputeCapabilityMajor, dev_id);

    if (cc_major > 7)
        return 0;
    else if (cc_major == 7)
        return 1;
    else 
        return 2;
}
