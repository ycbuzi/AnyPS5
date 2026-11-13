#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DISPATCH_DISPATCHAHEAD_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DISPATCH_DISPATCHAHEAD_HPP

#include <cstdint>
#include <cstdlib>

namespace AgcDriver::DriverDetail {

inline bool GroupAheadRuns(std::uint32_t queue, bool allQueues) {
    return allQueues || queue != 0;
}

inline bool GroupCaptureAllQueues() {
    static const bool enabled = std::getenv("APS5_GROUP_CAPTURE_ALL_QUEUES") != nullptr;
    return enabled;
}

inline bool GroupAheadRuns(std::uint32_t queue) {
    return GroupAheadRuns(queue, GroupCaptureAllQueues());
}

}

#endif
