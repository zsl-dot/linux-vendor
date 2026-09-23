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

namespace vendor::transsion::performance::sched::implementation {


Return<void> TransSched::initConfig() {
    if(copy_config_file(VENDOR_CONFIG_PATH, UPDATE_CONFIG_PATH) < 0) {
        ALOGE("copy %s to %s failed.", VENDOR_CONFIG_PATH, UPDATE_CONFIG_PATH);
        return Void();
    }
    if (init_sched_config(UPDATE_CONFIG_PATH) < 0) {
        ALOGE("init sched path %s failed.", UPDATE_CONFIG_PATH);
        return Void();
    }
    update_trans_sched_state();

    return Void();
}

Return<void> TransSched::initVersion() {
    char version[VERSION_STR_MAX_LEN] = "";

    if (access(TRANS_SCHED_DRV_PATH, F_OK) < 0) {
        ALOGE("trans_sched driver path %s not exist.", TRANS_SCHED_DRV_PATH);
        return Void();
    }

    if (get_config_version(version) < 0) {
        ALOGE("config is not inited.");
        return Void();
    }

    property_set(VERSION_PROPERTY, version);
    return Void();
}


Return<void> TransSched::destroyConfig() {
    stop_cloud_update();
    destroy_sched_config();
    return Void();
}

// Methods from ::vendor::transsion::performance::sched::V1_0::ITransSched follow.
Return<int32_t> TransSched::setTransSchedState(int32_t state) {
    return set_trans_sched_state(state);
}

Return<void> TransSched::getTransSchedState(getTransSchedState_cb _hidl_cb) {
    int32_t ret;
    int32_t kernel_state;

    ret = get_trans_sched_state(&kernel_state);
    _hidl_cb(ret, kernel_state);
    return Void();
}

Return<int32_t> TransSched::setTransSchedScene(int32_t scene) {
    int ret;

    ret = set_trans_sched_scene((unsigned int)scene);
    ALOGD("set scene=%#x %s", scene, (ret < 0) ? "failed" : "success" );

    return ret;
}

Return<void> TransSched::getTransSchedScene(getTransSchedScene_cb _hidl_cb) {
    int32_t ret;
    unsigned int scene;

    ret = get_trans_sched_scene(&scene);
    _hidl_cb(ret, scene);

    return Void();
}

Return<int32_t> TransSched::setTransSchedUxTagsByName(int32_t pid, const hidl_string& mainThread) {
    int ret;
    int ref = 0;
    int SchedState = 0;
    unsigned int uxTags = 0;
    char thread_name[THREAD_NAME_LEN] = {0};

    if ((ret = check_enable()) < 0) {
        ALOGE("get trans_sched config state error.");
        return ret;
    }

    if (ret == 0) {
        ALOGW("trans_sched config state is disable.");
        return 0;
    }

    if ((ret = get_trans_sched_state(&SchedState)) < 0) {
        ALOGE("get trans_sched state error.");
        return ret;
    }

    if (SchedState == FEATURE_NONE) {
        ALOGW("trans_sched state is disable.");
        return 0;
    }

    if ((ret = check_blacklist(mainThread.c_str())) < 0) {
        ALOGE("check blacklist failed.");
        return ret;
    }

    if (ret) {
        ALOGD("%s is in blacklist.", mainThread.c_str());
        return 0;
    }

    if ((ret = check_uxtaglist(mainThread.c_str(), &uxTags, &ref)) < 0) {
        ALOGE("check uxtaglist failed.");
        return ret;
    }

    strncpy(thread_name, mainThread.c_str(), sizeof(thread_name));

    if (ret) {
        ALOGD("thread(pid=%d) %s is uxtag %#x ref %#x", pid, mainThread.c_str(), uxTags, ref);
        return set_trans_sched_ux_tags(pid, uxTags, ref, thread_name);
    }

    if ((ret = get_default_uxtag(&uxTags)) < 0) {
        ALOGE("get default uxtag failed for thread(pid=%d) %s, set DEFAULT_UX_TAG(%#x)", pid, mainThread.c_str(), DEFAULT_UX_TAG);
        return set_trans_sched_ux_tags(pid, DEFAULT_UX_TAG, UXTAG_ID_REF_NONE, thread_name);
    }

    ALOGD("thread(pid=%d) %s is config default uxtag %#x", pid, mainThread.c_str(), uxTags);
    return set_trans_sched_ux_tags(pid, uxTags, UXTAG_ID_REF_NONE, thread_name);
}

Return<int32_t> TransSched::setTransSchedUxTags(int32_t pid, uint32_t uxTags) {
    return set_trans_sched_ux_tags(pid, uxTags, UXTAG_ID_REF_NONE, NULL);
}

Return<int32_t> TransSched::cancelTransSchedUxTags(int32_t pid) {
    ALOGD("thread(pid=%d)'s uxtag will be cancelled", pid);
    return set_trans_sched_ux_tags(pid, UX_TASK_TAGS_NONE, UXTAG_ID_REF_NONE, NULL);
}

Return<int32_t> TransSched::setTransSchedUxPrio(int32_t pid, uint32_t shift) {
    return set_trans_sched_ux_prio(pid, shift);
}

Return<void> TransSched::getTransSchedUxTags(int32_t pid, getTransSchedUxTags_cb _hidl_cb) {
    int32_t ret;
    uint32_t uxTags;

    ret = get_trans_sched_ux_tags(pid, &uxTags);
    _hidl_cb(ret, uxTags);

    return Void();
}

Return<void> TransSched::getTransSchedUxInfo(int32_t pid, getTransSchedUxInfo_cb _hidl_cb) {
    int32_t ret;
    char buffer[256] = {0};

    ret = get_trans_sched_ux_info(pid, buffer, sizeof(buffer));
    hidl_string info(buffer);
    _hidl_cb(ret, info);

    return Void();
}

Return<int32_t> TransSched::dumpTransSchedUxInfo() {
    return dump_trans_sched_ux_info();
}


Return<int32_t> TransSched::getCloudState() {
    return check_enable();
}

Return<void> TransSched::dumpConfig() {
    dump_sched_config();
    return Void();
}

}  // namespace vendor::transsion::performance::sched::implementation
