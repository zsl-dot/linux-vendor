/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS UAPI
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  03/2023
 **/

extern "C" {
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <utils/Log.h>
#include <sys/ioctl.h>

#include "SchedCore.h"


int g_trans_sched_fd = -EBADFD;

static int trans_sched_init(void)
{
    int ret;

    if (g_trans_sched_fd != -EBADFD) {
        return g_trans_sched_fd;
    }

    ret = access(TRANS_SCHED_DRV_PATH, F_OK);
    if (ret < 0) {
        ALOGE("trans sched don't support!\n");
        return ret;
    }

    ret = open(TRANS_SCHED_DRV_PATH, O_RDWR);
    if (ret < 0) {
        ALOGE("open %s failed!\n", TRANS_SCHED_DRV_PATH);
        return ret;
    }

    g_trans_sched_fd = ret;
    return g_trans_sched_fd;
}

static void trans_sched_deinit(void)
{
    if (g_trans_sched_fd != -EBADFD) {
        close(g_trans_sched_fd);
        g_trans_sched_fd = -EBADFD;
    }
}

static int is_available_state(int state)
{
    if (state & ~FEATURE_ALL) {
        return 0;
    }

    if (state && !(state & FEATURE_DEFAULT)) {
        return 0;
    }

    return 1;
}

static int is_available_scene(unsigned int scene)
{
    if (ignore_ssc_mask(scene) & (~SS_ALL)) {
        return 0;
    }

    return 1;
}

int set_proc_ux_tags(pid_t pid, unsigned int ux_tags, char *thread_name)
{
    int fd;
    int ret;
    struct ux_data ux_data = {0, 0, NULL};

    if (!thread_name) {
        ALOGE("param error: thread_name=%p\n", thread_name);
        return -EINVAL;
    }

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    ux_data.pid = pid;
    ux_data.ux_tags = ux_tags;
    ux_data.thread_name = thread_name;

    ret = ioctl(fd, TRANS_SET_PROC_UX_TAGS, &ux_data);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d pid=%d ret=%d\n", fd, pid, ret);
    }

    return ret;
}

static int trans_sched_state_ctl(int *state, unsigned long cmd)
{
    int fd;
    int ret;

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    if (!state) {
        ALOGE("param error: state=%p\n", state);
        return -EINVAL;
    }

    if (cmd != TRANS_GET_SCHED_STATE) {
        if (!is_available_state(*state)) {
            ALOGE("sched state not available: state=%#x\n", *state);
            return -EINVAL;
        }
    }

    ret = ioctl(fd, cmd, state);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d cmd=%lu state=%d\n", fd, cmd, *state);
    }

    return ret;
}

int get_trans_sched_state(int *state)
{
    return trans_sched_state_ctl(state, TRANS_GET_SCHED_STATE);
}

int set_trans_sched_state(int state)
{
    return trans_sched_state_ctl(&state, TRANS_SET_SCHED_STATE);
}

static int trans_sched_scene_ctl(unsigned int *scene, unsigned long cmd)
{
    int fd;
    int ret;

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    if (!scene) {
        ALOGE("param error: scene=%p\n", scene);
        return -EINVAL;
    }

    if (cmd != TRANS_GET_SCHED_SCENE) {
        if (!is_available_scene(*scene)) {
            ALOGE("scene not available: scene=%d\n", *scene);
            return -EINVAL;
        }
    }

    ret = ioctl(fd, cmd, scene);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d cmd=%lu scene=%d\n", fd, cmd, *scene);
    }

    return ret;
}

int set_trans_sched_scene(unsigned int scene)
{
    return trans_sched_scene_ctl(&scene, TRANS_SET_SCHED_SCENE);
}

int get_trans_sched_scene(unsigned int *scene)
{
    return trans_sched_scene_ctl(scene, TRANS_GET_SCHED_SCENE);
}

static int set_task_to_group(const char *path, pid_t pid)
{
    int fd;
    int ret;
    char buff[16] = {'\0'};
    int buff_len = sizeof(buff) / sizeof(buff[0]);

    if (!path) {
        ALOGE("Invalid argument - path NULL!\n");
        return -EFAULT;
    }

    ret = access(path, F_OK);
    if (ret < 0) {
        ALOGE("inquire about %s failed!\n", path);
        return ret;
    }

    fd = open(path, O_RDWR);
    if (fd < 0) {
        ALOGE("open %s failed!\n", path);
        return fd;
    }

    ret = snprintf(buff, buff_len, "%d", pid);
    if (ret < 0) {
        ALOGE("convert %d to string failed!\n", pid);
        goto out;
    }

    ret = write(fd, buff, strlen(buff) + 1);
    if (ret < 0) {
        ALOGE("write %d to %s failed!\n", pid, path);
    }

out:
    close(fd);
    return ret;
}

int set_trans_sched_group(pid_t pid, bool is_uxgroup)
{
    int ret;
    int sched_state = 0;

    get_trans_sched_state(&sched_state);
    if (sched_state == FEATURE_DEFAULT) {
        if (is_uxgroup) {
            ret = set_task_to_group(UAS_CGROUP, pid);
        }
        else {
            ret = set_task_to_group(DEFAULT_CGROUP, pid);
        }
        if (ret < 0) {
            ALOGE("set task(%d) to %s failed\n", pid, is_uxgroup ? UAS_CGROUP : DEFAULT_CGROUP);
            return ret;
        }
        ALOGD("set task(%d) to %s group.\n", pid, is_uxgroup ? UAS_CGROUP : DEFAULT_CGROUP);
    }
    else {
        ALOGD("trans_sched has been turned off, nothing to do hare.\n");
    }

    return 0;
}

static int trans_sched_ux_ctl(struct ux_data *ux_data, unsigned long cmd)
{
    int fd;
    int ret;

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    if (!ux_data) {
        ALOGE("param error: ux_data=%p\n", ux_data);
        return -EINVAL;
    }

    ret = ioctl(fd, cmd, ux_data);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d cmd=%lu ux_data->pid=%d ux_data->ux_tags=%#x\n", fd, cmd, ux_data->pid, ux_data->ux_tags);
    }

    return ret;
}

int set_trans_sched_ux_tags(pid_t pid, unsigned int ux_tags, int ref, char *thread_name)
{
    struct ux_data ux_data = {.pid = pid, .ux_tags = ux_tags, .ref = ref, .thread_name = thread_name};

    // set_trans_sched_group(pid, (ux_tags != 0));
    return trans_sched_ux_ctl(&ux_data, TRANS_SET_UX_TAGS);
}

int get_trans_sched_ux_tags(pid_t pid, unsigned int *ux_tags)
{
    int ret;
    struct ux_data ux_data = {.pid = pid, .ux_tags = 0};

    if (!ux_tags) {
        ALOGE("param error: ux_tags=%p\n", ux_tags);
        return -EINVAL;
    }

    ret = trans_sched_ux_ctl(&ux_data, TRANS_GET_UX_TAGS);
    if (ret < 0) {
        return ret;
    }

    *ux_tags = ux_data.ux_tags;
    return ret;
}

int set_trans_sched_ux_prio(pid_t pid, unsigned int shift)
{
    int fd;
    int ret;
    struct ux_prio ux_prio = {0, 0};

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    ux_prio.pid = pid;
    ux_prio.shift = shift;

    ret = ioctl(fd, TRANS_SET_UX_PRIO, &ux_prio);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d pid=%d ret=%d\n", fd, pid, ret);
    }

    return ret;
}

int get_trans_sched_ux_info(pid_t pid, char *info, unsigned int info_len)
{
    int fd;
    int ret;
    struct ux_info ux_info = {.pid = pid, .info = NULL, .info_len = info_len};

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    if (!info) {
        ALOGE("param error: info=%p\n", info);
        return -EINVAL;
    }

    ux_info.info = info;

    ret = ioctl(fd, TRANS_GET_UX_INFO, &ux_info);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d ux_info.pid=%d ux_info.info=%p ux_info.info_len=%d\n", \
                fd, ux_info.pid, ux_info.info, ux_info.info_len);
    }

    return ret;
}

int dump_trans_sched_ux_info(void)
{
    int fd;
    int ret;

    fd = trans_sched_init();
    if (fd < 0) {
        ALOGE("trans sched init falied!\n");
        return fd;
    }

    ret = ioctl(fd, TRANS_DUMP_UX_LIST);
    if (ret < 0) {
        ALOGE("ioclt error: fd=%d\n", fd);
    }

    return ret;
}
} /* extern C */
