add_library(libSceSigninDialog SHARED EXCLUDE_FROM_ALL Export.cpp)
target_include_directories(libSceSigninDialog PRIVATE ${LIBS_INCLUDE_DIR})
configure_windows_unwind(libSceSigninDialog)

if(MINGW)
    foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
        string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    endforeach()
endif()

# guest_signin_dialog_tests is registered in core/libs/CMakeLists.txt, which is where
# upstream keeps it. Declaring it here as well created the target twice (CMP0002) once the
# two sides met: neither file conflicted, so the merge simply kept both registrations.
