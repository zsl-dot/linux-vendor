/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Client
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  06/2024
 **/

#include <sys/types.h>
#include <hidl/HidlSupport.h>

using ::android::hardware::Return;
using ::android::hardware::hidl_string;

Return<int32_t> setTransSchedScene(int32_t scene);
Return<int32_t> getTransSchedScene(uint32_t *scene);
Return<int32_t> setTransSchedUxTagsByName(int32_t pid, const hidl_string& mainThread);
Return<int32_t> cancelTransSchedUxTags(int32_t pid);
Return<int32_t> getTransSchedUxTags(int32_t pid, uint32_t *uxTags);

