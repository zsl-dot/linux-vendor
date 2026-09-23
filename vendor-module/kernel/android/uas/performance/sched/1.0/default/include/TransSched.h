#pragma once

#include <vendor/transsion/performance/sched/1.0/ITransSched.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#define UX_EXACT_HEAD      "ux_exact"
#define UX_EXACT_HEAD_LEN  (8)
#define UX_EXACT_TEST      "ux_exact_test"

namespace vendor::transsion::performance::sched::implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct TransSched : public V1_0::ITransSched {
    // Methods from ::vendor::transsion::performance::sched::V1_0::ITransSched follow.

    TransSched() {
        initConfig();
        initVersion();
    }

    ~TransSched() {
        destroyConfig();
    }
    Return<int32_t> setTransSchedState(int32_t state) override;
    Return<void> getTransSchedState(getTransSchedState_cb _hidl_cb) override;
    Return<int32_t> setTransSchedScene(int32_t scene) override;
    Return<void> getTransSchedScene(getTransSchedScene_cb _hidl_cb) override;
    Return<int32_t> setTransSchedUxTagsByName(int32_t pid, const hidl_string& mainThread) override;
    Return<int32_t> setTransSchedUxTags(int32_t pid, uint32_t uxTags) override;
    Return<int32_t> cancelTransSchedUxTags(int32_t pid) override;
    Return<int32_t> setTransSchedUxPrio(int32_t pid, uint32_t shift) override;
    Return<void> getTransSchedUxTags(int32_t pid, getTransSchedUxTags_cb _hidl_cb) override;
    Return<void> getTransSchedUxInfo(int32_t pid, getTransSchedUxInfo_cb _hidl_cb) override;
    Return<int32_t> dumpTransSchedUxInfo() override;
    virtual Return<void> initConfig();
    virtual Return<void> initVersion();
    virtual Return<void> destroyConfig();
    Return<int32_t> getCloudState() override;
    Return<void> dumpConfig() override;
};

}  /* namespace vendor::transsion::performance::sched::implementation */