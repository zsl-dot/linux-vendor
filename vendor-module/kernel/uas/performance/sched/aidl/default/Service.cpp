/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS HIDL Service
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  03/2023
 **/


#include <android/log.h>
#include <pthread.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

#include "TransSched.h"

extern "C" {
#include <unistd.h>
#include "SchedCore.h"
#include "CloudUpdate.h"
} /* extern "C" */

using android::sp;
using aidl::vendor::transsion::performance::sched::TransSched;
using std::string_literals::operator""s;

static void *thread_func(void *arg)
{
    listen_cloud_update();
    return arg;
}

int main() {
    int ret;
    pthread_t thread_id = -1;

    // Enable vndbinder to allow vendor-to-venfor binder call
    //android::ProcessState::initWithDriver("/dev/vndbinder");
    ABinderProcess_setThreadPoolMaxThreadCount(3);
    ABinderProcess_startThreadPool();
    std::shared_ptr<TransSched> sched = ndk::SharedRefBase::make<TransSched>();
    const std::string instance = TransSched::descriptor + "/default"s;

    if (sched != nullptr) {
        binder_status_t status = AServiceManager_addService(sched->asBinder().get(), instance.c_str());
        if (STATUS_OK != status) {
            ALOGE("Failed to register ITransSched service1...%d  %s",status,instance.c_str());
            return -EFAULT;
        }
    } else {
        ALOGE("Create instance of TransSched failed!");
        return -EFAULT;
    }

    set_proc_ux_tags(getpid(), UX_TASK_TAGS_UAS_SERVICE, "uas-service");
    ALOGD("set uas-service to ux, pid=%d", getpid());

    ret = pthread_create(&thread_id, nullptr, thread_func, nullptr);
    if (!ret) {
        pthread_join(thread_id, nullptr);
    }

    ALOGD("ITransSched service starts to join service pool");
    ABinderProcess_joinThreadPool();
    return 0; // should never get here
}
