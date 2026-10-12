foreach(required IN ITEMS FIXTURE FRAME_HASH MANIFEST OUTPUT_ROOT PYTHON)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef runId)
set(runDirectory "${OUTPUT_ROOT}/frame-reference-${runId}")
file(MAKE_DIRECTORY "${runDirectory}")
execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "APS5_DUMP_FRAMES=1" "${FIXTURE}" --verify
        WORKING_DIRECTORY "${runDirectory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 120
)
file(WRITE "${runDirectory}/fixture.log" "${output}\n${error}")
if(NOT result STREQUAL "0" AND NOT DEFINED ENV{ANYPS5_REQUIRE_VULKAN})
    if(result STREQUAL "77" OR "${output}${error}" MATCHES "no usable Vulkan device|No available video device|SDL_Init|vkCreateInstance|CreateWindow")
        message(STATUS "frame reference run: skipped, no usable Vulkan device or display")
        file(REMOVE_RECURSE "${runDirectory}")
        return()
    endif()
endif()
if(NOT result STREQUAL "0" OR NOT output MATCHES "GPU readback verified")
    message(FATAL_ERROR "Visual fixture failed: ${result}\n${output}\n${error}")
endif()
if(RECORD)
    execute_process(
            COMMAND "${PYTHON}" "${FRAME_HASH}" record "${runDirectory}" --manifest "${MANIFEST}" --device "${RECORD_DEVICE}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Recording the frame manifest failed: ${result}\n${output}\n${error}")
    endif()
    message(STATUS "${output}")
    file(REMOVE_RECURSE "${runDirectory}")
    return()
endif()
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "No frame manifest at ${MANIFEST}; record one with -DRECORD=ON")
endif()
execute_process(
        COMMAND "${PYTHON}" "${FRAME_HASH}" verify "${runDirectory}" --manifest "${MANIFEST}" --tolerance "${TOLERANCE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60
)
file(WRITE "${runDirectory}/verify.log" "${output}\n${error}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Presented frames differ from ${MANIFEST}:\n${output}\n${error}\nFrames kept in ${runDirectory}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${runDirectory}")
