#ifndef _TRANS_SCHED_CONFIG_H
#define _TRANS_SCHED_CONFIG_H

#define CLOUD_ID                        "1001000059"
#define CONFIG_NAME                     "trans_sched_config.xml"

#define UPDATE_CONFIG_PATH              "/data/vendor/trans_sched/trans_sched_config.xml"
#define DEFAULT_CONFIG_PATH             "/vendor/etc/cloudengine/1001000059/trans_sched_config.xml"
#define CLOUD_CONFIG_PATH               "/tranfs/CloudEngine/1001000059/trans_sched_config.xml"
#define VENDOR_CONFIG_PATH              "/vendor/etc/vconfig/tran_sched/trans_sched_config.xml" //tos16+

#define ATTR_ENABLE_STR                 "enable"
#define ATTR_VERSION_STR                "version"
#define ATTR_BLACKNR_STR                "black_nr"
#define ATTR_UXTAGNR_STR                "uxtag_nr"
#define ATTR_NAME_STR                   "name"
#define ATTR_VALUE_STR                  "value"
#define ATTR_REF_STR                    "ref"
#define ATTR_DEFAULT_STR                "default"
#define ATTR_ENCODING_STR               "encoding"

#define ATTR_STR_MAX_LEN                (32)
#define THREAD_NAME_STR_MAX_LEN         (128)
#define ENABLE_STR_MAX_LEN              (16)
#define NR_STR_MAX_LEN                  (8)
#define VERSION_STR_MAX_LEN             (8)
#define TAG_STR_MAX_LEN                 (32)
#define REF_STR_MAX_LEN                 (8)
#define ELE_STR_MAX_LEN                 (32)
#define ENCODING_STR_MAX_LEN            (8)

#define TRANS_SCHED_CONFIG_V1_0         "1.0"

typedef enum {
    ELE_SCHED_CONFIG,
    ELE_UX_TAG,
    ELE_BLACK_LIST,
    ELE_INSTANCE,
    ELE_NOKNOW,
} ELE_TYPE;

typedef enum {
    PARSE_UX_TAG,
    PARSE_BLACK_LIST,
    PARSE_NR,
    PARSE_CONTENT,
    PARSE_NULL,
} PARSE_STATE;

struct uxtag_entry {
    char tag_str[TAG_STR_MAX_LEN];
    unsigned int tag_value;
};

struct element_entry {
    char ele_str[ELE_STR_MAX_LEN];
    ELE_TYPE type;
};

struct attr_len_entry {
    char attr_str[ATTR_STR_MAX_LEN];
    unsigned int len;
};

struct uxtag_config {
    char name[THREAD_NAME_STR_MAX_LEN];
    unsigned int uxtag;
    int ref;
};

struct uxtag_array {
    int nr;
    unsigned int default_uxtag;
    struct uxtag_config array[0];
};

struct blacklist_config {
    char name[THREAD_NAME_STR_MAX_LEN];
};

struct blacklist_array {
    int nr;
    struct blacklist_config array[0];
};

struct trans_sched_config {
    int enable;
    char version[VERSION_STR_MAX_LEN];
    struct uxtag_array *uxtags;
    struct blacklist_array *blacklists;
};

struct xml_handler {
    void(*start_element)(void *, const char *, const char **);
    void(*end_element)(void *, const char *);
};

/**
 * Check config switch
 * return 0 -> off, 1 -> 1, negative -> error
*/
int check_enable(void);

/**
 * Check if it is on the blacklist
 * @main_thread thread name
 * return 0 -> not in, 1 -> in, negative -> error
*/
int check_blacklist(const char *main_thread);

/**
 * Check if it is on the whitelist. If it is, obtain the configured uxtag
 * @main_thread threadID
 * @uxtag configured uxtag for this threadID
 * @ref threadID reference count
 * return 0 -> not in, 1 -> in, negative -> error
*/
int check_uxtaglist(const char *main_thread, unsigned int *uxtag, int *ref);

/**
 * Obtain the version number of the configuration file
 * return configuration version
*/
int get_config_version(char *ver);

/**
 * Obtain the default uxtag. If the current thread does not have a configured uxtag, select the default uxtag
 * @uxtag default uxtag pointer
 * return 0 -> success, negative -> error
*/
int get_default_uxtag(unsigned int *uxtag);

/**
 * Update configuration
 * @path configuration file path
 * return 0 -> success, negative -> error
*/
int update_sched_config(const char *path);

/**
 * Initial configuration
 * @path configuration file path
 * return 0 -> success, negative -> error
*/
int init_sched_config(const char *path);

/**
 * Destroy configuration
*/
void destroy_sched_config(void);

/**
 * Dump configuration content
*/
void dump_sched_config(void);

/**
 * Copy configuration file from @from to @to
 * @from from path
 * @to to path
 * return 0 -> success, negative -> error
*/
int copy_config_file(const char *from, const char *to);



#endif /* _TRANS_SCHED_CONFIG_H */
