/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Client
 * @author zhiyuan.wang@transsion.com
 * @version 1.0,  06/2024
 **/

#include <sys/types.h>
#include <string>

#define SS_ANIMATION        (0x1 << 2)
#define SS_TOUCH            (0x1 << 4)
#define SS_FLING            (0x1 << 5)
#define SS_CANCEL_MASK      (0x1 << 31)

int32_t setTransSchedScene(int32_t scene);
int32_t getTransSchedScene(uint32_t *scene);
int32_t setTransSchedUxTagsByName(int32_t pid, const std::string& mainThread);
int32_t cancelTransSchedUxTags(int32_t pid);
int32_t getTransSchedUxTags(int32_t pid, uint32_t *uxTags);

