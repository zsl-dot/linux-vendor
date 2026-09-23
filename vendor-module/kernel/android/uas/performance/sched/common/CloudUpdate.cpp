/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Cloud Control
 * @author xianhe.zhou@transsion.com
 * @version 1.0,  03/2023
 **/

extern "C" {
#include<tranlog/libtranlog.h>
#include<utils/Log.h>
#include<sys/stat.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<pthread.h>
#include<errno.h>

#include"CloudUpdate.h"
#include"TransSchedConfig.h"
#include"SchedCore.h"


static int g_is_listen = 0;

static void config_update(char* key)
{
    if (strcmp(CLOUD_ID, key)) {
        ALOGD("current key is %s not match", key);
        return;
    }
    if (access(CLOUD_CONFIG_PATH, F_OK)) {
        ALOGW("%s is not exist, please pull cloud config.", CLOUD_CONFIG_PATH);
        return;
    }

    if (update_sched_config(CLOUD_CONFIG_PATH) < 0) {
        ALOGE("cloud update config failed.");
        goto DUMP;
    }
    if (copy_config_file(CLOUD_CONFIG_PATH, UPDATE_CONFIG_PATH) < 0) {
        ALOGE("cloud config copy %s to %s failed.", CLOUD_CONFIG_PATH, UPDATE_CONFIG_PATH);
    }

DUMP:
    dump_sched_config();
    update_trans_sched_state();
}

static struct config_notify trans_sched_config_notify = {
    .notify = config_update
};

void update_trans_sched_state(void)
{
    int curr_state;
    int new_state;
    if ((new_state = check_enable()) < 0) {
        ALOGE("check enable failed.");
        return;
    }

    if (get_trans_sched_state(&curr_state) < 0) {
        ALOGE("get trans_sched current state error, set config enable %d.", new_state);
        set_trans_sched_state(new_state);
        return;
    }

    ALOGD("curr state: %d, new state: %d", curr_state, new_state);
    if (curr_state ^ new_state) {
        set_trans_sched_state(new_state);
    }
}

int listen_cloud_update(void)
{
    int re_count = RETRY_COUNT;
    char ver[VERSION_STR_MAX_LEN];
    int ret;
    if ((ret = get_config_version(ver)) < 0) {
        ALOGE("config is not inited.");
        return ret;
    }

    do {
        if (isReady() != 1) {
            ALOGD("cloud engine is not ready, try %d.\n", re_count);
            sleep(1);
            continue;
        }
        if ((ret = startListener(&trans_sched_config_notify, CLOUD_ID, ver)) == 0) {
            g_is_listen = 1;
            ALOGD("cloud id: %s, version: %s cloud connect success.", CLOUD_ID, ver);
            break;
        }
        else {
            ALOGE("cloud connect failed, reconnect...");
        }
    } while (re_count-- > 0);
    return ret;
}

int stop_cloud_update(void)
{
    if (!g_is_listen) {
        ALOGE("cloud update is not listen.");
        return -EFAULT;
    }

    if(stopListener(CLOUD_ID) != 0) {
        ALOGE("cloud close failed.");
        return -EFAULT;
    }
    g_is_listen = 0;
    ALOGD("cloud close success.");
    return 0;
}

} /* extern C */
