#include <cstdio>
#include <string>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "rapl.h"

#define MSR_RAPL_POWER_UNIT            0x606

/*
 * Platform specific RAPL Domains.
 * see Intel architecture datahseets for more information.
 */
/* Package RAPL  */
#define MSR_PKG_RAPL_POWER_LIMIT       0x610
#define MSR_PKG_ENERGY_STATUS          0x611
#define MSR_PKG_PERF_STATUS            0x13
#define MSR_PKG_POWER_INFO             0x614

/* PP0 RAPL */
#define MSR_PP0_POWER_LIMIT            0x638
#define MSR_PP0_ENERGY_STATUS          0x639
#define MSR_PP0_POLICY                 0x63A
#define MSR_PP0_PERF_STATUS            0x63B

/* PP1 RAPL */
#define MSR_PP1_POWER_LIMIT            0x640
#define MSR_PP1_ENERGY_STATUS          0x641
#define MSR_PP1_POLICY                 0x642

/* DRAM RAPL  */
#define MSR_DRAM_POWER_LIMIT           0x618
#define MSR_DRAM_ENERGY_STATUS         0x619
#define MSR_DRAM_PERF_STATUS           0x61B
#define MSR_DRAM_POWER_INFO            0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET              0
#define POWER_UNIT_MASK                0x0F

#define ENERGY_UNIT_OFFSET             0x08
#define ENERGY_UNIT_MASK               0x1F00

#define TIME_UNIT_OFFSET               0x10
#define TIME_UNIT_MASK                 0xF000

#define SIGNATURE_MASK                 0xFFFF0
#define IVYBRIDGE_E                    0x306F0
#define SANDYBRIDGE_E                  0x206D0


Rapl::Rapl() {

	open_msr();
	pp1_supported = detect_pp1();

	/* Read MSR_RAPL_POWER_UNIT Register */
	uint64_t raw_value = read_msr(MSR_RAPL_POWER_UNIT);
	power_units = pow(0.5,	(double) (raw_value & 0xf));
	energy_units = pow(0.5,	(double) ((raw_value >> 8) & 0x1f));
	time_units = pow(0.5,	(double) ((raw_value >> 16) & 0xf));

	/* Read MSR_PKG_POWER_INFO Register */
	raw_value = read_msr(MSR_PKG_POWER_INFO);
	thermal_spec_power = power_units * ((double)(raw_value & 0x7fff));
	minimum_power = power_units * ((double)((raw_value >> 16) & 0x7fff));
	maximum_power = power_units * ((double)((raw_value >> 32) & 0x7fff));
	time_window = time_units * ((double)((raw_value >> 48) & 0x7fff));

}



bool Rapl::detect_pp1() {
	uint32_t eax_input = 1;
	uint32_t eax;
	__asm__("cpuid;"
			:"=a"(eax)               // EAX into b (output)
			:"0"(eax_input)          // 1 into EAX (input)
			:"%ebx","%ecx","%edx");  // clobbered registers

	uint32_t cpu_signature = eax & SIGNATURE_MASK;
	if (cpu_signature == SANDYBRIDGE_E || cpu_signature == IVYBRIDGE_E) {
		return false;
	}
	return true;
}

void Rapl::open_msr() {
	std::stringstream filename_stream;
	filename_stream << "/dev/cpu/" << core << "/msr";
	fd = open(filename_stream.str().c_str(), O_RDONLY);
	if (fd < 0) {
		if ( errno == ENXIO) {
			fprintf(stderr, "rdmsr: No CPU %d\n", core);
			exit(2);
		} else if ( errno == EIO) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
			exit(3);
		} else {
			perror("rdmsr:open");
			fprintf(stderr, "Trying to open %s\n",
					filename_stream.str().c_str());
			exit(127);
		}
	}
}

uint64_t Rapl::read_msr(int msr_offset) {
	uint64_t data;
	if (pread(fd, &data, sizeof(data), msr_offset) != sizeof(data)) {
		perror("read_msr():pread");
		exit(127);
	}
	return data;
}

void Rapl::sample() {
	uint32_t max_int = ~((uint32_t) 0);

	pkg = read_msr(MSR_PKG_ENERGY_STATUS) & max_int;
	pp0 = read_msr(MSR_PP0_ENERGY_STATUS) & max_int;
	dram = read_msr(MSR_DRAM_ENERGY_STATUS) & max_int;
	
	if (pp1_supported) {
		pp1 = read_msr(MSR_PP1_ENERGY_STATUS) & max_int;
	} else {
		pp1 = 0;
	}

}

bool Rapl::get_data(uint64_t &Epkg , uint64_t &Epp0 , uint64_t &Epp1 , uint64_t &Edram) {

     Epkg = pkg;
	 Epp0 = pp0;
	 Epp1 = pp1;
	 Edram = dram;
	return true;
	
}

double Rapl::get_e_unit() {

	return energy_units;
	
}

uint32_t Rapl::get_TDP(){
    return (uint32_t)round(thermal_spec_power);
}
