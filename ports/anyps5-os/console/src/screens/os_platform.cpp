#include "screens/settings.h"

namespace ui::sett {

static json platformInfo;
static bool platformAsked = false;

void platformRows(Rows& R) {
    if (!platformAsked) {
        platformAsked = true;
        sys("platform", "info", {}, [](const json& r) {
            if (jbool(r, "ok") && r.contains("data") && r["data"].is_object()) platformInfo = r["data"];
        });
    }
    if (!platformInfo.is_object()) return;
    std::string os = jstr(platformInfo, "os"), machine = jstr(platformInfo, "machine"), kernel = jstr(platformInfo, "kernel");
    if (!os.empty()) R.info("Operating System", os);
    if (!kernel.empty()) R.info("Kernel", kernel);
    if (!machine.empty()) R.info("Machine Name", machine).secret = true;
    if (jbool(platformInfo, "testMode")) R.info("Test Mode", "On");
}

}
