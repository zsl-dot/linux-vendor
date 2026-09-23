#pragma once

#include <aidl/vendor/transsion/performance/sched/BnTransSched.h>

#define UX_EXACT_HEAD      "ux_exact"
#define UX_EXACT_HEAD_LEN  (8)
#define UX_EXACT_TEST      "ux_exact_test"

namespace aidl {
namespace vendor {
namespace transsion {
namespace performance {
namespace sched {

class TransSched : public BnTransSched {
public:
    TransSched() {
        initConfig();
        initVersion();
    }

    ~TransSched() {
        destroyConfig();
    }
    ndk::ScopedAStatus cancelTransSchedUxTags(int32_t in_pid, int32_t* _aidl_return);
    ndk::ScopedAStatus dumpConfig();
    ndk::ScopedAStatus dumpTransSchedUxInfo(int32_t* _aidl_return);
    ndk::ScopedAStatus getCloudState(int32_t* _aidl_return);
    ndk::ScopedAStatus getTransSchedScene(SchedInfo* out_info);
    ndk::ScopedAStatus getTransSchedState(SchedInfo* out_info);
    ndk::ScopedAStatus getTransSchedUxInfo(int32_t in_pid, SchedInfo* out_info);
    ndk::ScopedAStatus getTransSchedUxTags(int32_t in_pid, SchedInfo* out_info);
    ndk::ScopedAStatus setTransSchedScene(int32_t in_scene, int32_t* _aidl_return);
    ndk::ScopedAStatus setTransSchedState(int32_t in_state, int32_t* _aidl_return);
    ndk::ScopedAStatus setTransSchedUxTags(int32_t in_pid, int32_t in_uxTags, int32_t* _aidl_return);
    ndk::ScopedAStatus setTransSchedUxTagsByName(int32_t in_pid, const std::string& in_mainThread, int32_t* _aidl_return);
    ndk::ScopedAStatus setTransSchedUxPrio(int32_t in_pid, int32_t shift, int32_t* _aidl_return);
    ndk::ScopedAStatus cancelTransSchedUxTagsByName(int32_t in_pid, const std::string& in_mainThread, int32_t* _aidl_return);
    void initConfig();
    void initVersion();
    void destroyConfig();
};

}
}
}
}
}
