/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Client
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  06/2024
 **/

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <unistd.h>
#include <android/log.h>
#include <hidl/HidlSupport.h>

#include "TransSched.h"


using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_death_recipient;
using vendor::transsion::performance::sched::V1_0::ITransSched;

static std::mutex mMutex;
static android::sp<ITransSched> schedService = nullptr;

class UasDeathRecipient: public hidl_death_recipient {

public:
    void serviceDied(uint64_t /*cookie*/, const ::android::wp<::android::hidl::base::V1_0::IBase> & /*who*/) {
        ALOGE("UAS Hidl Server is death!");

        std::lock_guard lock(mMutex);
        schedService = nullptr;
    }
};

static sp<UasDeathRecipient> deathRecipicent = nullptr;


static android::sp<ITransSched> schedServiceInit() {
    std::lock_guard lock(mMutex);
    if (schedService != nullptr) {
        goto out;
    }

    schedService = ITransSched::tryGetService();
    if (schedService != nullptr) {
        deathRecipicent = new UasDeathRecipient();
        Return<bool> linked = schedService->linkToDeath(deathRecipicent, 0);
        if (!linked.isOk() || !linked) {
            schedService = nullptr;
            ALOGE("uas hidl failed to linkToDeath");
        }
    }

    if (schedService == nullptr) {
        ALOGE("try get uas hidl service failed!");
    }

out:
    return schedService;
}

Return<int32_t> setTransSchedScene(int32_t scene) {
    android::sp<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    Return<int32_t> ret = sched->setTransSchedScene(scene);
    if (!ret.isOk()) {
        ALOGE("set ux scene failed, scene=%#x, des=%s!\n", scene, ret.description().c_str());
    }

    return ret.withDefault(-1);
}

Return<int32_t> getTransSchedScene(uint32_t *scene) {
    int32_t result = -EPERM;
    android::sp<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    Return<void> ret = sched->getTransSchedScene([&](int32_t ret_val, int32_t value) {
        result = ret_val;

        if (!result && scene) {
            *scene = value;
        }
    });

    if (!ret.isOk()) {
        ALOGE("get ux scene failed, des=%s!\n", ret.description().c_str());
        result = -EIO;
    }

    return result;
}

Return<int32_t> setTransSchedUxTagsByName(int32_t pid, const hidl_string& mainThread) {
    android::sp<ITransSched> sched = nullptr;

    if (strncmp(mainThread.c_str(), UX_EXACT_HEAD, UX_EXACT_HEAD_LEN)) {
        return -EINVAL;
    }

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    Return<int32_t> ret = sched->setTransSchedUxTagsByName(pid, mainThread);
    if (!ret.isOk()) {
        ALOGE("set ux_tags by name failed, pid=%d, threadID=%s, des=%s!\n", pid, mainThread.c_str(), ret.description().c_str());
    }

    return ret.withDefault(-1);
}

Return<int32_t> cancelTransSchedUxTags(int32_t pid) {
    android::sp<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    Return<int32_t> ret = sched->cancelTransSchedUxTags(pid);
    if (!ret.isOk()) {
        ALOGE("cancel ux_tags failed, pid=%d, des=%s!\n", pid, ret.description().c_str());
    }

    return ret.withDefault(-1);
}

Return<int32_t> getTransSchedUxTags(int32_t pid, uint32_t *uxTags) {
    int32_t result = -EPERM;
    android::sp<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    Return<void> ret = sched->getTransSchedUxTags(pid, [&](int32_t ret_val, uint32_t ux_tags) {
        result = ret_val;

        if (!result && uxTags) {
            *uxTags = ux_tags;
        }
    });

    if (!ret.isOk()) {
        ALOGE("get ux_tags failed, des=%s!\n", ret.description().c_str());
        result = -EIO;
    }

    return result;
}
