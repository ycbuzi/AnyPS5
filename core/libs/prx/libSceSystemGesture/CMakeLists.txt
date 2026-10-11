add_library(libSceSystemGesture SHARED EXCLUDE_FROM_ALL Export.cpp)
target_include_directories(libSceSystemGesture PRIVATE ${LIBS_INCLUDE_DIR})
configure_windows_unwind(libSceSystemGesture)

if(MINGW)
    foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
        string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    endforeach()
endif()

