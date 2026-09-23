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

#include <binder/IServiceManager.h>
#include <binder/Status.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "TransSched.h"

using aidl::vendor::transsion::performance::sched::ITransSched;
using aidl::vendor::transsion::performance::sched::SchedInfo;
using ::ndk::SpAIBinder;
static std::mutex mMutex;
static std::shared_ptr<ITransSched> schedService = nullptr;

static void serviceDied(void* cookie) {
    ALOGE("UAS Aidl Server is death!");

    std::lock_guard lock(mMutex);
    schedService = nullptr;
}

// Global death recipient object
static AIBinder_DeathRecipient* deathRecipicent = nullptr;

static std::shared_ptr<ITransSched> schedServiceInit() {
    std::lock_guard lock(mMutex);
    if (schedService != nullptr) {
        return schedService;
    }

    const char * descriptor = ITransSched::descriptor;
    const std::string kInstance = std::string() + descriptor + "/default";
    // Use non-blocking method to check if the service is available
    AIBinder* binder = AServiceManager_checkService(kInstance.c_str());
    if (binder == nullptr) {
        ALOGE("try get uas aidl service failed!");
        return nullptr;
    }

    // Initialize global death recipient if not already initialized
    if (deathRecipicent == nullptr) {
        deathRecipicent = AIBinder_DeathRecipient_new(serviceDied);
        if (deathRecipicent == nullptr) {
            ALOGE("Failed to create death recipient\n");
            return nullptr;
        }
    }

    binder_status_t status = AIBinder_linkToDeath(binder, deathRecipicent, nullptr);
    if (status != STATUS_OK) {
        ALOGE("uas aidl failed to linkToDeath");
        return nullptr;
    }

    schedService = ITransSched::fromBinder(SpAIBinder(binder));
    if (schedService == nullptr) {
        ALOGE("try get uas aidl service failed!");
    }

    return schedService;
}

int32_t setTransSchedScene(int32_t scene) {
    int32_t ret;
    std::shared_ptr<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    ::ndk::ScopedAStatus status = sched->setTransSchedScene(scene, &ret);
    if (!status.isOk()) {
        ALOGE("set ux scene failed, scene=%#x, des=%s!\n", scene, status.getDescription().c_str());
        return -1;
    }

    return ret;
}

int32_t getTransSchedScene(uint32_t *scene) {
    int32_t result = -EPERM;
    SchedInfo info;
    std::shared_ptr<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    ::ndk::ScopedAStatus status = sched->getTransSchedScene(&info);
    if (!status.isOk()) {
        ALOGE("get ux scene failed, des=%s!\n", status.getDescription().c_str());
        return -EIO; // Return an error code similar to HIDL's `-EIO`
    }

    result = info.ret;
    if(!result && scene) {
        *scene = info.value;
    }

    return result;
}

int32_t setTransSchedUxTagsByName(int32_t pid, const std::string& mainThread) {
    int32_t ret;
    std::shared_ptr<ITransSched> sched = nullptr;

    if (strncmp(mainThread.c_str(), UX_EXACT_HEAD, UX_EXACT_HEAD_LEN)) {
        return -EINVAL;
    }

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    ::ndk::ScopedAStatus status = sched->setTransSchedUxTagsByName(pid, mainThread, &ret);
    if (!status.isOk()) {
        ALOGE("set ux_tags by name failed, pid=%d, ThreadID=%s, des=%s!\n", pid, mainThread.c_str(), status.getDescription().c_str());
        return -1;
    }

    return ret;
}

int32_t cancelTransSchedUxTags(int32_t pid) {
    int32_t ret;
    std::shared_ptr<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    ::ndk::ScopedAStatus status = sched->cancelTransSchedUxTags(pid, &ret);
    if (!status.isOk()) {
        ALOGE("cancel ux_tags failed, pid=%d, des=%s!\n", pid, status.getDescription().c_str());
    }

    return ret;
}

int32_t getTransSchedUxTags(int32_t pid, uint32_t *uxTags) {
    int32_t result = -EPERM;
    SchedInfo info;
    std::shared_ptr<ITransSched> sched = nullptr;

    sched = schedServiceInit();
    if (sched == nullptr) {
        return -EBADR;
    }

    ::ndk::ScopedAStatus status = sched->getTransSchedUxTags(pid, &info);
    if (!status.isOk()) {
        ALOGE("get ux_tags failed, des=%s!\n", status.getDescription().c_str());
        return -EIO; // Return an error code similar to HIDL's `-EIO
    }
    result = info.ret;
    if (!result && uxTags) {
        *uxTags = info.value;
    }

    return result;
}
