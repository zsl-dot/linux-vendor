/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS HIDL Service
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  03/2023
 **/


#include <android/log.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <pthread.h>

#include "TransSched.h"

extern "C" {
#include <unistd.h>
#include "SchedCore.h"
#include "CloudUpdate.h"
} /* extern "C" */

using android::sp;
using android::hardware::joinRpcThreadpool;
using android::hardware::configureRpcThreadpool;
using vendor::transsion::performance::sched::V1_0::ITransSched;
using vendor::transsion::performance::sched::implementation::TransSched;

static void *thread_func(void *arg)
{
    listen_cloud_update();
    return arg;
}

int main() {
    int ret;
    pthread_t thread_id = -1;
    android::sp<ITransSched> sched = nullptr;

    sched = new TransSched();
    if (sched != nullptr) {
        configureRpcThreadpool(2, false /*callerWillJoin*/);
        ret = sched->registerAsService();
        if (::android::OK != ret) {
            return ret;
        }
    }
    else {
        ALOGE("Create instance of TransSched failed!");
        return -EFAULT;
    }

    usleep(100000);
    set_proc_ux_tags(getpid(), UX_TASK_TAGS_EPROMOTE1, "uas-service");

    ret = pthread_create(&thread_id, nullptr, thread_func, nullptr);
    if (!ret) {
        pthread_join(thread_id, nullptr);
    }

    ALOGD("uas-service is ready!");
    joinRpcThreadpool();
    return 0; // should never get here
}
