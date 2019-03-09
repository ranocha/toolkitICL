/* This project is licensed under the terms of the Creative Commons CC BY-NC-ND 4.0 license. */

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <math.h>
#include <sstream>
#include <string>
#include <thread>
#include <time.h>
#include <vector>
#include <codecvt>
#include <string>

#include "opencl_include.hpp"
#include "util.hpp"
#include "hdf5_io.hpp"
#include "ocl_dev_mgr.hpp"
#include "timer.hpp"


#if defined(_WIN32)
#include <windows.h>
#include <sys/timeb.h>

int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
  static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

  SYSTEMTIME  system_time;
  FILETIME    file_time;
  uint64_t    time;

  GetSystemTime(&system_time);
  SystemTimeToFileTime(&system_time, &file_time);
  time = ((uint64_t)file_time.dwLowDateTime);
  time += ((uint64_t)file_time.dwHighDateTime) << 32;

  tp->tv_sec = (long)((time - EPOCH) / 10000000L);
  tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
  return 0;
}

#else
#include <sys/time.h>
#endif


inline double timeval2storage(const timeval& timepoint) {
  // convert microseconds to seconds using a resolution of milliseconds
  return timepoint.tv_sec + 1.e-3 * (timepoint.tv_usec / 1000);
}


using namespace std;

#if defined(USEAMDP)
// Disable MS compiler warnings:
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

// Include controller API header file.
#include "AMDTPowerProfileApi.h"

std::vector<std::string> AMDP_names;

bool initAMDPP(uint32_t sample_rate) {
  AMDTResult hResult = AMDT_STATUS_OK;

  // Initialize online mode
  hResult = AMDTPwrProfileInitialize(AMDT_PWR_MODE_TIMELINE_ONLINE);
  // check AMDT_STATUS_OK == hResult
  AMDTUInt32 nbrCounters = 0;
  AMDTPwrCounterDesc* pCounters = nullptr;

  hResult = AMDTPwrGetSupportedCounters(&nbrCounters, &pCounters);
  // check AMDT_STATUS_OK == hResult

  //  cout << endl << nbrCounters << endl;
  for (AMDTUInt32 idx = 0; idx < nbrCounters; idx++)
  {
    AMDTPwrCounterDesc counterDesc = pCounters[idx];

    //get only energy - for now
    if (counterDesc.m_category == AMDT_PWR_CATEGORY_CORRELATED_POWER) {
      AMDTPwrEnableCounter(counterDesc.m_counterID);

      //cout << endl << counterDesc.m_name << endl;
      AMDP_names.push_back(counterDesc.m_name);
    }

  }
  AMDTPwrSetTimerSamplingPeriod(sample_rate);
  return true;
}

std::vector<double> AMD_pwr_time;
std::vector<cl_float> AMD_pwr0[5]; //Socket 0

bool AMD_log_pwr = false;
cl_uint AMD_p_rate;

void AMD_log_pwr_func()
{
  AMDTPwrStartProfiling();

  while (AMD_log_pwr == true) {

    timeval rawtime;

    std::this_thread::sleep_for(std::chrono::milliseconds(AMD_p_rate));
    gettimeofday(&rawtime, NULL);

    AMDTResult hResult = AMDT_STATUS_OK;
    AMDTPwrSample* pSampleData = nullptr;
    AMDTUInt32 nbrSamples = 0;
    hResult = AMDTPwrReadAllEnabledCounters(&nbrSamples, &pSampleData);
    //  cout<< nbrSamples<<" / "<< AMDP_names.size() <<endl;
    //  if (nbrSamples== AMDP_names.size())
    {
      // for (size_t i = 0; i < AMDP_names.size(); i++) //THERE IS A BUG HERE SOMEWHERE
      for (size_t i = 0; i < 1; i++)
      {
         AMD_pwr0[i].push_back(pSampleData[i].m_counterValues->m_data);
      }
      AMD_pwr_time.push_back(timeval2storage(rawtime));
    }
  }

  AMDTPwrStopProfiling();
}

#endif // USEAMDP

#if defined(USEIRAPL)
#include "rapl.h"
bool intel_log_power = false;
bool intel_log_temp = false;
cl_uint intel_power_rate = 0;
cl_uint intel_temp_rate = 0;
std::vector<double> intel_power_time;
std::vector<double> intel_temp_time;
std::vector<uint64_t> intel_power0[5]; //Socket 0
std::vector<uint64_t> intel_power1[5]; //Socket 1
std::vector<std::string> MSR_names {"Package", "Cores", "DRAM", "GT"};
std::vector<cl_ushort> intel_temp;

Rapl *rapl;

void intel_log_power_func()
{
  timeval rawtime;

  if (intel_power_rate > 0)
  {
    uint64_t pkg;
    uint64_t pp0;
    uint64_t pp1;
    uint64_t dram;

    intel_power0[0].clear();
    intel_power0[1].clear();
    intel_power0[2].clear();
    intel_power0[3].clear();
    intel_power1[0].clear();
    intel_power1[1].clear();
    intel_power1[2].clear();
    intel_power1[3].clear();
    intel_power_time.clear();

    while (intel_log_power == true) {

      rapl->sample();
      std::this_thread::sleep_for(std::chrono::milliseconds(intel_power_rate/2));
      gettimeofday(&rawtime, NULL);
      std::this_thread::sleep_for(std::chrono::milliseconds(intel_power_rate/2));
      intel_power_time.push_back(timeval2storage(rawtime));

      rapl->get_socket0_data(pkg, pp0, pp1, dram);
      intel_power0[0].push_back(pkg);
      intel_power0[1].push_back(pp0);
      intel_power0[2].push_back(dram);
      intel_power0[3].push_back(pp1);

      if (rapl->detect_socket1() == true) {
        rapl->get_socket1_data(pkg, pp0, pp1, dram);
        intel_power1[0].push_back(pkg);
        intel_power1[1].push_back(pp0);
        intel_power1[2].push_back(dram);
        intel_power1[3].push_back(pp1);
      }

    }
  }
}

void intel_log_temp_func()
{
  uint32_t temp;
  timeval rawtime;

  if (intel_temp_rate > 0)
  {
    intel_temp.clear();
    intel_temp_time.clear();

    while (intel_log_temp == true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(intel_temp_rate));
      temp = rapl->get_temp();
      gettimeofday(&rawtime, NULL);
      intel_temp_time.push_back(timeval2storage(rawtime));
      intel_temp.push_back(temp);
    }
  }
}

#endif // USEIRAPL


#if defined(_WIN32)
#if defined(USEIPG)
#include "IntelPowerGadgetLib.h"

std::string utf16ToUtf8(const std::wstring& utf16Str)
{
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
  return conv.to_bytes(utf16Str);
}

CIntelPowerGadgetLib energyLib;

std::vector<double> intel_power_time;
std::vector<double> intel_temp_time;
std::vector<std::string> MSR_names;
std::vector<int> MSR;
bool intel_log_power = false;
bool intel_log_temp = false;
cl_uint intel_power_rate = 0;
cl_uint intel_temp_rate = 0;

std::vector<float> intel_power[5];
std::vector<cl_ushort> intel_temp;

void intel_log_temp_func()
{
  int temp;
  timeval rawtime;

  if (intel_temp_rate > 0)
  {
    intel_temp.clear();
    intel_temp_time.clear();

    while (intel_log_temp == true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(intel_temp_rate));
      energyLib.GetTemperature(0, &temp);
      gettimeofday(&rawtime, NULL);
      intel_temp_time.push_back(timeval2storage(rawtime));
      intel_temp.push_back(temp);
    }
  }
}

void intel_log_power_func()
{
  double data[3];
  int nData;
  timeval rawtime;

  if (intel_power_rate > 0)
  {
    intel_power.clear();
    intel_power_time.clear();

    while (intel_log_power == true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(intel_power_rate));
      energyLib.ReadSample();
      gettimeofday(&rawtime, NULL);
      intel_power_time.push_back(timeval2storage(rawtime));

      for (unsigned int i = 0; i < MSR.size(); i++) {
        energyLib.GetPowerData(0, MSR.at(i), data, &nData);
        intel_power[i].push_back((float)data[0]);
      }

    }

  }
}


#endif // USEIPG
#endif // USEIPG


#if defined(USENVML)
#include <nvml.h>
bool nvidia_log_power = false;
bool nvidia_log_temp = false;
cl_uint nvidia_power_rate = 0;
cl_uint nvidia_temp_rate = 0;

std::vector<cl_ushort> nvidia_temp;
std::vector<double> nvidia_temp_time;

std::vector<float> nvidia_power;
std::vector<double> nvidia_power_time;

void nvidia_log_power_func()
{
  if (nvidia_power_rate > 0) {
    unsigned int temp;
    nvmlReturn_t result;
    timeval rawtime;

    nvidia_power.clear();
    nvidia_power_time.clear();

    result = nvmlInit();
    if (NVML_SUCCESS == result)
    {
      nvmlDevice_t device;
      nvmlDeviceGetHandleByIndex(0, &device);

      while (nvidia_log_power == true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(nvidia_power_rate));

        nvmlDeviceGetPowerUsage(device, &temp);
        gettimeofday(&rawtime, NULL);
        nvidia_power_time.push_back(timeval2storage(rawtime));
        // convert milliwatt to watt
        nvidia_power.push_back(1.e-3f * (float)(temp));
      }

      nvmlShutdown();
    }
  }
}

void nvidia_log_temp_func()
{
  if (nvidia_temp_rate > 0) {
    unsigned int temp;
    nvmlReturn_t result;
    timeval rawtime;

    nvidia_temp.clear();
    nvidia_temp_time.clear();

    result = nvmlInit();
    if (NVML_SUCCESS == result)
    {
      nvmlDevice_t device;
      nvmlDeviceGetHandleByIndex(0, &device);

      while (nvidia_log_temp == true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(nvidia_temp_rate));
        result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
        gettimeofday(&rawtime, NULL);
        nvidia_temp_time.push_back(timeval2storage(rawtime));
        nvidia_temp.push_back(temp);
      }

      nvmlShutdown();
    }
  }
}

#endif // USENVML


#if defined(_WIN32)
typedef LONG NTSTATUS, *PNTSTATUS;
#define STATUS_SUCCESS (0x00000000)

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOEXW);

RTL_OSVERSIONINFOEXW GetRealOSVersion() {
  HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
  if (hMod) {
    RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
    if (fxPtr != nullptr) {
      RTL_OSVERSIONINFOEXW  rovi = { 0 };
      rovi.dwOSVersionInfoSize = sizeof(rovi);
      if (STATUS_SUCCESS == fxPtr(&rovi)) {
        return rovi;
      }

    }
  }

  RTL_OSVERSIONINFOEXW rovi = { 0 };
  return rovi;
}
#else
#include <sys/utsname.h>
#endif

std::string getOS()
{
  std::stringstream version;
#if defined(_WIN32)

  version<<"Windows " << GetRealOSVersion().dwMajorVersion<<"."<< GetRealOSVersion().dwMinorVersion;

  if (GetRealOSVersion().wProductType == VER_NT_WORKSTATION) {
    version << " Workstation";
  }
  else {
    version << " Server";
  }

#elif defined(__APPLE__)

  char line[256];
  string product_name, product_version;
  FILE* sw_vers = popen("sw_vers", "r");
  while (fgets(&line[0], sizeof(line), sw_vers) != nullptr) {
    if (strncmp(line, "ProductName:", 12) == 0) {
      product_name = string(&line[13]);
      product_name.pop_back(); // erase the newline
    }
    else if (strncmp(line, "ProductVersion:", 15) == 0) {
      product_version = string(&line[16]);
      product_version.pop_back(); // erase the newline
    }
  }
  pclose(sw_vers);
  version << product_name << " " << product_version;

#else // linux

  struct utsname unameData;
  uname(&unameData);
  string line;

  version << unameData.sysname << " ";

  ifstream rel_file("/etc/os-release");
  if (rel_file.is_open()) {
    while (rel_file.good()) {
      getline(rel_file, line);
      if (line.size() >= 1 && line.substr(0,11) == "PRETTY_NAME") {
        version << line.substr(13, line.length()-14);
        break;
      }
    }

    rel_file.close();
  }
  else {
    version << "Unknown Distribution";
  }

  version << "/" << unameData.release << "/" << unameData.version;

#endif

  return version.str();
}


// command line arguments
char const* getCmdOption(char** begin, char** end, std::string const& option)
{
  char** itr = find(begin, end, option);
  if (itr != end && ++itr != end)  {
    return *itr;
  }
  return 0;
}

bool cmdOptionExists(char** begin, char** end, const std::string& option)
{
  return find(begin, end, option) != end;
}

void print_help()
{
  cout
    << "Usage: toolkitICL [options] -c config.h5" << endl
    << "Options:" << endl
    << "  -d device_id: \n    Use the device specified by `device_id`." << endl
    << "  -b: \n    Activate the benchmark mode (additional delay before & after runs)." << endl
    << "  -c config.h5: \n    Specify the URL `config.h5` of the HDF5 configuration file." << endl
#if defined(USENVML)
    << "  -nvidia_power sample_rate: \n    Log Nvidia GPU power consumption with `sample_rate` (ms)" << endl
    << "  -nvidia_temp sample_rate: \n    Log Nvidia GPU temperature with `sample_rate` (ms)" << endl
#endif
#if defined(USEIPG) || defined(USEIRAPL)
    << "  -intel_power sample_rate: \n    Log Intel system power consumption with `sample_rate` (ms)" << endl
    << "  -intel_temp  sample_rate: \n    Log Intel package temperature with `sample_rate` (ms)" << endl
#endif
#if defined(USEAMDP)
    << "  -amd_cpu_power sample_rate: \n    Log AMD CPU power consumption with `sample_rate` (ms)" << endl
#endif
    << endl;
}


int main(int argc, char *argv[]) {

  Timer timer; //used to track performance

  cl_uint deviceIndex = 0; // set default OpenCL Device

  // parse command line arguments
  bool benchmark_mode = false;
  if (cmdOptionExists(argv, argv + argc, "-b"))  {
    benchmark_mode = true;
    cout << "Benchmark mode" << endl << endl;
  }

  if (cmdOptionExists(argv, argv + argc, "-d")) {
    char const* dev_id = getCmdOption(argv, argv + argc, "-d");
    deviceIndex = atoi(dev_id);
  }

  if (cmdOptionExists(argv, argv + argc, "-h") || !cmdOptionExists(argv, argv + argc, "-c")) {
    print_help();
    return 0;
  }
  char const* filename = getCmdOption(argv, argv + argc, "-c");

#if defined(USENVML)
  if (cmdOptionExists(argv, argv + argc, "-nvidia_power")) {
    char const* tmp = getCmdOption(argv, argv + argc, "-nvidia_power");
    nvidia_power_rate = atoi(tmp);
    nvidia_log_power = true;
  }

  if (cmdOptionExists(argv, argv + argc, "-nvidia_temp")) {
    char const* tmp = getCmdOption(argv, argv + argc, "-nvidia_temp");
    nvidia_temp_rate = atoi(tmp);
    nvidia_log_temp = true;
  }
#endif
#if defined(USEIPG) || defined(USEIRAPL)
  if (cmdOptionExists(argv, argv + argc, "-intel_power")) {
    char const* tmp = getCmdOption(argv, argv + argc, "-intel_power");
    intel_power_rate = atoi(tmp);
    intel_log_power = true;
  }
if (cmdOptionExists(argv, argv + argc, "-intel_temp")) {
   char const* tmp = getCmdOption(argv, argv + argc, "-intel_temp");
   intel_temp_rate = atoi(tmp);
   intel_log_temp = true;
  }
#endif
#if defined(USEAMDP)
  if (cmdOptionExists(argv, argv + argc, "-amd_cpu_power")) {
    char const* tmp = getCmdOption(argv, argv + argc, "-amd_cpu_power");
    AMD_p_rate = atoi(tmp);
    AMD_log_pwr = true;
  }
#endif
  ocl_dev_mgr& dev_mgr = ocl_dev_mgr::getInstance();
  cl_uint devices_availble=dev_mgr.get_avail_dev_num();

  cout << "Available devices: " << devices_availble << endl
       << dev_mgr.get_avail_dev_info(deviceIndex).name.c_str() << endl;
  cout << "OpenCL version: " << dev_mgr.get_avail_dev_info(deviceIndex).ocl_version.c_str() << endl;
  cout << "Memory limit: "<< dev_mgr.get_avail_dev_info(deviceIndex).max_mem << endl;
  cout << "WG limit: "<< dev_mgr.get_avail_dev_info(deviceIndex).wg_size << endl << endl;
  dev_mgr.init_device(deviceIndex);

  string kernel_url;
  if (h5_check_object(filename, "Kernel_URL") == true) {
    h5_read_string(filename, "Kernel_URL", kernel_url);
    cout << "Reading kernel from file: " << kernel_url << "... " << endl;
  }
  else if (h5_check_object(filename, "Kernel_Source") == true) {
    cout << "Reading kernel from HDF5 file... " << endl;
    std::vector<std::string> kernel_source;
    h5_read_strings(filename, "Kernel_Source", kernel_source);
    ofstream tmp_clfile;
    tmp_clfile.open("tmp_kernel.cl");
    for (string const& kernel : kernel_source) {
      tmp_clfile << kernel << endl;
    }

    tmp_clfile.close();
    kernel_url = string("tmp_kernel.cl");
  }
  else {
    cerr << "No kernel information found! " << endl;
    return -1;
  }

  std::vector<std::string> kernel_list;
  h5_read_strings(filename, "Kernels", kernel_list);

  cl_ulong kernel_repetitions = 1;
  if (h5_check_object(filename, "Kernel_Repetitions")) {
    kernel_repetitions = h5_read_single<cl_ulong>(filename, "Kernel_Repetitions");
  }
  if (kernel_repetitions <= 0) {
    cout << "Warning: Setting `kernel_repetitions = " << kernel_repetitions << "` implies that no kernels are executed." << endl;
  }

  dev_mgr.add_program_url(0, "ocl_Kernel", kernel_url);

  string settings;
  h5_read_string(filename, "Kernel_Settings", settings);


  uint64_t num_kernels_found = 0;
  num_kernels_found = dev_mgr.compile_kernel(0, "ocl_Kernel", settings);
  if (num_kernels_found == 0) {
    cerr << ERROR_INFO << "No valid kernels found" << endl;
    return -1;
  }

  std::vector<std::string> found_kernels;
  dev_mgr.get_kernel_names(0, "ocl_Kernel", found_kernels);
  cout << "Found Kernels: " << found_kernels.size() << endl;
  if (found_kernels.size() == 0) {
    cerr << ERROR_INFO << "No valid kernels found." << endl;
    return -1;
  }

  cout << "Number of Kernels to execute: " << kernel_list.size() * kernel_repetitions << endl;

  //TODO: Clean up; debug mode?
  // for (uint32_t kernel_idx = 0; kernel_idx < kernel_list.size(); kernel_idx++) {
  //   cout <<"Found : "<< kernel_list.at(kernel_idx) << endl;
  // }

  cout << "Ingesting HDF5 config file..." << endl;

  std::vector<std::string> data_names;
  std::vector<HD5_Type> data_types;
  std::vector<size_t> data_sizes;
  h5_get_content(filename, "/Data/", data_names, data_types, data_sizes);

  cout << "Creating output HDF5 file..." << endl;
  string out_name = "out_" + string(filename);

  if (fileExists(out_name)) {
    remove(out_name.c_str());
    cout << "Old HDF5 data file found and deleted!" << endl;
  }
  h5_write_string(out_name.c_str(), "Host_OS", getOS().c_str());

  h5_write_string(out_name.c_str(), "Kernel_Settings", settings);

  std::vector<cl::Buffer> data_in;
  bool blocking = CL_TRUE;

  //TODO: Implement functionality! Allow other integer types instead of cl_int?
  vector<cl_int> data_rw_flags(data_names.size(), 0);

  uint64_t push_time, pull_time;
  push_time = timer.getTimeMicroseconds();

  for(cl_uint i = 0; i < data_names.size(); i++) {
    try {
      uint8_t *tmp_data = nullptr;
      size_t var_size = 0;

      switch (data_types.at(i)) {
        case H5_float:
          var_size = data_sizes.at(i)*sizeof(float);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<float>(filename, data_names.at(i).c_str(), (float*)tmp_data);
          break;
        case H5_double:
          var_size = data_sizes.at(i)*sizeof(double);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<double>(filename, data_names.at(i).c_str(), (double*)tmp_data);
          break;
        case H5_char:
          var_size=data_sizes.at(i)*sizeof(cl_char);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_char>(filename, data_names.at(i).c_str(), (cl_char*)tmp_data);
          break;
        case H5_uchar:
          var_size = data_sizes.at(i)*sizeof(cl_uchar);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_uchar>(filename, data_names.at(i).c_str(), (cl_uchar*)tmp_data);
          break;
        case H5_short:
          var_size=data_sizes.at(i)*sizeof(cl_short);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_short>(filename, data_names.at(i).c_str(), (cl_short*)tmp_data);
          break;
        case H5_ushort:
          var_size=data_sizes.at(i)*sizeof(cl_ushort);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_ushort>(filename, data_names.at(i).c_str(), (cl_ushort*)tmp_data);
          break;
        case H5_int:
          var_size=data_sizes.at(i)*sizeof(cl_int);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_int>(filename, data_names.at(i).c_str(), (cl_int*)tmp_data);
          break;
        case H5_uint:
          var_size=data_sizes.at(i)*sizeof(cl_uint);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_uint>(filename, data_names.at(i).c_str(), (cl_uint*)tmp_data);
          break;
        case H5_long:
          var_size=data_sizes.at(i)*sizeof(cl_long);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_long>(filename, data_names.at(i).c_str(), (cl_long*)tmp_data);
          break;
        case H5_ulong:
          var_size=data_sizes.at(i)*sizeof(cl_ulong);
          tmp_data = new uint8_t[var_size];
          h5_read_buffer<cl_ulong>(filename, data_names.at(i).c_str(), (cl_ulong*)tmp_data);
          break;
        default:
          cerr << ERROR_INFO << "Data type '" << data_types.at(i) << "' unknown." << endl;
          break;
      }

      switch (data_rw_flags.at(i)) {
        case 0:
          data_in.push_back(cl::Buffer(dev_mgr.get_context(0), CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, var_size));
          dev_mgr.get_queue(0, 0).enqueueWriteBuffer(data_in.back(), blocking, 0, var_size, tmp_data);
          break;
        case 1:
          data_in.push_back(cl::Buffer(dev_mgr.get_context(0), CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, var_size));
          dev_mgr.get_queue(0, 0).enqueueWriteBuffer(data_in.back(), blocking, 0, var_size, tmp_data);
          break;
        case 2:
          data_in.push_back(cl::Buffer(dev_mgr.get_context(0), CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, var_size));
          break;
      }

      for (uint32_t kernel_idx = 0; kernel_idx < found_kernels.size(); kernel_idx++) {
        dev_mgr.getKernelbyName(0, "ocl_Kernel", found_kernels.at(kernel_idx))->setArg(i, data_in.back());
      }

      if (tmp_data != nullptr) {
        delete[] tmp_data; tmp_data = nullptr;
      }
    }
    catch (cl::Error err) {
      std::cerr << ERROR_INFO << "Exception: " << err.what() << std::endl;
    }
  }

  dev_mgr.get_queue(0, 0).finish(); // Buffer Copy is asynchronous

  push_time = timer.getTimeMicroseconds() - push_time;

  cout << "Setting range..." << endl << endl;

  cl::NDRange range_start;
  cl::NDRange global_range;
  cl::NDRange local_range;

  //TODO: Allow other integer types instead of cl_int?
  cl_int tmp_range[3];
  h5_read_buffer<cl_int>(filename, "Global_Range", tmp_range);
  global_range = cl::NDRange(tmp_range[0], tmp_range[1], tmp_range[2]);
  h5_write_buffer<cl_int>(out_name, "Global_Range", tmp_range, 3,
                          "Argument `global_work_size` of the OpenCL function `clEnqueueNDRangeKernel`.");

  h5_read_buffer<cl_int>(filename, "Range_Start", tmp_range);
  range_start = cl::NDRange(tmp_range[0], tmp_range[1], tmp_range[2]);
  h5_write_buffer<cl_int>(out_name, "Range_Start", tmp_range, 3,
                          "Argument `global_work_offset` of the OpenCL function `clEnqueueNDRangeKernel`.");

  h5_read_buffer<cl_int>(filename, "Local_Range", tmp_range);
  h5_write_buffer<cl_int>(out_name, "Local_Range", tmp_range, 3,
                          "Argument `local_work_size` of the OpenCL function `clEnqueueNDRangeKernel`.");
  if ((tmp_range[0]==0) && (tmp_range[1]==0) && (tmp_range[2]==0)) {
    local_range = cl::NullRange;
  }
  else {
    local_range = cl::NDRange(tmp_range[0], tmp_range[1], tmp_range[2]);
  }

#if defined(USEAMDP)
  if (AMD_log_pwr)
  {
    cout << "Using AMD Power Profiling interface..." << endl;
    h5_create_dir(out_name, "/Housekeeping");
    h5_create_dir(out_name, "/Housekeeping/amd");

    initAMDPP(AMD_p_rate);
  }
  std::thread AMD_log_pwr_thread(AMD_log_pwr_func);

#endif

#if defined(USENVML)
  if (nvidia_log_power || nvidia_log_temp)
  {
    cout << "Using NVML interface..." << endl;
    h5_create_dir(out_name, "/Housekeeping");
    h5_create_dir(out_name, "/Housekeeping/nvidia");
  }
  std::thread nvidia_log_power_thread(nvidia_log_power_func);
  std::thread nvidia_log_temp_thread(nvidia_log_temp_func);
#endif

#if defined(USEIPG)
  if (intel_log_power || intel_log_temp)
  {
    cout << "Using Intel Power Gadget interface..." << endl;
    h5_create_dir(out_name, "/Housekeeping");
    h5_create_dir(out_name, "/Housekeeping/intel");
  }

  if (intel_log_power)
  {
    if (energyLib.IntelEnergyLibInitialize() == false)
    {
      cout << "Intel Power Gadget interface error!" << endl;
      return -1;
    }

    double CPU_TDP = 0;
    energyLib.GetTDP(0, &CPU_TDP);
    h5_write_single<float>(out_name, "/Housekeeping/intel/TDP" , (float)(CPU_TDP),
                           "Thermal Design Power in watt");

    int numCPUnodes = 0;
    energyLib.GetNumNodes(&numCPUnodes);

    int numMsrs = 0;
    energyLib.GetNumMsrs(&numMsrs);

    //This is necesarry for initalization
    energyLib.ReadSample();
    energyLib.ReadSample();
    energyLib.ReadSample();

    for (int j = 0; j < numMsrs; j++)
    {
      int funcID;
      double data[3];
      int nData;
      wchar_t szName[MAX_PATH];

      energyLib.GetMsrFunc(j, &funcID);
      energyLib.GetMsrName(j, szName);

      if ((funcID == 1)) {
        MSR.push_back(j);
        if (utf16ToUtf8(szName) == "Processor") {
          MSR_names.push_back("Package");
        }
        else {
          if (utf16ToUtf8(szName) == "IA") {
            MSR_names.push_back("Cores");
          }
          else {
            MSR_names.push_back(utf16ToUtf8(szName));
          }
        }
      }

      //Get Package Power Limit
      if ((funcID == 3) ) {
        double data[3];
        int nData;
        energyLib.GetPowerData(0, j, data, &nData);
        std::string varname = "/Housekeeping/intel/" +  utf16ToUtf8(szName) + "_Power_Limit";
        h5_write_single<double>(out_name, varname.c_str() , data[0]);
      }

    }
  }
  std::thread intel_log_power_thread(intel_log_power_func);
  std::thread intel_log_temp_thread(intel_log_temp_func);

#endif

#if defined(USEIRAPL)
  if (intel_log_power || intel_log_temp)
  {
    cout << "Using Intel MSR interface..." << endl;
    h5_create_dir(out_name, "/Housekeeping");
    h5_create_dir(out_name, "/Housekeeping/intel");
    rapl = new Rapl();
  }

  if (intel_log_power)
  {
    h5_write_single<float>(out_name, "/Housekeeping/intel/TDP", rapl->get_TDP(),
                           "Thermal Design Power in watt");
  }

  std::thread intel_log_power_thread(intel_log_power_func);
  std::thread intel_log_temp_thread(intel_log_temp_func);

#endif


  if (benchmark_mode == true) {
    cout << "Sleeping for 4s" << endl << endl;
    std::chrono::milliseconds timespan(4000);
    std::this_thread::sleep_for(timespan);
  }

  cout << "Launching kernel..." << endl;


  timeval start_timeinfo;

  //get execution timestamp
  gettimeofday(&start_timeinfo, NULL);

  uint64_t exec_time = 0;
  uint32_t kernels_run=0;

  uint64_t total_exec_time = timer.getTimeMicroseconds();

  for (cl_ulong repetition = 0; repetition < kernel_repetitions; ++repetition) {
    for (string const& kernel_name : kernel_list) {
      exec_time = exec_time + dev_mgr.execute_kernelNA(*(dev_mgr.getKernelbyName(0, "ocl_Kernel", kernel_name)),
                                                       dev_mgr.get_queue(0, 0), range_start, global_range, local_range);
      kernels_run++;
    }
  }

  total_exec_time = timer.getTimeMicroseconds() - total_exec_time;
  h5_write_single<double>(out_name, "Total_ExecTime", 1.e-6 * total_exec_time,
                          "Time in seconds of the total execution (data transfer, kernel, and host code).");

  cout << "Kernels executed: " << kernels_run << endl;
  cout << "Kernel runtime: " << exec_time/1000 << " ms" << endl;

  if (benchmark_mode == true) {
    cout << endl << "Sleeping for 4s" << endl;
    std::chrono::milliseconds timespan(4000);

    std::this_thread::sleep_for(timespan);
  }

 cout << "Saving results... " << endl;


#if defined(USEAMDP)
  AMD_log_pwr = false;
  AMD_log_pwr_thread.join();

  if (AMD_p_rate > 0)
  {
    h5_write_buffer<double>(out_name, "/Housekeeping/amd/Power_Time", AMD_pwr_time.data(), AMD_pwr_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");

    for (size_t i = 0; i < AMDP_names.size(); i++)
    {
      std::string varname = "/Housekeeping/amd/" + AMDP_names.at(i);
      h5_write_buffer<cl_float>(out_name, varname.c_str(), AMD_pwr0[i].data(), AMD_pwr0[i].size());
    }
  }
#endif

#if defined(USEIRAPL)

  intel_log_power = false;
  intel_log_power_thread.join();

  intel_log_temp = false;
  intel_log_temp_thread.join();

  if (intel_power_rate > 0)
  {
    // size()-1 because differences are computed later
    h5_write_buffer<double>(out_name, "/Housekeeping/intel/Power_Time", intel_power_time.data(), intel_power_time.size()-1,
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");

    std::vector<float> tmp_vector;

    size_t max_entries = MSR_names.size();
    if (rapl->detect_igp() == false) {
      // no GT data
      max_entries--;
    }

    for (size_t i = 0; i < max_entries; i++)
    {
      tmp_vector.clear();

      for (size_t j = 0; j < intel_power0[i].size()-1; j++)
      {
        tmp_vector.push_back((rapl->get_e_unit()*(double)(intel_power0[i].at(j+1)-intel_power0[i].at(j))) / ((double)intel_power_rate*0.001));
      }
      std::string varname = "/Housekeeping/intel/" + MSR_names.at(i) + "0";
      h5_write_buffer<float>(out_name, varname.c_str(), tmp_vector.data(), tmp_vector.size(),
                             "Power in watt");
    }

    if (rapl->detect_socket1() == true)
    {
      for (size_t i = 0; i < max_entries; i++)
      {
        tmp_vector.clear();

        for (size_t j = 0; j < intel_power1[i].size()-1; j++)
        {
          tmp_vector.push_back((rapl->get_e_unit()*(double)(intel_power1[i].at(j+1)-intel_power1[i].at(j)))/((double)intel_power_rate*0.001));
        }
        std::string varname = "/Housekeeping/intel/" + MSR_names.at(i) + "1";
        h5_write_buffer<float>(out_name, varname.c_str(), tmp_vector.data(), tmp_vector.size(),
                               "Power in watt");
      }
    }
  }

  if (intel_temp_rate > 0)
  {
    h5_write_buffer<double>(out_name, "/Housekeeping/intel/Temperature_Time", intel_temp_time.data(), intel_temp_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");

    h5_write_buffer<cl_ushort>(out_name, "/Housekeeping/intel/Package_Temperature", intel_temp.data(), intel_temp.size(),
                               "Temperature in degree Celsius");
  }

#endif

#if defined(USEIPG)

  intel_log_power = false;
  intel_log_power_thread.join();

  intel_log_temp = false;
  intel_log_temp_thread.join();

  if (intel_power_rate > 0)
  {
    h5_write_buffer<double>(out_name, "/Housekeeping/intel/Power_Time", intel_power_time.data(), intel_power_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");

    for (size_t i = 0; i < MSR_names.size(); i++)
    {
      std::string varname = "/Housekeeping/intel/" + MSR_names.at(i) + "0";
      h5_write_buffer<float>(out_name, varname.c_str(), intel_power[i].data(), intel_power[i].size(),
                             "Power in watt");
    }
  }

  if (intel_temp_rate > 0)
  {
    h5_write_buffer<double>(out_name, "/Housekeeping/intel/Temperature_Time", intel_temp_time.data(), intel_temp_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");

    h5_write_buffer<cl_ushort>(out_name, "/Housekeeping/intel/Package_Temperature", intel_temp.data(), intel_temp.size(),
                               "Temperature in degree Celsius");
  }

#endif

#if defined(USENVML)
  nvidia_log_power = false;
  nvidia_log_temp = false;
  nvidia_log_power_thread.join();
  nvidia_log_temp_thread.join();

  if (nvidia_power_rate > 0) {

    h5_write_buffer<float>(out_name, "/Housekeeping/nvidia/Power", nvidia_power.data(), nvidia_power.size(),
                           "Power in watt");

    h5_write_buffer<double>(out_name, "/Housekeeping/nvidia/Power_Time", nvidia_power_time.data(), nvidia_power_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");
  }

  if (nvidia_temp_rate > 0) {

    h5_write_buffer<cl_ushort>(out_name, "/Housekeeping/nvidia/Temperature", nvidia_temp.data(), nvidia_temp.size(),
                               "Temperature in degree Celsius");

    h5_write_buffer<double>(out_name, "/Housekeeping/nvidia/Temperature_Time", nvidia_temp_time.data(), nvidia_temp_time.size(),
                            "POSIX UTC time in seconds since 1970-01-01T00:00.000 (resolution of milliseconds)");
  }
#endif


  char time_buffer[90];
  time_t tmp_time = start_timeinfo.tv_sec;
  strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", localtime(&tmp_time));
  sprintf(time_buffer, "%s.%03ld", time_buffer, start_timeinfo.tv_usec / 1000);
  h5_write_string(out_name, "Kernel_ExecStart", time_buffer);
  h5_write_string(out_name, "OpenCL_Device", dev_mgr.get_avail_dev_info(deviceIndex).name.c_str());
  h5_write_string(out_name, "OpenCL_Platform", dev_mgr.get_avail_dev_info(deviceIndex).platform_name.c_str());
  h5_write_string(out_name, "OpenCL_Version", dev_mgr.get_avail_dev_info(deviceIndex).ocl_version.c_str());
  h5_write_single<double>(out_name, "Kernel_ExecTime", 1.e-6 * exec_time,
                          "Time in seconds of the kernel execution (no host code).");
  h5_write_single<double>(out_name, "Data_LoadTime", 1.e-6 * push_time,
                          "Time in seconds of the data transfer: hdf5 input file -> host -> device.");

  h5_create_dir(out_name, "/Data");

  pull_time = timer.getTimeMicroseconds();

  uint32_t buffer_counter = 0;

  for(cl_uint i = 0; i < data_names.size(); i++) {
    try {
      uint8_t *tmp_data = nullptr;
      size_t var_size = 0;

      switch (data_types.at(i)) {
        case H5_float:  var_size=data_sizes.at(i)*sizeof(cl_float);  break;
        case H5_double: var_size=data_sizes.at(i)*sizeof(cl_double); break;
        case H5_char:   var_size=data_sizes.at(i)*sizeof(cl_char);   break;
        case H5_uchar:  var_size=data_sizes.at(i)*sizeof(cl_uchar);  break;
        case H5_short:  var_size=data_sizes.at(i)*sizeof(cl_short);  break;
        case H5_ushort: var_size=data_sizes.at(i)*sizeof(cl_ushort); break;
        case H5_int:    var_size=data_sizes.at(i)*sizeof(cl_int);    break;
        case H5_uint:   var_size=data_sizes.at(i)*sizeof(cl_uint);   break;
        case H5_long:   var_size=data_sizes.at(i)*sizeof(cl_long);   break;
        case H5_ulong:  var_size=data_sizes.at(i)*sizeof(cl_ulong);  break;
        default: cerr << ERROR_INFO << "Data type '" << data_types.at(i) << "' unknown." << endl;
      }

      tmp_data = new uint8_t[var_size];

      switch (data_rw_flags.at(buffer_counter)) {
        case 0: dev_mgr.get_queue(0, 0).enqueueReadBuffer(data_in.at(buffer_counter), blocking, 0, var_size, tmp_data); break;
        case 1: break;
        case 2: dev_mgr.get_queue(0, 0).enqueueReadBuffer(data_in.at(buffer_counter), blocking, 0, var_size, tmp_data); break;
      }

      dev_mgr.get_queue(0, 0).finish(); //Buffer Copy is asynchronous

      switch (data_types.at(i)){
        case H5_float:  h5_write_buffer<float>(    out_name, data_names.at(i).c_str(), (float*)tmp_data,     data_sizes.at(buffer_counter)); break;
        case H5_double: h5_write_buffer<double>(   out_name, data_names.at(i).c_str(), (double*)tmp_data,    data_sizes.at(buffer_counter)); break;
        case H5_char:   h5_write_buffer<cl_char>(  out_name, data_names.at(i).c_str(), (cl_char*)tmp_data,   data_sizes.at(buffer_counter)); break;
        case H5_uchar:  h5_write_buffer<cl_uchar>( out_name, data_names.at(i).c_str(), (cl_uchar*)tmp_data,  data_sizes.at(buffer_counter)); break;
        case H5_short:  h5_write_buffer<cl_short>( out_name, data_names.at(i).c_str(), (cl_short*)tmp_data,  data_sizes.at(buffer_counter)); break;
        case H5_ushort: h5_write_buffer<cl_ushort>(out_name, data_names.at(i).c_str(), (cl_ushort*)tmp_data, data_sizes.at(buffer_counter)); break;
        case H5_int:    h5_write_buffer<cl_int>(   out_name, data_names.at(i).c_str(), (cl_int*)tmp_data,    data_sizes.at(buffer_counter)); break;
        case H5_uint:   h5_write_buffer<cl_uint>(  out_name, data_names.at(i).c_str(), (cl_uint*)tmp_data,   data_sizes.at(buffer_counter)); break;
        case H5_long:   h5_write_buffer<cl_long>(  out_name, data_names.at(i).c_str(), (cl_long*)tmp_data,   data_sizes.at(buffer_counter)); break;
        case H5_ulong:  h5_write_buffer<cl_ulong>( out_name, data_names.at(i).c_str(), (cl_ulong*)tmp_data,  data_sizes.at(buffer_counter)); break;
        default: cerr << ERROR_INFO << "Data type '" << data_types.at(i) << "' unknown." << endl;
      }
      if (tmp_data != nullptr) {
        delete[] tmp_data; tmp_data = nullptr;
      }
      buffer_counter++;
    }
    catch (cl::Error err) {
      std::cerr << ERROR_INFO << "Exception: " << err.what() << std::endl;
    }
  }

  pull_time = timer.getTimeMicroseconds() - pull_time;
  h5_write_single<double>(out_name, "Data_StoreTime", 1.e-6 * pull_time,
                          "Time in seconds of the data transfer: device -> host -> hdf5 output file.");

  return 0;
}
