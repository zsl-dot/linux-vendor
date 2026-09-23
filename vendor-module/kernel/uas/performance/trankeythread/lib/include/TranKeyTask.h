#ifndef _TRAN_KEY_TASK_H_
#define _TRAN_KEY_TASK_H_

#pragma once


#include <bpf/BpfMap.h>
#include <libbpf.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/Vector.h>
#include <utils/Log.h>
#include <utils/RefBase.h>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include "../../bpf/include/tran_bpf_keytask.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "libtrankeytask"

#define log_debug(fmt, ...) ALOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define log_info(fmt, ...) ALOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define log_warn(fmt, ...) ALOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define log_error(fmt, ...) ALOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

namespace com {
namespace transsion {

using namespace android;

typedef void (*bpf_callback_fn) (int action, int kPid, int kTgid, String16& kComm, int wkePid, int wkeTgid, String16& wkeComm);

class TranKeyTask : public virtual RefBase {

friend int handle_event(void *ctx, void *data, size_t data_sz);
friend void attachRingbuf(struct ring_buffer *rb, TranKeyTask *keyTask);

public:
    TranKeyTask();
    ~TranKeyTask();

    // initialize eBPF program and map
    bool initRecog();

    bool enableDynRecog();

    bool disableDynRecog();
    // void dump(int fd, const Vector<String16>& args);
    void dump(int out, const Vector<String16>& args);

    bool addKeyComm(const String16& name, int val);

    bool deleteKeyComm(const String16& name);

    bool getKeyTask(Vector<int>& pids, Vector<String16>& comms);

    bool queryDepTask(int pid, Vector<int>& pids, Vector<int>& tgids, Vector<String16>& comms, Vector<uint64_t>& loads);

    bool deleteReport(int pid);

    void setCallback(bpf_callback_fn cb);


private:

    static constexpr char kBpfBinName[] = "tranKeyTask";

    static constexpr char kSchedTraceGroup[] = "sched";

    static constexpr char kSchedWakingTp[] = "sched_waking";

    static constexpr char kSchedProcessExitTp[] = "sched_process_exit";

    static constexpr char kSchedProcessFreeTp[] = "sched_process_free";

    static constexpr char kOomTraceGroup[] = "oom";

    static constexpr char kOomScoreAdjUpdateTp[] = "oom_score_adj_update";

    static constexpr char kTaskTraceGroup[] = "task";

    static constexpr char kTaskRenameTp[] = "task_rename";


    // pinned tracepoint total bpf map path in bpf sysfs
    static constexpr char kEnableMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_dyn_enable_map";

    static constexpr char kKeyCommMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_key_comm_map";

    static constexpr char kKeyRegexCountMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_key_regex_cnt_map";

    static constexpr char kKeyTaskMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_key_task_map";

    static constexpr char kKeyDepMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_key_dep_list_map";

    static constexpr char kReportMapPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_report_task_map";

    static constexpr char kKeyTaskRBPath[] = BPF_FS_DIR_PATH"map_tranKeyTask_key_task_ringbuf";

    std::atomic<bool> mInit{false};
    bool mInitMap = false;
    bool mDynRegon = false;
    bool mMonitoring = false;
    bool mDebugEn = false;
    bool mRegexEn = false;

    struct ring_buffer *mRB;

    bpf_callback_fn mCallback = nullptr;

    bpf::BpfMap<int, int> mEnableMap;
    bpf::BpfMap<struct k_task_comm, int> mKeyCommMap;
    std::unordered_set<std::string> mKeyRegexCommSet;
    bpf::BpfMap<int, int> mKeyRegexCountMap;
    bpf::BpfMap<int, struct k_task_info> mKeyTaskMap;
    bpf::BpfMap<int, struct k_task_dep_arr> mKeyDepMap;
    bpf::BpfMap<int, int> mReportMap;

    std::shared_mutex mEnableMapLock;
    std::shared_mutex mKeyCommMapLock;
    std::shared_mutex mKeyRegexCommSetLock;
    std::shared_mutex mKeyTaskMapLock;
    std::shared_mutex mKeyDepMapLock;
    std::shared_mutex mReportMapLock;


    bool initBpf();
    bool writeEnable(int enable);
    void updateRegexCommCnt(int delta);
    void resetAllMaps();
    void startMonitor();
};

} // transsion
} // com


#endif /* _TRAN_KEY_TASK_H_ */
