#include <unistd.h>
#include <cstdint>

#ifndef RAPL_H_
#define RAPL_H_


class Rapl {

private:

	int fd0;
	int fd1;
	bool pp1_supported = true;
	bool socket1_detected = false;
	
	double power_units, energy_units, time_units;
	double thermal_spec_power, minimum_power, maximum_power, time_window;

	uint64_t pkg_0;
	uint64_t pp0_0;
	uint64_t pp1_0;
	uint64_t dram_0;

	uint64_t pkg_1;
	uint64_t pp0_1;
	uint64_t pp1_1;
	uint64_t dram_1;
	
	void open_msr(unsigned int node, int & fd);
	uint64_t read_msr(int msr_offset, int & fd);


public:
	Rapl();
	void sample();
	
	bool get_socket0_data(uint64_t &Epkg , uint64_t &Epp0 , uint64_t &Epp1 , uint64_t &Edram);
	bool get_socket1_data(uint64_t &Epkg , uint64_t &Epp0 , uint64_t &Epp1 , uint64_t &Edram);
    double get_e_unit();
	uint32_t get_TDP();
	uint32_t get_temp();
	bool detect_igp();
	bool detect_socket1();
	
};

#endif /* RAPL_H_ */
