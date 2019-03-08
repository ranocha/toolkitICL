
# include header directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${OpenCL_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS} ../include)

# header files of the project
IF(USEIRAPL)
set(HEADER ../include/opencl_include.hpp ../include/ocl_dev_mgr.hpp ../include/timer.hpp ../include/util.hpp ../include/rapl.h)
ELSE(USEIRAPL)
IF(USEIPG)
set(HEADER ../include/opencl_include.hpp ../include/ocl_dev_mgr.hpp ../include/timer.hpp ../include/util.hpp ../include/IntelPowerGadgetLib.h)
ELSE(USEIPG)
set(HEADER ../include/opencl_include.hpp ../include/ocl_dev_mgr.hpp ../include/timer.hpp ../include/util.hpp)
ENDIF(USEIPG)
ENDIF(USEIRAPL)

IF(USEAMDP)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${OpenCL_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS} $ENV{AMDPROFILERPATH}/../include ../include)
set(HEADER ../include/opencl_include.hpp ../include/ocl_dev_mgr.hpp ../include/timer.hpp ../include/util.hpp ../include/rapl.h)
ENDIF(USEAMDP)

IF(USEIRAPL)
set(SOURCES main.cpp ocl_dev_mgr.cpp rapl.cpp ${HEADER})
ELSE(USEIRAPL)
IF(USEIPG)
set(SOURCES main.cpp ocl_dev_mgr.cpp IntelPowerGadgetLib.cpp ${HEADER})
ELSE(USEIPG)
set(SOURCES main.cpp ocl_dev_mgr.cpp ${HEADER})
ENDIF(USEIPG)
ENDIF(USEIRAPL)

# add hdf5_io as object library in order to reuse it for the tests
add_library(hdf5_io OBJECT ../include/hdf5_io.hpp hdf5_io.cpp)

# specifiy library paths for linker
IF(USENVML)
  IF(MSVC)
    link_directories(${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64)
  ELSE(MSVC)
    link_directories(${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} ${CUDA_TOOLKIT_ROOT_DIR}/lib/x86_64-linux-gnu/stubs/)
  ENDIF(MSVC)
ELSE()
    IF(USEAMDP)
        link_directories(${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} $ENV{AMDPROFILERPATH})
    ELSE()
        link_directories(${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES})
    ENDIF(USEAMDP)
ENDIF()



# set output directory for executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# generate executable
add_executable(toolkitICL ${SOURCES} $<TARGET_OBJECTS:hdf5_io>)

# link libraries
IF(USENVML)
  if(MSVC)
    TARGET_LINK_LIBRARIES(toolkitICL ${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} nvml.lib)
  ELSE()
    TARGET_LINK_LIBRARIES(toolkitICL ${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} libnvidia-ml.so)
  ENDIF()
ELSE()
  IF(USEAMDP)
 TARGET_LINK_LIBRARIES(toolkitICL ${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES} AMDPowerProfileAPI.lib) 
  ELSE(USEAMDP)
  TARGET_LINK_LIBRARIES(toolkitICL ${OpenCL_LIBRARIES} ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES}) 
  ENDIF(USEAMDP)
ENDIF()
