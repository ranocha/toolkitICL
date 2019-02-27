#include <unistd.h>
#include <cstdint>

#ifndef RAPL_H_
#define RAPL_H_


class Rapl {

private:

	int fd;
	int core = 0;
	bool pp1_supported = true;
	double power_units, energy_units, time_units;
	double thermal_spec_power, minimum_power, maximum_power, time_window;

	uint64_t pkg;
	uint64_t pp0;
	uint64_t pp1;
	uint64_t dram;

	bool detect_pp1();
	void open_msr();
	uint64_t read_msr(int msr_offset);


public:
	Rapl();
	void sample();
	
	bool get_data(uint64_t &Epkg , uint64_t &Epp0 , uint64_t &Epp1 , uint64_t &Edram);
    double get_e_unit();
	
};

#endif /* RAPL_H_ */
