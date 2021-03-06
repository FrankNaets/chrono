# ------------------------------------------------------------------------------
# Additional include paths and libraries
# ------------------------------------------------------------------------------

INCLUDE_DIRECTORIES(${CH_PARALLEL_INCLUDES})

SET(LIBRARIES
    ChronoEngine   
    ChronoEngine_fsi
)
IF(ENABLE_MODULE_PARALLEL)
SET(LIBRARIES ${LIBRARIES} 
    ChronoEngine_parallel
)
ENDIF()
IF(ENABLE_MODULE_VEHICLE)
SET(LIBRARIES ${LIBRARIES}
    ChronoEngine_vehicle   
)
ENDIF()
IF(ENABLE_MODULE_OPENGL)
    INCLUDE_DIRECTORIES(${CH_OPENGL_INCLUDES})
    SET(LIBRARIES ${LIBRARIES} ChronoEngine_opengl)
ENDIF()

# ------------------------------------------------------------------------------
# List of all executables
# ------------------------------------------------------------------------------

# List all FSI demos that use chrono
SET(FSI_DEMOS
# add fluid demos here
)

# List all FSI demos that use chrono-parallel
SET(FSI_PARALLEL_DEMOS
# add fluid demos here
)

# List all FSI demos use vehicle
SET(FSI_VEHICLE_DEMOS
# add fluid demos here
)

# List all FSI demos use parallel and vehicle
SET(FSI_PARALLEL_VEHICLE_DEMOS
# add fluid demos here
)

#--------------------------------------------------------------
# Add executables for demos that have no other dependencies
IF(ENABLE_MODULE_PARALLEL)
	IF(ENABLE_MODULE_VEHICLE)
		FOREACH(PROGRAM ${FSI_PARALLEL_VEHICLE_DEMOS})
		    MESSAGE(STATUS "...add ${PROGRAM}")

		    CUDA_ADD_EXECUTABLE(${PROGRAM}  "${PROGRAM}.cpp")
		    SOURCE_GROUP(""  FILES "${PROGRAM}.cpp")

		    SET_TARGET_PROPERTIES(${PROGRAM} PROPERTIES
			FOLDER demos
			COMPILE_FLAGS "${CH_CXX_FLAGS} ${CH_PARALLEL_CXX_FLAGS}"
			LINK_FLAGS "${CH_LINKERFLAG_EXE}"
		    )

		    TARGET_LINK_LIBRARIES(${PROGRAM} ${LIBRARIES})
		    ADD_DEPENDENCIES(${PROGRAM} ${LIBRARIES})

		    INSTALL(TARGETS ${PROGRAM} DESTINATION bin)

		ENDFOREACH(PROGRAM)
	ELSE()
		FOREACH(PROGRAM ${FSI_PARALLEL_DEMOS})
		    MESSAGE(STATUS "...add ${PROGRAM}")

		    CUDA_ADD_EXECUTABLE(${PROGRAM}  "${PROGRAM}.cpp")
		    SOURCE_GROUP(""  FILES "${PROGRAM}.cpp")

		    SET_TARGET_PROPERTIES(${PROGRAM} PROPERTIES
			FOLDER demos
			COMPILE_FLAGS "${CH_CXX_FLAGS} ${CH_PARALLEL_CXX_FLAGS}"
			LINK_FLAGS "${CH_LINKERFLAG_EXE}"
		    )

		    TARGET_LINK_LIBRARIES(${PROGRAM} ${LIBRARIES})
		    ADD_DEPENDENCIES(${PROGRAM} ${LIBRARIES})

		    INSTALL(TARGETS ${PROGRAM} DESTINATION bin)

		ENDFOREACH(PROGRAM)
	ENDIF()
ELSE()
	IF(ENABLE_MODULE_VEHICLE)
		FOREACH(PROGRAM ${FSI_VEHICLE_DEMOS})
		    MESSAGE(STATUS "...add ${PROGRAM}")

		    CUDA_ADD_EXECUTABLE(${PROGRAM}  "${PROGRAM}.cpp")
		    SOURCE_GROUP(""  FILES "${PROGRAM}.cpp")

		    SET_TARGET_PROPERTIES(${PROGRAM} PROPERTIES
			FOLDER demos
			COMPILE_FLAGS "${CH_CXX_FLAGS} ${CH_PARALLEL_CXX_FLAGS}"
			LINK_FLAGS "${CH_LINKERFLAG_EXE}"
		    )

		    TARGET_LINK_LIBRARIES(${PROGRAM} ${LIBRARIES})
		    ADD_DEPENDENCIES(${PROGRAM} ${LIBRARIES})

		    INSTALL(TARGETS ${PROGRAM} DESTINATION bin)

		ENDFOREACH(PROGRAM)
	ELSE()
		FOREACH(PROGRAM ${FSI_DEMOS})
		    MESSAGE(STATUS "...add ${PROGRAM}")

		    CUDA_ADD_EXECUTABLE(${PROGRAM}  "${PROGRAM}.cpp")
		    SOURCE_GROUP(""  FILES "${PROGRAM}.cpp")

		    SET_TARGET_PROPERTIES(${PROGRAM} PROPERTIES
			FOLDER demos
			COMPILE_FLAGS "${CH_CXX_FLAGS} ${CH_PARALLEL_CXX_FLAGS}"
			LINK_FLAGS "${CH_LINKERFLAG_EXE}"
		    )

		    TARGET_LINK_LIBRARIES(${PROGRAM} ${LIBRARIES})
		    ADD_DEPENDENCIES(${PROGRAM} ${LIBRARIES})

		    INSTALL(TARGETS ${PROGRAM} DESTINATION bin)

		ENDFOREACH(PROGRAM)
	ENDIF()
ENDIF()




