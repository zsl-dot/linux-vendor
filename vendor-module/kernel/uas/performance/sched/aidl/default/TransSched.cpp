/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS UAPI
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  03/2023
 **/

#include "TransSched.h"

extern "C" {
#include <utils/Log.h>
#include <cutils/properties.h>
#include "SchedCore.h"
#include "TransSchedConfig.h"
#include "CloudUpdate.h"
} /* extern "C" */

namespace aidl {
namespace vendor {
namespace transsion {
namespace performance {
namespace sched {

void TransSched::initConfig() {

    if(copy_config_file(VENDOR_CONFIG_PATH, UPDATE_CONFIG_PATH) < 0) {
        ALOGE("copy %s to %s failed.", VENDOR_CONFIG_PATH, UPDATE_CONFIG_PATH);
        return;
    }

    if (init_sched_config(UPDATE_CONFIG_PATH) < 0) {
        ALOGE("init sched path %s failed.", UPDATE_CONFIG_PATH);
        return;
    }

    update_trans_sched_state();
    return;
}

void TransSched::initVersion() {
    char version[VERSION_STR_MAX_LEN] = "";

    if (access(TRANS_SCHED_DRV_PATH, F_OK) < 0) {
        ALOGE("trans_sched driver path %s not exist.", TRANS_SCHED_DRV_PATH);
        return;
    }

    if (get_config_version(version) < 0) {
        ALOGE("config is not inited.");
        return;
    }

    property_set(VERSION_PROPERTY, version);
    return;
}

void TransSched::destroyConfig() {
    stop_cloud_update();
    destroy_sched_config();
    return;
}

// Methods from ::vendor::transsion::performance::sched::V1_0::ITransSched follow.
ndk::ScopedAStatus TransSched::setTransSchedState(int32_t state, int32_t* _aidl_return) {
    *_aidl_return = set_trans_sched_state(state);
    ALOGD("set state=%#x %s", state, (*_aidl_return < 0) ? "failed" : "success" );
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::getTransSchedState(SchedInfo* out_info) {
    int32_t ret;
    int32_t kernel_state;

    ret = get_trans_sched_state(&kernel_state);
    out_info->ret = ret;
    out_info->value = kernel_state;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::setTransSchedScene(int32_t scene, int32_t* _aidl_return) {
    int32_t ret;

    if ((ret = check_enable()) <= 0) {
        *_aidl_return = ret;
        goto end;
    }

    *_aidl_return = set_trans_sched_scene((unsigned int)scene);
    ALOGD("set scene=%#x %s", scene, (*_aidl_return < 0) ? "failed" : "success" );

end:
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::getTransSchedScene(SchedInfo* out_info) {
    int32_t ret;
    unsigned int scene;

    ret = get_trans_sched_scene(&scene);
    out_info->ret = ret;
    out_info->value = scene;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::setTransSchedUxTagsByName(int32_t pid, const std::string& mainThread, int32_t* _aidl_return) {
    int ret;
    int ref = 0;
    int SchedState = 0;
    unsigned int uxTags = 0;
    char thread_name[THREAD_NAME_LEN] = {0};

    if ((ret = check_enable()) < 0) {
        ALOGE("get trans_sched config state error.");
        *_aidl_return = ret;
        goto end;
    }

    if (ret == 0) {
        ALOGW("trans_sched config state is disable.");
        *_aidl_return = 0;
        goto end;
    }

    if ((ret = get_trans_sched_state(&SchedState)) < 0) {
        ALOGE("get trans_sched state error.");
        *_aidl_return = ret;
        goto end;
    }

    if (SchedState == FEATURE_NONE) {
        ALOGW("trans_sched state is disable.");
        *_aidl_return = 0;
        goto end;
    }

    if ((ret = check_blacklist(mainThread.c_str())) < 0) {
        ALOGE("check blacklist failed.");
        *_aidl_return = ret;
        goto end;
    }

    if (ret) {
        ALOGD("%s is in blacklist.", mainThread.c_str());
        *_aidl_return = 0;
        goto end;
    }

    if ((ret = check_uxtaglist(mainThread.c_str(), &uxTags, &ref)) < 0) {
        ALOGE("check uxtaglist failed.");
        *_aidl_return = ret;
        goto end;
    }

    strncpy(thread_name, mainThread.c_str(), sizeof(thread_name));
    if (ret) {
        ALOGD("thread(pid=%d) %s is uxtag %#x ref %#x", pid, mainThread.c_str(), uxTags, ref);
        *_aidl_return = set_trans_sched_ux_tags(pid, uxTags, ref, thread_name);
        goto end;
    }

    if ((ret = get_default_uxtag(&uxTags)) < 0) {
        ALOGE("get default uxtag failed for thread(pid=%d) %s, set DEFAULT_UX_TAG(%#x)", pid, mainThread.c_str(), DEFAULT_UX_TAG);
        *_aidl_return = set_trans_sched_ux_tags(pid, DEFAULT_UX_TAG, UXTAG_ID_REF_NONE, thread_name);
        goto end;
    }

    ALOGD("thread(pid=%d) %s is config default uxtag %#x", pid, mainThread.c_str(), uxTags);
    *_aidl_return = set_trans_sched_ux_tags(pid, uxTags, UXTAG_ID_REF_NONE, thread_name);

end:
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::cancelTransSchedUxTagsByName(int32_t pid, const std::string& mainThread, int32_t* _aidl_return) {
    int ref = 0;
    unsigned int uxTags = 0;
    char thread_name[THREAD_NAME_LEN] = {0};

    if ((check_uxtaglist(mainThread.c_str(), &uxTags, &ref)) > 0) {
        strncpy(thread_name, mainThread.c_str(), sizeof(thread_name));
        *_aidl_return = set_trans_sched_ux_tags(pid, get_unset_ux_tags_val(uxTags), UXTAG_ID_REF_NONE, thread_name);
        goto end;
    }

    uxTags = UX_TASK_TAGS_NONE;
    *_aidl_return = set_trans_sched_ux_tags(pid, get_unset_ux_tags_val(uxTags), UXTAG_ID_REF_NONE, NULL);

end:
    ALOGD("thread(pid=%d) %s cancel uxtag %#x ret=%d", pid, mainThread.c_str(), get_unset_ux_tags_val(uxTags), *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::setTransSchedUxTags(int32_t pid, int32_t uxTags, int32_t* _aidl_return) {
    *_aidl_return = set_trans_sched_ux_tags(pid, uxTags, UXTAG_ID_REF_NONE, NULL);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::cancelTransSchedUxTags(int32_t pid, int32_t* _aidl_return) {
    ALOGD("thread(pid=%d)'s uxtag will be cancelled", pid);
    *_aidl_return = set_trans_sched_ux_tags(pid, UX_TASK_TAGS_NONE, UXTAG_ID_REF_NONE, NULL);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::setTransSchedUxPrio(int32_t pid, int32_t shift, int32_t* _aidl_return) {
    *_aidl_return = set_trans_sched_ux_prio(pid, shift);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::getTransSchedUxTags(int32_t pid, SchedInfo* out_info) {
    int32_t ret;
    uint32_t uxTags;

    ret = get_trans_sched_ux_tags(pid, &uxTags);
    out_info->ret = ret;
    out_info->value = uxTags;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::getTransSchedUxInfo(int32_t pid, SchedInfo* out_info) {
    int32_t ret;
    char buffer[256] = {0};

    ret = get_trans_sched_ux_info(pid, buffer, sizeof(buffer));
    out_info->ret = ret;
    out_info->info = buffer;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::dumpTransSchedUxInfo(int32_t* _aidl_return) {
    *_aidl_return = dump_trans_sched_ux_info();

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::getCloudState(int32_t* _aidl_return) {
    *_aidl_return = check_enable();

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TransSched::dumpConfig() {
    dump_sched_config();

    return ndk::ScopedAStatus::ok();
}

}
}
}
}
}

