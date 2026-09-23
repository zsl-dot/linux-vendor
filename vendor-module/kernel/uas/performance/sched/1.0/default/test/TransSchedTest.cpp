/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Assist Tool
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  03/2023
 **/

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <sched.h>
#include <sys/wait.h>
#include <signal.h>
#include <android/log.h>
#include <hidl/HidlSupport.h>

#include "TransSched.h"


using ::android::sp;
using ::android::hardware::hidl_string;
using vendor::transsion::performance::sched::V1_0::ITransSched;

#define PI  (3.14)
#define TRANS_NR_CPUS (8)

int g_terminate = 0;

void usage(void)
{
    fprintf(stderr, "usage:\n \
        \t./TransSchedTest 1 state            -- set sched state!\n \
        \t./TransSchedTest 2                  -- get sched state!\n \
        \t./TransSchedTest 3 scene            -- set sched scene!\n \
        \t./TransSchedTest 4                  -- get sched scene!\n \
        \t./TransSchedTest 5 pid main_thread  -- set ux_tags by main_thread name!\n \
        \t./TransSchedTest 6 pid ux_tags      -- set ux_tags!\n \
        \t./TransSchedTest 7 pid shift        -- set prio shift, shift: 1:LOWER, 2:RAISE, 4:REBASE, 16:BML, 256:REBASE_CAP!\n \
        \t./TransSchedTest 8 pid              -- cancel ux_tags!\n \
        \t./TransSchedTest 9 pid              -- get ux_tags!\n \
        \t./TransSchedTest 10 pid             -- get ux info!\n \
        \t./TransSchedTest 11                 -- dump ux info to kernel log!\n \
    ");
    fprintf(stderr, ">>>simulate overloaded:\n \
        \t./TransSchedTest 12 loops           -- calculate trigonometric loops * 10000 times!\n \
        \t./TransSchedTest 12 bind_cpu loops  -- calculate trigonometric loops * 10000 times on bind_cpu CPU!\n \
    ");
    fprintf(stderr, "***cloud config:\n \
        \t./TransSchedTest 13                 -- get cloud state!\n \
        \t./TransSchedTest 14                 -- dump local config!\n \
    ");
}

void sig_handler(int)
{
    g_terminate = 1;
}

// loops = 1 --> loop 10000 times
void math_operation(int loops)
{
    unsigned long cnt;
    unsigned long times;
    volatile double val;
    volatile double angle;
    volatile double result;

    times = loops > 1 ? loops : 1;
    times *= 10000;

    while (1) {
        if (g_terminate) {
            break;
        }

        cnt = 0;
        while (1) {
            angle = 44.3;
            val = PI / 180;
            result = sin(angle * val);
            cnt ++;
            if (cnt > times) {
                break;
            }
        }

        usleep(3000);
    }
}

int cpu_pressure(int bind_cpu, int loops)
{
    int ret;
    cpu_set_t set;

    signal(SIGINT, sig_handler);

    if (bind_cpu < TRANS_NR_CPUS && bind_cpu >= 0) {
        CPU_SET(bind_cpu, &set);
        ret = sched_setaffinity(getpid(), sizeof(set), &set);
        if (ret < 0) {
            fprintf(stderr, "set sched_setaffinity failed!\n");
        }
    }

    fprintf(stdout, "start of pressurization, pid=%d, cpu=%d\n", getpid(), bind_cpu);
    nice(-4);
    math_operation(loops);
    fprintf(stdout, "end of pressurization, pid=%d\n", getpid());
    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    int cmd;
    int state;
    int cloud_state;
    int scene;
    pid_t pid;
    int loops;
    int bind_cpu;
    unsigned int shift;
    char *main_thread = NULL;
    unsigned int ux_tags;
    android::sp<ITransSched> sched = nullptr;

    if (argc < 2) {
        usage();
        return -EINVAL;
    }

    sched = ITransSched::getService();
    if (!sched) {
        fprintf(stderr, "get trans sched service failed!\n");
        return -EFAULT;
    }

    cmd = atoi(argv[1]);
    switch (cmd) {
        case 1:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            state = atoi(argv[2]);
            ret = sched->setTransSchedState(state);
            if (ret < 0) {
                fprintf(stderr, "set trans sched state failed, state=%#x, ret=%d!\n", state, ret);
                return ret;
            }
            break;
        case 2:
            if (argc != 2) {
                usage();
                return -EINVAL;
            }

            sched->getTransSchedState([&](int32_t ret_val, int32_t state) {
                if (ret_val < 0) {
                    fprintf(stderr, "get trans sched state failed, state=%#x, ret=%d!\n", state, ret_val);
                }
                else {
                    fprintf(stdout, "get trans sched state=%#x!\n", state);
                }
                ret = ret_val;
            });

            break;
        case 3:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            scene = atoi(argv[2]);
            ret = sched->setTransSchedScene(scene);
            if (ret < 0) {
                fprintf(stderr, "set trans sched scene failed, scene=%#x, ret=%d!\n", scene, ret);
                return ret;
            }
            break;
        case 4:
            if (argc != 2) {
                usage();
                return -EINVAL;
            }

            sched->getTransSchedScene([&](int32_t ret_val, int32_t scene) {
                if (ret_val < 0) {
                    fprintf(stderr, "get trans sched scene failed, scene=%#x, ret=%d!\n", scene, ret_val);
                }
                else {
                    fprintf(stdout, "get trans sched scene=%#x!\n", scene);
                }
                ret = ret_val;
            });

            break;
        case 5:
            if (argc != 4) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            main_thread = argv[3];
            ret = sched->setTransSchedUxTagsByName(pid, main_thread);
            if (ret < 0) {
                fprintf(stderr, "set trans sched ux_tags by name failed, pid=%d, main_thread=%s, ret=%d!\n", pid, main_thread, ret);
                return ret;
            }
            break;
        case 6:
            if (argc != 4) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            ux_tags = atoi(argv[3]);
            ret = sched->setTransSchedUxTags(pid, ux_tags);
            if (ret < 0) {
                fprintf(stderr, "set trans sched ux_tags failed, pid=%d, ux_tags=%#x, ret=%d!\n", pid, ux_tags, ret);
                return ret;
            }
            break;
        case 7:
            if (argc != 4) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            shift = atoi(argv[3]);
            ret = sched->setTransSchedUxPrio(pid, shift);
            if (ret < 0) {
                fprintf(stderr, "set trans sched ux dprio failed, pid=%d, shift=%d, ret=%d!\n", pid, shift, ret);
                return ret;
            }
            break;
        case 8:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            ret = sched->cancelTransSchedUxTags(pid);
            if (ret < 0) {
                fprintf(stderr, "cancel trans sched ux_tags failed, pid=%d, ret=%d!\n", pid, ret);
                return ret;
            }
            break;
        case 9:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            sched->getTransSchedUxTags(pid, [&](int32_t ret_val, uint32_t ux_tags) {
                if (ret_val < 0) {
                    fprintf(stderr, "get trans sched ux_tags failed, pid=%d, ret=%d!\n", pid, ret_val);
                }
                else {
                    fprintf(stdout, "get trans sched ux_tags=%#x!\n", ux_tags);
                }
                ret = ret_val;
            });

            break;
        case 10:
            if (argc != 3) {
                usage();
                return -EINVAL;
            }

            pid = atoi(argv[2]);
            sched->getTransSchedUxInfo(pid, [&](int32_t ret_val, hidl_string info) {
                if (ret_val < 0) {
                    fprintf(stderr, "get trans sched ux_info failed, pid=%d, ret=%d!\n", pid, ret_val);
                }
                else {
                    fprintf(stdout, "get trans sched ux_info:%s!\n", info.c_str());
                }
                ret = ret_val;
            });

            break;
        case 11:
            if (argc != 2) {
                usage();
                return -EINVAL;
            }

            ret = sched->dumpTransSchedUxInfo();
            if (ret < 0) {
                fprintf(stderr, "dump trans sched ux_info failed, ret=%d!\n", ret);
                return ret;
            }
            break;
        case 12:
            if (argc == 3) {
                loops = atoi(argv[2]);
                ret = cpu_pressure(-1, loops);
            }
            else if (argc == 4) {
                bind_cpu = atoi(argv[2]);
                loops = atoi(argv[3]);
                ret = cpu_pressure(bind_cpu, loops);
            }
            else {
                usage();
                return -EINVAL;
            }

            if (ret < 0) {
                fprintf(stderr, "cpu_pressure failed, ret=%d!\n", ret);
                return ret;
            }
            break;
        case 13:
            if (argc == 2) {
                cloud_state = sched->getCloudState();
                if (cloud_state < 0) {
                    fprintf(stdout, "get cloud state failed!\n");
                    return cloud_state;
                }
                else {
                    fprintf(stdout, "cloud state = %d!\n", cloud_state);
                }
                ret = 0;
            }
            else {
                usage();
                return -EINVAL;
            }
            break;
        case 14:
            if (argc == 2) {
                sched->dumpConfig();
                ret = 0;
            }
            else {
                usage();
                return -EINVAL;
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
