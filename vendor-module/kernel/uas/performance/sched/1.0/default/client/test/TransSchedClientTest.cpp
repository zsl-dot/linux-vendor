/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Assist Tool
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  06/2024
 **/

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <unistd.h>
#include <android/log.h>
#include <hidl/HidlSupport.h>

#include "TransSchedClient.h"


using ::android::hardware::hidl_string;

void usage(void)
{
    fprintf(stderr, "usage:\n \
        \t./TransSchedTest 1 scene            -- set sched scene!\n \
        \t./TransSchedTest 2                  -- get sched scene!\n \
        \t./TransSchedTest 3 pid main_thread  -- set ux_tags by main_thread name!\n \
        \t./TransSchedTest 4 pid              -- cancel ux_tags!\n \
        \t./TransSchedTest 5 pid              -- get ux_tags!\n \
    ");
}

int main(int argc, char *argv[])
{
    int ret;
    int cmd;
    pid_t pid;
    char *main_thread = NULL;
    unsigned int scene;
    unsigned int ux_tags;

    if (argc < 2) {
        usage();
        return -EINVAL;
    }

    cmd = atoi(argv[1]);
    switch (cmd) {
        case 1:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            scene = atoi(argv[2]);
            ret = setTransSchedScene(scene);
            if (ret < 0) {
                fprintf(stderr, "set trans sched scene failed, scene=%#x, ret=%d!\n", scene, ret);
                return ret;
            }
            break;
        case 2:
            if (argc != 2) {
                usage();
                return -EINVAL;
            }

            ret = getTransSchedScene(&scene);
            if (ret < 0) {
                fprintf(stderr, "get trans sched scene failed, ret=%d!\n", ret);
            }
            else {
                fprintf(stdout, "get trans sched scene=%#x!\n", scene);
            }

            break;
        case 3:
            if (argc != 4) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            main_thread = argv[3];
            ret = setTransSchedUxTagsByName(pid, main_thread);
            if (ret < 0) {
                fprintf(stderr, "set trans sched ux_tags by name failed, pid=%d, main_thread=%s, ret=%d!\n", pid, main_thread, ret);
                return ret;
            }
            break;
        case 4:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            ret = cancelTransSchedUxTags(pid);
            if (ret < 0) {
                fprintf(stderr, "cancel trans sched ux_tags failed, pid=%d, ret=%d!\n", pid, ret);
                return ret;
            }
            break;
        case 5:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            ret = getTransSchedUxTags(pid, &ux_tags);
            if (ret < 0) {
                fprintf(stderr, "get trans sched ux_tags failed, pid=%d, ret=%d!\n", pid, ret);
            }
            else {
                fprintf(stdout, "get trans sched ux_tags=%#x!\n", ux_tags);
            }

            break;
        default:
            usage();
            return -EINVAL;
    }

    if (!ret) {
        fprintf(stdout, "cmd-%d: run success\n", cmd);
    }
    return ret;
}
