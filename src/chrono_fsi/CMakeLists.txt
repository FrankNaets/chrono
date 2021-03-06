#=============================================================================
# CMake configuration file for Chrono FSI module
#
# Cannot be used stand-alone (it is loaded by parent CMake configuration file)
#=============================================================================

option(ENABLE_MODULE_FSI "Enable the Chrono FSI module" OFF)

# Return now if this module is not enabled
#
# TODO: REMOVE CHRONO_VEHICLE & CHRONO_PARALLEL AS REQUIREMENTS!!!!!
#
if(NOT ENABLE_MODULE_FSI OR NOT ENABLE_MODULE_PARALLEL OR NOT ENABLE_MODULE_VEHICLE)
  mark_as_advanced(FORCE CUDA_TOOLKIT_ROOT_DIR)
  mark_as_advanced(FORCE CUDA_USE_STATIC_CUDA_RUNTIME)
  return()
endif()

message(STATUS "==== Chrono FSI module ====")

mark_as_advanced(CLEAR CUDA_TOOLKIT_ROOT_DIR)
mark_as_advanced(CLEAR CUDA_USE_STATIC_CUDA_RUNTIME)

# ----------------------------------------------------------------------------
# CUDA STUFF , Arman Take care of this
# ----------------------------------------------------------------------------

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

find_package(CUDA)

SET(CHRONO_INCLUDE_DIRS 
	${CHRONO_INCLUDE_DIRS} 
	"${CUDA_TOOLKIT_ROOT_DIR}/include"
    	"${CUDA_SDK_ROOT_DIR}/common/inc")

#SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -std=c++11")
#SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} --device-c")
SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -gencode arch=compute_20,code=sm_20")
#SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} ---gpu-code=sm_20")

option(CUDA_PROPAGATE_HOST_FLAGS "set host flags off" FALSE)


# ----------------------------------------------------------------------------
# Generate and install configuration file
# ----------------------------------------------------------------------------

# Generate the configuration header file using substitution variables.

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ChConfigFSI.h.in
               ${PROJECT_BINARY_DIR}/chrono_fsi/ChConfigFSI.h)

install(FILES "${PROJECT_BINARY_DIR}/chrono_fsi/ChConfigFSI.h"
        DESTINATION include/chrono_fsi)

#-----------------------------------------------------------------------------
# LIST THE FILES THAT MAKE THE FSI FLUID-SOLID INTERACTION LIBRARY

SET(ChronoEngine_FSI_SOURCES
ChBce.cu
ChCollisionSystemFsi.cu
ChDeviceUtils.cu
ChFluidDynamics.cu
ChFsiDataManager.cu
ChFsiForceParallel.cu
ChFsiGeneral.cu
ChFsiInterface.cpp
ChSystemFsi.cpp
ChFsiTypeConvert.cpp
UtilsFsi/ChUtilsGeneratorBce.cpp
UtilsFsi/ChUtilsGeneratorFluid.cpp
UtilsFsi/ChUtilsGeneratorFsi.cpp
UtilsFsi/ChUtilsPrintSph.cu
)

SET(ChronoEngine_FSI_HEADERS
ChBce.cuh
ChCollisionSystemFsi.cuh
ChDeviceUtils.cuh
ChFluidDynamics.cuh
ChFsiDataManager.cuh
ChFsiForceParallel.cuh
ChFsiGeneral.cuh
ChFsiInterface.h
ChSystemFsi.h
ChSphGeneral.cuh
ChApiFsi.h
include/utils.h
ChFsiTypeConvert.h
UtilsFsi/ChUtilsGeneratorBce.h
UtilsFsi/ChUtilsGeneratorFluid.h
UtilsFsi/ChUtilsGeneratorFsi.h
UtilsFsi/ChUtilsPrintSph.cuh
)

SOURCE_GROUP("" FILES 
    ${ChronoEngine_FSI_SOURCES} 
    ${ChronoEngine_FSI_HEADERS})

#-----------------------------------------------------------------------------	
# Create the ChronoEngine_fsi library
#-----------------------------------------------------------------------------	

set(CXX_FLAGS ${CH_CXX_FLAGS})
set(LIBRARIES "ChronoEngine")

if(ENABLE_MODULE_OPENGL)
    include_directories(${CH_OPENGL_INCLUDES})
    list(APPEND LIBRARIES ChronoEngine_opengl)
ENDIF()

if(ENABLE_MODULE_PARALLEL)
	set(CXX_FLAGS ${CH_PARALLEL_CXX_FLAGS})
	include_directories(${CH_PARALLEL_INCLUDES})
	list(APPEND LIBRARIES ChronoEngine_parallel)
endif()

if(ENABLE_MODULE_VEHICLE)
	include_directories(${CH_VEHICLE_INCLUDES})
	list(APPEND LIBRARIES ChronoEngine_vehicle)
endif()

CUDA_ADD_LIBRARY(ChronoEngine_fsi SHARED 
    ${ChronoEngine_FSI_SOURCES}
    ${ChronoEngine_FSI_HEADERS})

SET_TARGET_PROPERTIES(ChronoEngine_fsi PROPERTIES
                      COMPILE_FLAGS "${CXX_FLAGS}"
                      LINK_FLAGS "${CH_LINKERFLAG_SHARED}" 
                      COMPILE_DEFINITIONS "CH_API_COMPILE_FSI")
                          
TARGET_LINK_LIBRARIES(ChronoEngine_fsi ${LIBRARIES})

INSTALL(TARGETS ChronoEngine_fsi
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

INSTALL(FILES ${ChronoEngine_FSI_HEADERS}
        DESTINATION include/chrono_fsi)
