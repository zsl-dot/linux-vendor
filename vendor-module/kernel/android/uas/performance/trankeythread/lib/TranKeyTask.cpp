
#include <sstream>
#include <utility>
#include <bpf/WaitForProgsLoaded.h>
#include "include/TranKeyTask.h"
#include <regex.h>
#include <unistd.h>
#include <cutils/properties.h>

#ifdef DEBUG
#undef DEBUG
#endif
#define DEBUG 1
#define KEY_COMM_TYPE_STATIC 0
#define KEY_COMM_TYPE_DYNAMIC 1
#define KEY_COMM_TYPE_REGEX 2

/* Runtime: adb shell setprop persist.sys.tran.tranKeyTask.debug 1; logcat tag libtrankeytask at D. */
int DEBUG_EN = 0;

#define log_debug_info(fmt, ...) \
    do { \
        if (DEBUG_EN) { \
            log_info(fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define attactTP(eventType, eventName) \
    do { \
        if (!attachTracepointProgram(eventType, eventName)) { \
            log_error("Failed to attach bpf program to %s/%s tracepoint [%d(%s)]", \
                eventType, eventName, errno, strerror(errno)); \
            return false; \
        } \
    } while(0)

#define initBpfRB(ringBuffer, handler, ctx, rbPath) \
    do { \
        int fd = bpf::mapRetrieveRW(rbPath); \
        if (fd < 0) { \
            log_error("Failed to retrieve pinned program from %s [%d(%s)]", rbPath, errno, \
                  strerror(errno)); \
            return false; \
        } \
        ringBuffer = (struct ring_buffer *)bpf_new_ringbuf(fd, handler, ctx); \
        if (ringBuffer == NULL) { \
            log_error("Failed to create ring buf %s [%d(%s)]", rbPath, errno, \
                  strerror(errno)); \
            return false; \
        } \
    } while(0)

#define initBpfMap(map, keyType, valType, mapPath) \
    do { \
        map = bpf::BpfMap<keyType, valType>(mapPath); \
        if (!map.isValid()) { \
            log_error("Failed to create bpf map from %s [%d(%s)]", \
                mapPath, errno, strerror(errno)); \
            return false; \
        } \
    } while(0)


#define checkBpfCallRet(fmt, ...) \
    do { \
        if (errno != 0) { \
            log_error("[%d(%s)]" fmt, errno, strerror(errno), ##__VA_ARGS__); \
            return false; \
        } \
    } while(0)

#define checkBpfCall(fmt, ...) \
    do { \
        if (errno != 0) { \
            log_error("[%d(%s)]" fmt, errno, strerror(errno), ##__VA_ARGS__); \
        } \
    } while(0)

#define COMM_MAX_LEN   (TASK_COMM_LEN - 1)

namespace com {
namespace transsion {

using android::base::StringPrintf;
using android::base::Result;
using namespace android;

static void initDebugEn() {
    DEBUG_EN = property_get_int32("persist.sys.tran.tranKeyTask.debug", 0);
}

static inline String8 comm2String8(const struct k_task_comm& key) {
    char buffer[TASK_COMM_LEN];
    memcpy(buffer, key.comm, sizeof(key.comm));
    return String8(buffer);
}

static inline String16 comm2String16(const struct k_task_comm& key) {
    char buffer[TASK_COMM_LEN];
    memcpy(buffer, key.comm, sizeof(key.comm));
    return String16(buffer);
}

static inline bool regexMatch(const char* pattern, const char* text) {
    regex_t regex;
    if (!pattern || !text) {
        return false;
    }
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        return false;
    }
    bool matched = (regexec(&regex, text, 0, nullptr, 0) == 0);
    regfree(&regex);
    return matched;
}

static inline bool makeTaskComm(const String8& nameUTF8, struct k_task_comm* outComm) {
    if (outComm == nullptr) {
        return false;
    }
    if (nameUTF8.size() > COMM_MAX_LEN) {
        log_error("name too long(size=%lu)", nameUTF8.size());
        return false;
    }
    memset(outComm, 0, sizeof(struct k_task_comm));
    memcpy(outComm->comm, nameUTF8.c_str(), nameUTF8.size());
    return true;
}

static int retrieveProgramFd(const std::string &eventType, const std::string &eventName) {
    std::string path = StringPrintf(BPF_FS_DIR_PATH "prog_tranKeyTask_tracepoint_%s_%s",
        eventType.c_str(), eventName.c_str());
    return bpf::retrieveProgram(path.c_str());
}


static bool attachTracepointProgram(const std::string &eventType, const std::string &eventName) {
    int prog_fd = retrieveProgramFd(eventType, eventName);
    if (prog_fd < 0) return false;
    return bpf_attach_tracepoint(prog_fd, eventType.c_str(), eventName.c_str()) >= 0;
}

template <typename Key, typename Value>
static inline bool clearMap(bpf::BpfMap<Key, Value>& map, const char* mapName, std::shared_mutex& mapLock) {
    log_debug_info("clearMap [%s] trying to acquire lock", mapName);
    std::unique_lock<std::shared_mutex> lock(mapLock);
    log_debug_info("clearMap [%s] lock acquired", mapName);
    errno = 0;
    auto res = map.clear();
    log_debug_info("clearMap [%s] clear end", mapName);
    if (!res.ok()) {
        log_error("Failed to clear %s map [%d(%s)]",
            mapName, res.error().code().value(), res.error().message().c_str());
        return false;
    }
    return true;
}

template <typename Key, typename Value>
static bool isBpfMapEmpty(bpf::BpfMap<Key, Value>& map, const char* mapName, std::shared_mutex& mapLock) {
    log_debug_info("isBpfMapEmpty [%s] trying to acquire lock", mapName);
    std::shared_lock<std::shared_mutex> lock(mapLock);
    log_debug_info("isBpfMapEmpty [%s] lock acquired", mapName);
    if (!map.isValid()) return false;
    auto empCnt = map.isEmpty();
    return (empCnt.ok() && !empCnt.value());
}

int handle_event(void *ctx, void *data, size_t data_sz) {
    if (!ctx) {
        log_error("ctx null, can't get map info, data size %zu", data_sz);
        return 0;
    }
    TranKeyTask *keyTask = (TranKeyTask *)ctx;

    if (!keyTask->mInit) {
        log_debug_info("mInit not initialized, can't get map info");
        return 0;
    }
    struct k_task_msg *msg = (struct k_task_msg *)data;
    if (!msg) {
        log_error("msg null, can't get map info");
        return 0;
    }

    int action = msg->action;
    int kPid = msg->k_task.pid, wkePid = kPid;
    int kTgid = msg->k_task.tgid, wkeTgid = kTgid;
    String16 kComm = comm2String16(msg->k_task.comm), wkeComm = kComm;
    if (action == K_OVER_THR) {
        wkePid = msg->wke_task.pid;
        wkeTgid = msg->wke_task.tgid;
        wkeComm = comm2String16(msg->wke_task.comm);
    }
    if (action == K_KEY_REGEX_COMM) {
        bool regexMatched = false;
        String8 kComm8(kComm);
        log_debug_info("Regex event received, kComm=%s", kComm8.c_str());
        std::shared_lock<std::shared_mutex> lock(keyTask->mKeyRegexCommSetLock);
        for (const std::string& regexPattern : keyTask->mKeyRegexCommSet) {
            if (regexMatched) {
                break;
            }
            bool matches = regexMatch(regexPattern.c_str(), kComm8.c_str());
            log_debug_info("Regex compare, pattern=%s, kComm=%s, matched=%d",
                regexPattern.c_str(), kComm8.c_str(), matches ? 1 : 0);
            if (matches) {
                wkeComm = String16(regexPattern.c_str());
                regexMatched = true;
            }
        }
        if (!regexMatched) {
            log_debug_info("Regex event filtered, kComm=%s, no pattern matched", kComm8.c_str());
            return 0;
        }
        log_debug_info("Regex event accepted, kComm=%s, regex=%s", kComm8.c_str(), String8(wkeComm).c_str());
    }
    if (keyTask->mCallback) {
        log_debug_info(
            "Trigger key task callback, action: %d, pid: %d, tgid: %d, comm: %s, load: %lu, wke_pid: %d, wke_tgid: %d, wke_comm: %s",
            action, kPid, kTgid, String8(kComm).c_str(), msg->k_task.load, wkePid, wkeTgid, String8(wkeComm).c_str()
        );
        keyTask->mCallback(action, kPid, kTgid, kComm, wkePid, wkeTgid, wkeComm);
    }
    return 0;
}

void attachRingbuf(struct ring_buffer *rb, TranKeyTask *keyTask) {
    log_info("Key task monitoring start.");
    int err;
    while(keyTask->mMonitoring){
        err = bpf_poll_ringbuf(rb, -1);
        if (err < 0 && err != -EINTR) {
            log_error("Error polling ring buffer: [%d(%s)]", errno, strerror(errno));
        }
    }
}

TranKeyTask::TranKeyTask() {}
TranKeyTask::~TranKeyTask() {
    mMonitoring = false;
}

bool TranKeyTask::initBpf() {
    log_debug_info("TranKeyTask initBpf start");
    bpf::waitForProgsLoaded();

    initBpfMap(mEnableMap, int, int, kEnableMapPath);
    initBpfMap(mKeyCommMap, struct k_task_comm, int, kKeyCommMapPath);
    initBpfMap(mKeyRegexCountMap, int, int, kKeyRegexCountMapPath);
    initBpfMap(mKeyTaskMap, int, struct k_task_info, kKeyTaskMapPath);
    initBpfMap(mKeyDepMap, int, struct k_task_dep_arr, kKeyDepMapPath);
    initBpfMap(mReportMap, int, int, kReportMapPath);

    mInitMap = true;
    resetAllMaps();
    initBpfRB(mRB, handle_event, this, kKeyTaskRBPath);
    startMonitor();
    attactTP(kOomTraceGroup, kOomScoreAdjUpdateTp);
    attactTP(kTaskTraceGroup, kTaskRenameTp);
    attactTP(kSchedTraceGroup, kSchedWakingTp);
    attactTP(kSchedTraceGroup, kSchedProcessFreeTp);
    log_debug_info("TranKeyTask initBpf end");
    return true;
}

void TranKeyTask::resetAllMaps() {
    log_debug_info("TranKeyTask resetAllMaps start");
    if (mEnableMap.isValid()) {
        writeEnable(0);
    }

    if (isBpfMapEmpty(mKeyCommMap, "KeyCommMap", mKeyCommMapLock)) {
        clearMap(mKeyCommMap, "KeyCommMap", mKeyCommMapLock);
    }

    if (isBpfMapEmpty(mKeyTaskMap, "KeyTaskMap", mKeyTaskMapLock)) {
        clearMap(mKeyTaskMap, "KeyTaskMap", mKeyTaskMapLock);
    }

    if (isBpfMapEmpty(mKeyDepMap, "KeyDepMap", mKeyDepMapLock)) {
        clearMap(mKeyDepMap, "KeyDepMap", mKeyDepMapLock);
    }

    if (isBpfMapEmpty(mReportMap, "ReportMap", mReportMapLock)) {
        clearMap(mReportMap, "ReportMap", mReportMapLock);
    }

    bool cntEmpty = mKeyRegexCommSet.empty();

    if (!cntEmpty) {
        std::unique_lock<std::shared_mutex> lockSet(mKeyRegexCommSetLock);
        mKeyRegexCommSet.clear();
    }
    if (mKeyRegexCountMap.isValid()) {
        std::unique_lock<std::shared_mutex> lockSet(mKeyRegexCommSetLock);
        mKeyRegexCountMap.writeValue(0, 0, BPF_ANY);
    }
    log_debug_info("TranKeyTask resetAllMaps end");
}


bool TranKeyTask::initRecog() {
    initDebugEn();
    mInit = false;
    mInit = initBpf();
    return mInit;
}

void TranKeyTask::startMonitor(){
    mMonitoring = true;
    std::thread t1(attachRingbuf, mRB, this);
    t1.detach();
}

bool TranKeyTask::writeEnable(int enable) {
    if (!mInitMap) return false;
    std::unique_lock<std::shared_mutex> lock(mEnableMapLock);
    errno = 0;
    mEnableMap.writeValue(ENABLE_KEY, enable, BPF_ANY);
    checkBpfCallRet("Failed to write enable map");
    return true;
}

bool TranKeyTask::enableDynRecog() {
    mDynRegon = writeEnable(1);
    return mDynRegon;
}

bool TranKeyTask::disableDynRecog() {
    if (!mInit) return false;
    bool res = false;
    res = writeEnable(0);
    res = clearMap(mKeyDepMap, "KeyDepMap", mKeyDepMapLock);
    res = clearMap(mReportMap, "ReportMap", mReportMapLock);
    mDynRegon = !res;
    return res;
}

void TranKeyTask::updateRegexCommCnt(int delta)
{
    int key = 0;
    auto cntRes = mKeyRegexCountMap.readValue(key);
    int current = cntRes.ok() ? cntRes.value() : 0;
    int new_cnt = std::max(0, current + delta);
    if (new_cnt > 0) {
        mRegexEn = true;
    } else {
        mRegexEn = false;
    }
    errno = 0;
    mKeyRegexCountMap.writeValue(key, new_cnt, BPF_ANY);
    checkBpfCall("Failed to update key regex count");
}

bool TranKeyTask::addKeyComm(const String16& name, int val) {
    struct k_task_comm comm;

    if (!mInit) return false;
    String8 nameUTF8(name);
    if (val == KEY_COMM_TYPE_REGEX) {
        std::string regexPattern(nameUTF8.c_str(), nameUTF8.size());
        std::unique_lock<std::shared_mutex> lock2(mKeyRegexCommSetLock);
        auto insRes = mKeyRegexCommSet.insert(regexPattern);
        if (insRes.second) {
            log_debug_info("addKeyComm: write regex (origin=%s, normalized=%s)",
                nameUTF8.c_str(), regexPattern.c_str());
            updateRegexCommCnt(1);
        }
        return true;
    }
    if (!makeTaskComm(nameUTF8, &comm)) return false;
    errno = 0;
    std::unique_lock<std::shared_mutex> lock(mKeyCommMapLock);
    mKeyCommMap.writeValue(comm, val, BPF_ANY);
    checkBpfCallRet("Failed to write (name=%s) to key comm", nameUTF8.c_str());
    return true;
}

bool TranKeyTask::deleteKeyComm(const String16& name) {
    struct k_task_comm comm;
    if (!mInit) return false;
    String8 nameUTF8(name);
    bool deleted = false;
    if (nameUTF8.size() <= COMM_MAX_LEN && makeTaskComm(nameUTF8, &comm)) {
        errno = 0;
        std::unique_lock<std::shared_mutex> lock(mKeyCommMapLock);
        mKeyCommMap.deleteValue(comm);
        checkBpfCallRet("Failed to delete (name=%s) from key comm", nameUTF8.c_str());
        deleted = true;
    }
    std::unique_lock<std::shared_mutex> lock2(mKeyRegexCommSetLock);
    std::string regexPattern(nameUTF8.c_str(), nameUTF8.size());
    size_t regexDeletedCnt = mKeyRegexCommSet.erase(regexPattern);
    if (regexDeletedCnt > 0) {
        updateRegexCommCnt(-1);
        deleted = true;
    }
    return deleted;
}


bool TranKeyTask::getKeyTask(Vector<int>& pids, Vector<String16>& comms) {
    if (!mInit) return false;
    std::shared_lock<std::shared_mutex> lock(mKeyTaskMapLock);
    mKeyTaskMap.iterateWithValue([&pids, &comms]
        (const int& pid, const struct k_task_info& task, bpf::BpfMap<int, struct k_task_info>& map) -> Result<void> {
        pids.add(pid);
        comms.add(comm2String16(task.comm));
        return {};
    });
    return true;
}

bool TranKeyTask::queryDepTask(int pid, Vector<int>& pids, Vector<int>& tgids, Vector<String16>& comms, Vector<uint64_t>& loads) {
    if (!mInit || !mDynRegon) return false;
    std::shared_lock<std::shared_mutex> lock(mKeyDepMapLock);
    errno = 0;
    auto res = mKeyDepMap.readValue(pid);
    if (!res.ok()) {
        log_warn("Failed to read key dep map [%d(%s)]",
            res.error().code().value(), res.error().message().c_str());
        return false;
    }

    struct k_task_dep_arr depArr = res.value();
    for (int i = 0; i < depArr.size; ++i) {
        pids.add(depArr.arr[i].pid);
        tgids.add(depArr.arr[i].tgid);
        comms.add(comm2String16(depArr.arr[i].comm));
        loads.add(depArr.arr[i].load);
    }
    return true;
}


bool TranKeyTask::deleteReport(int pid) {
    if (!mInit) return false;
    std::unique_lock<std::shared_mutex> lock(mReportMapLock);
    errno = 0;
    mReportMap.deleteValue(pid);
    checkBpfCallRet("Failed to delete (pid=%d) from report map", pid);
    return true;
}

void TranKeyTask::dump(int out, const Vector<String16>& args) {

    if (!mInit) return;

    if (out < 0) {
        log_error("Invalid output fd %d", out);
        return;
    }
    std::ostringstream result;

#ifdef DEBUG
    result << "[DumpArgs]:\n";
    size_t size = args.size();
    for (size_t i = 0; i < size; ++i) {
        result << fmt::sprintf("args[%lu]=%s\n", i, String8(args[i]).c_str());
    }
#endif

    if (mEnableMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mEnableMapLock);
        auto empRet = mEnableMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            auto res = mEnableMap.readValue(ENABLE_KEY);
            if (!res.ok()) {
                log_warn("Failed to read enable map [%d(%s)]",
                    res.error().code().value(), res.error().message().c_str());
            } else {
                result << "[DynRecog]:\n";
                result << fmt::sprintf("dyn_recog=%d\n", res.value());
            }
        }
    }

    if (mKeyRegexCountMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mKeyRegexCommSetLock);
        auto empRet = mKeyRegexCountMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            auto res = mKeyRegexCountMap.readValue(0);
            if (!res.ok()) {
                log_warn("Failed to read key regex count map [%d(%s)]",
                    res.error().code().value(), res.error().message().c_str());
            } else {
                result << fmt::sprintf("regex_en=%d\n", res.value());
            }
        }
    }

    if (mKeyCommMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mKeyCommMapLock);
        auto empRet = mKeyCommMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            result << "[KeyCommMap]:\n";
            result << fmt::sprintf("%-*s%-s\n",
                30, "comm", "type");
            mKeyCommMap.iterateWithValue(
                [&] (const struct k_task_comm& comm, const int& val, bpf::BpfMap<struct k_task_comm, int>& map) -> Result<void> {
                result << fmt::sprintf("%-*s%-d\n",
                    30, comm2String8(comm).c_str(),
                    val);
                return {};
            });
        }
    }

    {
        std::shared_lock<std::shared_mutex> lock2(mKeyRegexCommSetLock);
        if (!mKeyRegexCommSet.empty()) {
            result << "[KeyRegexCommMap]:\n";
            result << fmt::sprintf("%-*s%-s\n",
                30, "comm", "type");
            for (const std::string& regexComm : mKeyRegexCommSet) {
                result << fmt::sprintf("%-*s%-d\n",
                    30, regexComm.c_str(),
                    KEY_COMM_TYPE_REGEX);
            }
        }
    }

    if (mKeyTaskMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mKeyTaskMapLock);
        auto empRet = mKeyTaskMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            result << "[KeyTaskMap]:\n";
            result << fmt::sprintf("%-*s%-*s%-*s\n",
                10, "pid", 10, "tgid", 20, "comm");
            mKeyTaskMap.iterateWithValue(
                [&] (const int& pid, const struct k_task_info& task, bpf::BpfMap<int, struct k_task_info>& map) -> Result<void> {
                result << fmt::sprintf("%-*d%-*d%-*s\n",
                    10, pid, 10, task.tgid,
                    20, comm2String8(task.comm).c_str());
                return {};
            });
        }

    }

    if (mKeyDepMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mKeyDepMapLock);
        auto empRet = mKeyDepMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            result << "[KeyDepMap]:\n";
            mKeyDepMap.iterateWithValue(
                [&] (const int& pid, const struct k_task_dep_arr& depArr, bpf::BpfMap<int, struct k_task_dep_arr>& map) -> Result<void> {
                result << fmt::sprintf("pid=%d:\n", pid);
                result << fmt::sprintf("%-*s%-*s%-*s%-*s\n",
                    10, "pid", 10, "tgid",
                    20, "comm", 10, "load");
                for (int i = 0; i < depArr.size; ++i) {
                    result << fmt::sprintf("%-*d%-*d%-*s%-*lu\n",
                        10, depArr.arr[i].pid,
                        10, depArr.arr[i].tgid,
                        20, comm2String8(depArr.arr[i].comm).c_str(),
                        10, depArr.arr[i].load);
                }
                return {};
            });
        }
    }

    if (mReportMap.isValid()) {
        std::shared_lock<std::shared_mutex> lock(mReportMapLock);
        auto empRet = mReportMap.isEmpty();
        if (empRet.ok() && !empRet.value()) {
            result << "[ReportMap]:\n";
            result << fmt::sprintf("%-*s%-*s\n",
                10, "pid", 10, "tgid");
            mReportMap.iterateWithValue(
                [&] (const int& pid, const int& tgid, bpf::BpfMap<int, int>& map) -> Result<void> {
                result << fmt::sprintf("%-*d%-*d\n",
                    10, pid, 10, tgid);
                return {};
            });
        }

    }

    std::string resultStr = std::move(result.str());
    write(out, resultStr.c_str(), resultStr.size());
}

void TranKeyTask::setCallback(bpf_callback_fn cb) {
    if (mCallback == nullptr) {
        log_info("Set key task bpf callback.");
        mCallback = cb;
    }
}


} // transsion
} // com
