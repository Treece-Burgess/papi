# ***NOTE*** The Environment Variable PAPI_ROCM_ROOT must be defined for 
# programs to compile correctly. one typical location is /opt/rocm, but
# contact your sysadmin if you cannot find it.
NAME=rocm_smi
include ../../Makefile_comp_tests.target
PAPI_ROCM_ROOT ?= /opt/rocm
HIP_PATH= ${PAPI_ROCM_ROOT}/hip
HIPCC=$(HIP_PATH)/bin/hipcc

INCLUDE += -I$(PAPI_ROCM_ROOT)/include
INCLUDE += -I$(PAPI_ROCM_ROOT)/include/rocm_smi
INCLUDE += -I$(PAPI_ROCM_ROOT)/include/hip
INCLUDE += -I$(PAPI_ROCM_ROOT)/include/hsa
INCLUDE += -I$(PAPI_ROCM_ROOT)/include/rocprofiler
INCLUDE += -I$(PAPI_ROCM_ROOT)/include/rocblas
LDFLAGS = -ldl -g -L$(PAPI_ROCM_ROOT)/lib/rocblas -lrocblas

%.o:%.c
	@echo "INCLUDE=" $(INCLUDE)
	$(CC) $(CFLAGS) $(OPTFLAGS) $(INCLUDE) -c -o $@ $<

%.o:%.cpp
	@echo "INCLUDE=" $(INCLUDE)
	@echo "CFLAGS=" $(CFLAGS)
	g++ $(CFLAGS) $(OPTFLAGS) $(INCLUDE) -c -o $@ $<

TESTS = rocm_smi_all rocm_smi_example \
        power_monitor_with_rocm_smi \
        rocm_smi_writeTests \
        rocm_smi_list_events

rocm_smi_tests: $(TESTS)

# Note: We compile .o separately from the executable link; some versions of hipcc
#       have trouble managing libraries if we try to do both in a single step.

rocm_smi_all.o: rocm_smi_all.cpp $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -c $< -o $@

rocm_smi_all: rocm_smi_all.o $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -o $@ $< $(UTILOBJS) $(PAPILIB) $(LDFLAGS) 

rocm_smi_example.o: rocm_smi_example.cpp $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -c $< -o $@

rocm_smi_example: rocm_smi_example.o $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -o $@ $< $(UTILOBJS) $(PAPILIB) $(LDFLAGS) -pthread

power_monitor_with_rocm_smi.o: power_monitor_with_rocm_smi.cpp $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -c $< -o $@

power_monitor_with_rocm_smi: power_monitor_with_rocm_smi.o $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -o $@ $< $(UTILOBJS) $(PAPILIB) $(LDFLAGS) 

rocm_smi_writeTests.o: rocm_smi_writeTests.cpp $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -c $< -o $@

rocm_smi_writeTests: rocm_smi_writeTests.o $(UTILOBJS) $(PAPILIB)
	$(HIPCC) $(CFLAGS) $(INCLUDE) -o $@ $< $(UTILOBJS) $(PAPILIB) $(LDFLAGS)

rocm_smi_list_events: rocm_smi_list_events.o $(UTILOBJS) $(PAPILIB)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(UTILOBJS) $(PAPILIB)

clean:
	rm -f *.o $(TESTS)

checkpath: 
	echo PAPI_ROCM_ROOT = $(PAPI_ROCM_ROOT)
	echo HIP_PATH = $(HIP_PATH)
	echo HIPCC = $(HIPCC)
	echo INCLUDE = $(INCLUDE)
	echo LDFLAGS = $(LDFLAGS)
	echo CFLAGS = $(CFLAGS)

