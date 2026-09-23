/**
 * Copyright (C) 2023 transsion  Inc
 * add for UAS Cloud Control
 * @author xianhe.zhou@transsion.com
 * @version 1.0,  03/2023
 **/

#include "TransSched.h"

extern "C" {
#include<expat.h>
#include<sys/stat.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdbool.h>
#include<utils/Log.h>
#include<pthread.h>
#include<errno.h>

#include "SchedCore.h"
#include "TransSchedConfig.h"

static struct uxtag_entry g_uxtag_map[] = {
    {"UX_TASK_TAGS_NONE",      UX_TASK_TAGS_NONE},
    {"UX_TASK_TAGS_EDROP2",    UX_TASK_TAGS_EDROP2},
    {"UX_TASK_TAGS_EDROP1",    UX_TASK_TAGS_EDROP1},
    {"UX_TASK_TAGS_EXACT",     UX_TASK_TAGS_EXACT},
    {"UX_TASK_TAGS_EPROMOTE1", UX_TASK_TAGS_EPROMOTE1},
    {"UX_TASK_TAGS_EPROMOTE2", UX_TASK_TAGS_EPROMOTE2},
    {"UX_TASK_TAGS_DDROP2",    UX_TASK_TAGS_DDROP2},
    {"UX_TASK_TAGS_DDROP1",    UX_TASK_TAGS_DDROP1},
    {"UX_TASK_TAGS_DEFAULT",   UX_TASK_TAGS_DEFAULT},
    {"UX_TASK_TAGS_DPROMOTE1", UX_TASK_TAGS_DPROMOTE1},
    {"UX_TASK_TAGS_DPROMOTE2", UX_TASK_TAGS_DPROMOTE2},
    {"UX_TASK_TAGS_EDROP2_L",  UX_TASK_TAGS_EDROP2_L},
    {"UX_TASK_TAGS_RDROP1",    UX_TASK_TAGS_RDROP1},
    {"UX_TASK_TAGS_RECKON",    UX_TASK_TAGS_RECKON},
    {"UX_TASK_TAGS_RPROMOTE1", UX_TASK_TAGS_RPROMOTE1},
    {"UX_TASK_TAGS_FUTEX",     UX_TASK_TAGS_FUTEX},
    {"UX_TASK_TAGS_ABINDER",   UX_TASK_TAGS_ABINDER},
    {"UX_TASK_TAGS_ABINDER_BIT", UX_TASK_TAGS_ABINDER_BIT},
    {"UX_TASK_TAGS_E_S5",      UX_TASK_TAGS_E_S5},
    {"UX_TASK_TAGS_E_S6",      UX_TASK_TAGS_E_S6},
    {"UX_TASK_TAGS_EP1_S5",    UX_TASK_TAGS_EP1_S5},
    {"UX_TASK_TAGS_EP1_S6",    UX_TASK_TAGS_EP1_S6},
    {"UX_TASK_TAGS_DP1_S6",    UX_TASK_TAGS_DP1_S6},
};

static struct element_entry g_element_map[] = {
    {"trans_sched.config",  ELE_SCHED_CONFIG},
    {"uxtag",               ELE_UX_TAG},
    {"blacklist",           ELE_BLACK_LIST},
    {"instance",            ELE_INSTANCE},
};

static struct attr_len_entry g_attr_len_map[] = {
    {ATTR_ENABLE_STR,       ENABLE_STR_MAX_LEN},
    {ATTR_VERSION_STR,      VERSION_STR_MAX_LEN},
    {ATTR_BLACKNR_STR,      NR_STR_MAX_LEN},
    {ATTR_UXTAGNR_STR,      NR_STR_MAX_LEN},
    {ATTR_NAME_STR,         THREAD_NAME_STR_MAX_LEN},
    {ATTR_VALUE_STR,        TAG_STR_MAX_LEN},
    {ATTR_REF_STR,          REF_STR_MAX_LEN},
    {ATTR_DEFAULT_STR,      TAG_STR_MAX_LEN},
    {ATTR_ENCODING_STR,     ENCODING_STR_MAX_LEN},
};
static size_t uxtag_map_size = sizeof(g_uxtag_map) / sizeof(struct uxtag_entry);
static size_t element_map_size = sizeof(g_element_map) / sizeof(struct element_entry);
static size_t attr_len_map_size = sizeof(g_attr_len_map) / sizeof(struct attr_len_entry);

struct trans_sched_config *g_sched_config = NULL;
static PARSE_STATE g_ele_parse_state = PARSE_NULL;
static int g_uxtag_idx = 0;
static int g_blacklist_idx = 0;
static int g_parse_failed = 0;
static pthread_rwlock_t g_config_rwlock;


static bool is_empty(struct trans_sched_config *config) {
    if (!config || !config->uxtags || !config->blacklists) {
        return true;
    }
    return false;
}

void dump_sched_config(void) {
    pthread_rwlock_rdlock(&g_config_rwlock);
    if (is_empty(g_sched_config)) {
        ALOGE("config is null");
        goto UNLOCK;
    }

    int i;
    ALOGD("version: %s, enable: %#x, default: %#x", g_sched_config->version, g_sched_config->enable, g_sched_config->uxtags->default_uxtag);
    for (i = 0; i < g_sched_config->uxtags->nr; i++) {
        ALOGD("uxtag: %d. name: %s, tag: %#x, ref: %#x", i + 1, g_sched_config->uxtags->array[i].name, g_sched_config->uxtags->array[i].uxtag, g_sched_config->uxtags->array[i].ref);
    }

    for (i = 0; i < g_sched_config->blacklists->nr; i++) {
        ALOGD("blacklist: %d. name: %s", i + 1, g_sched_config->blacklists->array[i].name);
    }

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
}

static int get_attr_value_len(const char *name) {
    if (!name) {
        ALOGE("name is NULL.");
        return -EINVAL;
    }

    int idx;
    for (idx = 0; idx < attr_len_map_size; idx++) {
        if (!strcmp(g_attr_len_map[idx].attr_str, name)) {
           return g_attr_len_map[idx].len;
        }
    }
    ALOGE("no such attribute %s", name);
    return -EINVAL;

}

static int get_attr(const char **attrs, const char *name, char *value)
{
    int i = 0;
    int len = 0;
    int max_len = 0;
    char *tmp = NULL;
    if (!attrs || !(*attrs) || !name || !value) {
        ALOGE("get attr args is NULL.");
        return -EFAULT;
    }

    for (i = 0; attrs[i]; i += 2) {
        if (!strcmp(attrs[i], name)) {
            tmp = (char *)attrs[i + 1];
            max_len = get_attr_value_len(name);
            if (max_len < 0) {
                return -EINVAL;
            }

            if ((len = strlen(tmp)) > max_len) {
                ALOGE("current attr value %s too long (%d) parse failed, max length (%d) is allowed.", name, len, max_len);
                return -EINVAL;
            }
            strcpy(value, tmp);
            return len;
        }
    }
    ALOGE("no such attrbute %s", name);
    return -EINVAL;
}

static ELE_TYPE get_ele_type(const char *ele_str) {
    if (!ele_str) {
        ALOGE("ele_str is NULL.");
        return ELE_NOKNOW;
    }
    ELE_TYPE ret = ELE_NOKNOW;
    int idx;
    for (idx = 0; idx < element_map_size; idx++) {
        if (!strcmp(g_element_map[idx].ele_str, ele_str)) {
           ret = g_element_map[idx].type;
           break;
        }
    }
    return ret;
}



static unsigned int get_tag_value(const char *tag_str) {
    int idx;
    if (!tag_str) {
        ALOGE("tag_str is NULL.");
        return UX_TASK_TAGS_NONE;
    }

    for (idx = 0; idx < uxtag_map_size; idx++) {
        if (!strcmp(g_uxtag_map[idx].tag_str, tag_str)) {
           return g_uxtag_map[idx].tag_value;
        }
    }

    return UX_TASK_TAGS_NONE;
}

static void parse_head(const char **attrs, struct trans_sched_config *config)
{
    char *endptr = NULL;
    char ver_val[VERSION_STR_MAX_LEN];
    char en_val[ENABLE_STR_MAX_LEN];
    if (!attrs || !(*attrs) || !config) {
        ALOGE("attrs is NULL or config is NULL.");
        goto failed;
    }

    if (get_attr(attrs, ATTR_VERSION_STR, ver_val) < 0) {
        ALOGE("parse attribute %s failed.", ATTR_VERSION_STR);
        goto failed;
    }

    strcpy(config->version, ver_val);
    if (get_attr(attrs, ATTR_ENABLE_STR, en_val) < 0) {
        ALOGE("parse attribute %s failed.", ATTR_ENABLE_STR);
        goto failed;
    }

    if (!strcmp(TRANS_SCHED_CONFIG_V1_0, config->version)) {
        config->enable = strcmp("true", en_val) ? FEATURE_NONE : FEATURE_DEFAULT;
    }
    else {
        config->enable = (int)strtol(en_val, &endptr, 0);
    }

    return;

failed:
    g_parse_failed = 1;
}

static void parse_uxtag(const char **attrs, struct uxtag_array *uxtag_array) {

    char *endptr = NULL;
    char name_val[THREAD_NAME_STR_MAX_LEN];
    char value_val[TAG_STR_MAX_LEN];
    char ref_val[REF_STR_MAX_LEN];

    if (!attrs || !(*attrs) || !uxtag_array) {
        ALOGE("attrs is NULL or uxtag_array is NULL.");
        goto failed;
    }

    if (g_uxtag_idx >= uxtag_array->nr) {
        goto failed;
    }

    if (get_attr(attrs, ATTR_NAME_STR, name_val) < 0) {
        ALOGE("parse uxtag attribute %s failed.", ATTR_NAME_STR);
        goto failed;
    }
    strcpy(uxtag_array->array[g_uxtag_idx].name, name_val);

    if (get_attr(attrs, ATTR_VALUE_STR, value_val) < 0) {
        ALOGE("parse uxtag attribute %s failed.", ATTR_VALUE_STR);
        goto failed;
    }
    uxtag_array->array[g_uxtag_idx].uxtag = get_tag_value(value_val);

    if (get_attr(attrs, ATTR_REF_STR, ref_val) < 0) {
        ALOGE("parse uxtag attribute %s failed.", ATTR_REF_STR);
        goto failed;
    }
    uxtag_array->array[g_uxtag_idx].ref = (int)strtol(ref_val, &endptr, 0);
    g_uxtag_idx++;
    return;

failed:
    g_parse_failed = 1;
}

static void parse_pkgname(char *name_val, size_t max_len, char target_char) {

    char *src = name_val;
    char *dst = name_val;
    size_t count = 0;

    while (*src && count < max_len - 1) {
        if (*src != target_char) {
            *dst = *src;
            dst++;
        }
        src++;
        count++;
    }

    *dst = '\0';
}

static void parse_blacklist(const char **attrs, struct blacklist_array *blacklist_array) {

    char name_val[THREAD_NAME_STR_MAX_LEN];
    if (!attrs || !(*attrs) || !blacklist_array) {
        ALOGE("attrs is NULL or blacklist_array is NULL.");
        goto failed;
    }

    if (g_blacklist_idx >= blacklist_array->nr) {
        goto failed;
    }

    if (get_attr(attrs, ATTR_NAME_STR, name_val) < 0) {
        ALOGE("parse blacklist attribute %s failed.", ATTR_NAME_STR);
        goto failed;
    }

    parse_pkgname(name_val, THREAD_NAME_STR_MAX_LEN, '+');

    strcpy(blacklist_array->array[g_blacklist_idx].name, name_val);
    g_blacklist_idx++;
    return;
failed:
    g_parse_failed = 1;
}

static int init_config(struct trans_sched_config **config) {
    *config = (struct trans_sched_config *) malloc(sizeof(struct trans_sched_config));
    if (!(*config)) {
        ALOGE("malloc config failed.");
        return -ENOMEM;
    }
    (*config)->uxtags = NULL;
    (*config)->blacklists = NULL;
    return 0;
}

static void init_uxtag_array(const char **attrs, struct trans_sched_config *config)
{
    int uxtag_nr;
    int def_uxtag;
    char uxtag_nr_val[NR_STR_MAX_LEN];
    char def_uxtag_val[TAG_STR_MAX_LEN];
    if (!attrs || !config) {
        ALOGE("attrs is NULL or config is NULL.");
        g_parse_failed = 1;
        return;
    }

    if (get_attr(attrs, ATTR_UXTAGNR_STR, uxtag_nr_val) < 0) {
        ALOGE("init_uxtag_array attribute %s failed.", ATTR_UXTAGNR_STR);
        g_parse_failed = 1;
        return;
    }

    uxtag_nr = atoi(uxtag_nr_val);

    if (get_attr(attrs, ATTR_DEFAULT_STR, def_uxtag_val) < 0) {
        ALOGE("init_uxtag_array attribute %s failed.", ATTR_DEFAULT_STR);
        g_parse_failed = 1;
        return;
    }

    def_uxtag = get_tag_value(def_uxtag_val);
    struct uxtag_array *tmp = (struct uxtag_array *) malloc(sizeof(struct uxtag_array) + sizeof(struct uxtag_config) * uxtag_nr);
    if (!tmp) {
        ALOGE("malloc uxtag_array failed.");
        g_parse_failed = 1;
        return;
    }

    config->uxtags = tmp;
    config->uxtags->nr = uxtag_nr;
    config->uxtags->default_uxtag = def_uxtag;
}

static void init_blacklist_array(const char **attrs, struct trans_sched_config *config)
{
    int black_nr;
    char black_nr_val[NR_STR_MAX_LEN];

    if (get_attr(attrs, ATTR_BLACKNR_STR, black_nr_val) < 0) {
        ALOGE("init_blacklist_array attribute %s failed.", ATTR_BLACKNR_STR);
        g_parse_failed = 1;
        return;
    }

    black_nr = atoi(black_nr_val);
    struct blacklist_array *tmp = (struct blacklist_array *) malloc(sizeof(struct blacklist_array) + sizeof(struct blacklist_config) * black_nr);
    if (!tmp) {
        ALOGE("malloc blacklist failed.");
        g_parse_failed = 1;
        return;
    }

    config->blacklists = tmp;
    config->blacklists->nr = black_nr;
}

static int read_content(const char *path, char **buf)
{
    struct stat st;
    int ret = -EIO;
    FILE *fd = NULL;

    if (!(fd = fopen(path, "r"))) {
        ALOGE("open file: %s error.", path);
        goto CLOSE;
    }

    if ((ret = fstat(fileno(fd), &st)) < 0) {
        ALOGE("fstat file error.");
        goto CLOSE;
    }

    *buf = (char *) malloc(st.st_size);
    if (!*buf) {
        ALOGE("malloc buf failed.");
        goto CLOSE;
    }

    if ((ret = fread(*buf, 1, st.st_size, fd)) < 0){
        ALOGE("read file error.");
        goto CLOSE;
    }

CLOSE:
    if (fd) {
        fclose(fd);
    }

    return ret;
}

static void XMLCALL start(void *data, const char *name, const char **attrs)
{

    struct trans_sched_config *config = (struct trans_sched_config *)data;
    if (g_parse_failed) {
        return;
    }

    ELE_TYPE type = get_ele_type(name);
    switch (type) {
        case ELE_SCHED_CONFIG:
            parse_head(attrs, config);
            break;
        case ELE_UX_TAG:
            g_ele_parse_state = PARSE_UX_TAG;
            init_uxtag_array(attrs, config);

            break;
        case ELE_BLACK_LIST:
            g_ele_parse_state = PARSE_BLACK_LIST;
            init_blacklist_array(attrs, config);
            break;
        case ELE_INSTANCE:
            if (PARSE_UX_TAG == g_ele_parse_state) {
                parse_uxtag(attrs, config->uxtags);
            }
            else if (PARSE_BLACK_LIST == g_ele_parse_state) {
                parse_blacklist(attrs, config->blacklists);
            }
            else {
                ALOGE("no match element parse state.");
                return;
            }
            break;
        case ELE_NOKNOW:
            return;

    }
}

static void XMLCALL end(void *data, const char *name)
{
    ELE_TYPE type = get_ele_type(name);

    switch (type) {
        case ELE_SCHED_CONFIG:
            g_ele_parse_state = PARSE_NULL;
            break;
        case ELE_UX_TAG:
            g_ele_parse_state = PARSE_NULL;
            break;
        case ELE_BLACK_LIST:
            g_ele_parse_state = PARSE_NULL;
            break;
    }
}

struct xml_handler def_handler = {
    .start_element = start,
    .end_element = end,
};

static void free_config(struct trans_sched_config **config)
{
    if (!(*config)) {
        return;
    }
    if ((*config)->uxtags) {
        free((*config)->uxtags);
        (*config)->uxtags = NULL;
    }

    if ((*config)->blacklists) {
        free((*config)->blacklists);
        (*config)->blacklists = NULL;
    }

    if (*config) {
        free(*config);
        *config = NULL;
    }
}

static void state_reset() {
    g_ele_parse_state = PARSE_NULL;
    g_uxtag_idx = 0;
    g_blacklist_idx = 0;
    g_parse_failed = 0;
}

static int expat_parse(char *buf, size_t size, struct trans_sched_config *config, struct xml_handler *handler)
{
    int ret;
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, config);
    XML_SetElementHandler(parser, handler->start_element, handler->end_element);
    if ((ret = XML_Parse(parser, buf, size, XML_TRUE)) == XML_STATUS_ERROR) {
        ALOGE("parse failed.");
        goto FREE_PARSE;
    }

FREE_PARSE:
    XML_ParserFree(parser);
    return ret;
}

static int check_parse(struct trans_sched_config *config) {
    if (g_parse_failed) {
        return -EFAULT;
    }

    if (is_empty(config)) {
        return -EFAULT;
    }
    if (g_uxtag_idx != config->uxtags->nr || g_blacklist_idx != config->blacklists->nr) {
        ALOGE("parse nr not match xml");
        return -EFAULT;
    }
    return 0;
}

static int do_parse_config(char *buf, size_t size, struct trans_sched_config **config, struct xml_handler *handler)
{
    int ret;
    state_reset();
    if ((ret = init_config(config)) < 0) {
        return ret;
    }
    if ((ret = expat_parse(buf, size, *config, handler)) < 0) {
        free_config(config);
        return ret;
    }
    if ((ret = check_parse(*config)) < 0) {
        free_config(config);
        return ret;
    }

    return ret;
}

static int parse_config(const char *path, struct trans_sched_config **config) {
    char *buf = NULL;
    int ret;

    if ((ret = read_content(path, &buf)) <= 0) {
        goto FREE;
    }

    if ((ret = do_parse_config(buf, ret, config, &def_handler)) < 0) {
        goto FREE;
    }

FREE:
    if (buf) {
       free(buf);
    }

    return ret;
}

static int update_config(const char *path, struct trans_sched_config **config)
{
    int ret;
    struct trans_sched_config *new_config = NULL;
    if ((ret = parse_config(path, &new_config)) < 0) {
        ALOGE("parse new config error.");
        return ret;
    }
    free_config(config);
    *config = new_config;
    if (*config) {
        ret = 0;
    }
    return ret;
}

int update_sched_config(const char *path)
{
    if (!path) {
        ALOGE("update path is NULL");
        return -EINVAL;
    }

    int ret;
    pthread_rwlock_wrlock(&g_config_rwlock);
    if ((ret = update_config(path, &g_sched_config)) < 0) {
        goto UNLOCK;
    }

    if (!is_empty(g_sched_config)) {
        ret = 0;
    }

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
    return ret;
}

int init_sched_config(const char *path)
{
    int ret;
    if (!path) {
        ALOGE("init sched config path is NULL");
        return -EINVAL;
    }

    pthread_rwlock_init(&g_config_rwlock, NULL);
    if (!is_empty(g_sched_config)) {
        ALOGW("tran_sched_config has be inited.");
        return -EFAULT;
    }

    if ((ret = parse_config(path, &g_sched_config)) < 0) {
        ALOGE("init sched parse failed.");
        return ret;
    }

    return 0;
}

void destroy_sched_config(void)
{
    pthread_rwlock_wrlock(&g_config_rwlock);
    free_config(&g_sched_config);
    pthread_rwlock_unlock(&g_config_rwlock);
    pthread_rwlock_destroy(&g_config_rwlock);

}

int check_enable(void)
{
    int enable = -EFAULT;
    pthread_rwlock_rdlock(&g_config_rwlock);
    if (is_empty(g_sched_config)) {
        goto UNLOCK;
    }

    enable = g_sched_config->enable;

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
    return enable;
}

int get_default_uxtag(unsigned int *uxtag)
{
    int ret = -EFAULT;
    if (!uxtag) {
        ALOGE("uxtag pointer is NULL.");
        return -EINVAL;
    }

    pthread_rwlock_rdlock(&g_config_rwlock);
    if (is_empty(g_sched_config)) {
        goto UNLOCK;
    }
    *uxtag = g_sched_config->uxtags->default_uxtag;
    ret = 0;

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
    return ret;
}

int check_blacklist(const char *main_thread)
{
    int ret = 0;
    if (!main_thread) {
        ALOGE("main_thread is NULL");
        return -EINVAL;
    }

    if (!strnlen(main_thread, sizeof(char))) {
        ALOGE("main_thread is null string");
        return -EINVAL;
    }

    pthread_rwlock_rdlock(&g_config_rwlock);
    if (is_empty(g_sched_config)) {
        ret = -EFAULT;
        goto UNLOCK;
    }
    int idx;
    for (idx = 0; idx < g_sched_config->blacklists->nr; idx++) {
        if (strstr(main_thread, g_sched_config->blacklists->array[idx].name)) {
            ret = 1;
            break;
        }
    }

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
    return ret;
}

int check_uxtaglist(const char *main_thread, unsigned int *uxtag, int *ref)
{
    int ret = 0;
    int idx = 0;
    if (!main_thread || !uxtag || !ref) {
        ALOGE("mian_thread or uxtag is NULL");
        return -EINVAL;
    }

    pthread_rwlock_rdlock(&g_config_rwlock);
    if (is_empty(g_sched_config)) {
        ret = -EFAULT;
        goto UNLOCK;
    }

    for (idx = 0; idx < g_sched_config->uxtags->nr; idx++) {
        if (!strcmp(g_sched_config->uxtags->array[idx].name, main_thread)) {
            *ref = g_sched_config->uxtags->array[idx].ref;
            *uxtag = g_sched_config->uxtags->array[idx].uxtag;
            ret = 1;
            break;
        }
    }

    if (!ret && (!strncmp(main_thread, UX_EXACT_HEAD, UX_EXACT_HEAD_LEN))) {
        if (strcmp(main_thread, UX_EXACT_TEST))
            ret = -EINVAL;

        ALOGE("thread_id = %s is Invalid", main_thread);
    }

UNLOCK:
    pthread_rwlock_unlock(&g_config_rwlock);
    return ret;
}

int copy_config_file(const char *from, const char *to)
{
    if (!from || !to) {
        ALOGE("copy path from or to is NULL");
        return -EINVAL;
    }

    FILE *from_fp = NULL;
    FILE *to_fp = NULL;
    char ch = '\0';
    int ret = -EIO;
    if (!(from_fp = fopen(from, "r"))) {
        ALOGE("open from file: %s failed.", from);
        goto OUT;
    }

    if (!(to_fp = fopen(to, "w"))) {
        ALOGE("open to file: %s failed.", to);
        goto OUT;
    }
    ch = fgetc(from_fp);
    while (!feof(from_fp)) {
        ret = fputc(ch, to_fp);
        ch = fgetc(from_fp);
    }
OUT:
    if(from_fp) {
        fclose(from_fp);
    }
    if(to_fp) {
        fclose(to_fp);
    }
    return ret;
}

int get_config_version(char *ver)
{
    if (!ver) {
        ALOGE("ver is NULL");
        return -EINVAL;
    }

    if (is_empty(g_sched_config)) {
        return -EFAULT;
    }

    strcat(ver, "v");
    strcat(ver, g_sched_config->version);
    return 0;
}

} /* extern C */
