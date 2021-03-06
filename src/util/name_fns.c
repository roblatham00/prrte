/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2010      Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2014-2016 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2016-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2018-2020 Cisco Systems, Inc.  All rights reserved
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "prte_config.h"
#include "types.h"
#include "constants.h"

#include <stdio.h>
#include <string.h>

#include "src/util/printf.h"
#include "src/util/string_copy.h"
#include "src/threads/tsd.h"

#include "src/mca/errmgr/errmgr.h"

#include "src/util/name_fns.h"

#define PRTE_PRINT_NAME_ARGS_MAX_SIZE   50
#define PRTE_PRINT_NAME_ARG_NUM_BUFS    16

#define PRTE_SCHEMA_DELIMITER_CHAR      '.'
#define PRTE_SCHEMA_WILDCARD_CHAR       '*'
#define PRTE_SCHEMA_WILDCARD_STRING     "*"
#define PRTE_SCHEMA_INVALID_CHAR        '$'
#define PRTE_SCHEMA_INVALID_STRING      "$"

/* constructor - used to initialize namelist instance */
static void prte_namelist_construct(prte_namelist_t* list)
{
    list->name.jobid = PRTE_JOBID_INVALID;
    list->name.vpid = PRTE_VPID_INVALID;
}

/* destructor - used to free any resources held by instance */
static void prte_namelist_destructor(prte_namelist_t* list)
{
}

/* define instance of prte_class_t */
PRTE_CLASS_INSTANCE(prte_namelist_t,              /* type name */
                   prte_list_item_t,             /* parent "class" name */
                   prte_namelist_construct,      /* constructor */
                   prte_namelist_destructor);    /* destructor */

static bool fns_init=false;

static prte_tsd_key_t print_args_tsd_key;
char* prte_print_args_null = "NULL";
typedef struct {
    char *buffers[PRTE_PRINT_NAME_ARG_NUM_BUFS];
    int cntr;
} prte_print_args_buffers_t;

static void
buffer_cleanup(void *value)
{
    int i;
    prte_print_args_buffers_t *ptr;

    if (NULL != value) {
        ptr = (prte_print_args_buffers_t*)value;
        for (i=0; i < PRTE_PRINT_NAME_ARG_NUM_BUFS; i++) {
            free(ptr->buffers[i]);
        }
        free (ptr);
    }
}

static prte_print_args_buffers_t*
get_print_name_buffer(void)
{
    prte_print_args_buffers_t *ptr;
    int ret, i;

    if (!fns_init) {
        /* setup the print_args function */
        if (PRTE_SUCCESS != (ret = prte_tsd_key_create(&print_args_tsd_key, buffer_cleanup))) {
            PRTE_ERROR_LOG(ret);
            return NULL;
        }
        fns_init = true;
    }

    ret = prte_tsd_getspecific(print_args_tsd_key, (void**)&ptr);
    if (PRTE_SUCCESS != ret) return NULL;

    if (NULL == ptr) {
        ptr = (prte_print_args_buffers_t*)malloc(sizeof(prte_print_args_buffers_t));
        for (i=0; i < PRTE_PRINT_NAME_ARG_NUM_BUFS; i++) {
            ptr->buffers[i] = (char *) malloc((PRTE_PRINT_NAME_ARGS_MAX_SIZE+1) * sizeof(char));
        }
        ptr->cntr = 0;
        ret = prte_tsd_setspecific(print_args_tsd_key, (void*)ptr);
    }

    return (prte_print_args_buffers_t*) ptr;
}

char* prte_util_print_name_args(const prte_process_name_t *name)
{
    prte_print_args_buffers_t *ptr;
    char *job, *vpid;

    /* protect against NULL names */
    if (NULL == name) {
        /* get the next buffer */
        ptr = get_print_name_buffer();
        if (NULL == ptr) {
            PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
            return prte_print_args_null;
        }
        /* cycle around the ring */
        if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
            ptr->cntr = 0;
        }
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "[NO-NAME]");
        return ptr->buffers[ptr->cntr-1];
    }

    /* get the jobid, vpid strings first - this will protect us from
     * stepping on each other's buffer. This also guarantees
     * that the print_args function has been initialized, so
     * we don't need to duplicate that here
     */
    job = prte_util_print_jobids(name->jobid);
    vpid = prte_util_print_vpids(name->vpid);

    /* get the next buffer */
    ptr = get_print_name_buffer();

    if (NULL == ptr) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return prte_print_args_null;
    }

    /* cycle around the ring */
    if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }

    snprintf(ptr->buffers[ptr->cntr++],
             PRTE_PRINT_NAME_ARGS_MAX_SIZE,
             "[%s,%s]", job, vpid);

    return ptr->buffers[ptr->cntr-1];
}

char* prte_util_print_jobids(const prte_jobid_t job)
{
    prte_print_args_buffers_t *ptr;
    unsigned long tmp1, tmp2;

    ptr = get_print_name_buffer();

    if (NULL == ptr) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return prte_print_args_null;
    }

    /* cycle around the ring */
    if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }

    if (PRTE_JOBID_INVALID == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "[INVALID]");
    } else if (PRTE_JOBID_WILDCARD == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "[WILDCARD]");
    } else {
        /* find the job on our list */
        tmp1 = PRTE_JOB_FAMILY((unsigned long)job);
        tmp2 = PRTE_LOCAL_JOBID((unsigned long)job);
        snprintf(ptr->buffers[ptr->cntr++],
                 PRTE_PRINT_NAME_ARGS_MAX_SIZE,
                 "[%lu,%lu]", tmp1, tmp2);
    }
    return ptr->buffers[ptr->cntr-1];
}

char* prte_util_print_job_family(const prte_jobid_t job)
{
    prte_print_args_buffers_t *ptr;
    unsigned long tmp1;

    ptr = get_print_name_buffer();

    if (NULL == ptr) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return prte_print_args_null;
    }

    /* cycle around the ring */
    if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }

    if (PRTE_JOBID_INVALID == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "INVALID");
    } else if (PRTE_JOBID_WILDCARD == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "WILDCARD");
    } else {
        tmp1 = PRTE_JOB_FAMILY((unsigned long)job);
        snprintf(ptr->buffers[ptr->cntr++],
                 PRTE_PRINT_NAME_ARGS_MAX_SIZE,
                 "%lu", tmp1);
    }
    return ptr->buffers[ptr->cntr-1];
}

char* prte_util_print_local_jobid(const prte_jobid_t job)
{
    prte_print_args_buffers_t *ptr;
    unsigned long tmp1;

    ptr = get_print_name_buffer();

    if (NULL == ptr) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return prte_print_args_null;
    }

    /* cycle around the ring */
    if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }

    if (PRTE_JOBID_INVALID == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "INVALID");
    } else if (PRTE_JOBID_WILDCARD == job) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "WILDCARD");
    } else {
        tmp1 = (unsigned long)job & 0x0000ffff;
        snprintf(ptr->buffers[ptr->cntr++],
                 PRTE_PRINT_NAME_ARGS_MAX_SIZE,
                 "%lu", tmp1);
    }
    return ptr->buffers[ptr->cntr-1];
}

char* prte_util_print_vpids(const prte_vpid_t vpid)
{
    prte_print_args_buffers_t *ptr;

    ptr = get_print_name_buffer();

    if (NULL == ptr) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return prte_print_args_null;
    }

    /* cycle around the ring */
    if (PRTE_PRINT_NAME_ARG_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }

    if (PRTE_VPID_INVALID == vpid) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "INVALID");
    } else if (PRTE_VPID_WILDCARD == vpid) {
        snprintf(ptr->buffers[ptr->cntr++], PRTE_PRINT_NAME_ARGS_MAX_SIZE, "WILDCARD");
    } else {
        snprintf(ptr->buffers[ptr->cntr++],
                 PRTE_PRINT_NAME_ARGS_MAX_SIZE,
                 "%ld", (long)vpid);
    }
    return ptr->buffers[ptr->cntr-1];
}



/***   STRING FUNCTIONS   ***/
int prte_util_convert_vpid_to_string(char **vpid_string, const prte_vpid_t vpid)
{
    /* check for wildcard value - handle appropriately */
    if (PRTE_VPID_WILDCARD == vpid) {
        *vpid_string = strdup(PRTE_SCHEMA_WILDCARD_STRING);
        return PRTE_SUCCESS;
    }

    /* check for invalid value - handle appropriately */
    if (PRTE_VPID_INVALID == vpid) {
        *vpid_string = strdup(PRTE_SCHEMA_INVALID_STRING);
        return PRTE_SUCCESS;
    }

    if (0 > prte_asprintf(vpid_string, "%u", vpid)) {
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return PRTE_ERR_OUT_OF_RESOURCE;
    }

    return PRTE_SUCCESS;
}


int prte_util_convert_string_to_vpid(prte_vpid_t *vpid, const char* vpidstring)
{
    if (NULL == vpidstring) {  /* got an error */
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        *vpid = PRTE_VPID_INVALID;
        return PRTE_ERR_BAD_PARAM;
    }

    /** check for wildcard character - handle appropriately */
    if (0 == strcmp(PRTE_SCHEMA_WILDCARD_STRING, vpidstring)) {
        *vpid = PRTE_VPID_WILDCARD;
        return PRTE_SUCCESS;
    }

    /* check for invalid value */
    if (0 == strcmp(PRTE_SCHEMA_INVALID_STRING, vpidstring)) {
        *vpid = PRTE_VPID_INVALID;
        return PRTE_SUCCESS;
    }

    *vpid = strtoul(vpidstring, NULL, 10);

    return PRTE_SUCCESS;
}

int prte_util_convert_string_to_process_name(prte_process_name_t *name,
                                             const char* name_string)
{
    char *p;

    /* check for NULL string - error */
    if (NULL == name_string) {
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        return PRTE_ERR_BAD_PARAM;
    }

    p = strrchr(name_string, PRTE_SCHEMA_DELIMITER_CHAR); /** get last field -> vpid */

    /* check for error */
    if (NULL == p) {
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        return PRTE_ERR_BAD_PARAM;
    }
    p++;

    name->jobid = PRTE_PROC_MY_NAME->jobid;
    name->vpid = strtoul(p, NULL, 10);

    return PRTE_SUCCESS;
}

int prte_util_convert_process_name_to_string(char **name_string,
                                             const prte_process_name_t* name)
{
    prte_job_t *jdata;

    if (NULL == name) { /* got an error */
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        return PRTE_ERR_BAD_PARAM;
    }

    jdata = prte_get_job_data_object(name->jobid);

    prte_asprintf(name_string, "%s%c%lu", jdata->nspace, PRTE_SCHEMA_DELIMITER_CHAR, (unsigned long)name->vpid);

    return PRTE_SUCCESS;
}


/****    CREATE PROCESS NAME    ****/
int prte_util_create_process_name(prte_process_name_t **name,
                                  prte_jobid_t job,
                                  prte_vpid_t vpid
                                  )
{
    *name = NULL;

    *name = (prte_process_name_t*)malloc(sizeof(prte_process_name_t));
    if (NULL == *name) { /* got an error */
        PRTE_ERROR_LOG(PRTE_ERR_OUT_OF_RESOURCE);
        return PRTE_ERR_OUT_OF_RESOURCE;
    }

    (*name)->jobid = job;
    (*name)->vpid = vpid;

    return PRTE_SUCCESS;
}

/****    COMPARE NAME FIELDS     ****/
int prte_util_compare_name_fields(prte_ns_cmp_bitmask_t fields,
                                  const prte_process_name_t* name1,
                                  const prte_process_name_t* name2)
{
    /* handle the NULL pointer case */
    if (NULL == name1 && NULL == name2) {
        return PRTE_EQUAL;
    } else if (NULL == name1) {
        return PRTE_VALUE2_GREATER;
    } else if (NULL == name2) {
        return PRTE_VALUE1_GREATER;
    }

    /* in this comparison function, we check for exact equalities.
     * In the case of wildcards, we check to ensure that the fields
     * actually match those values - thus, a "wildcard" in this
     * function does not actually stand for a wildcard value, but
     * rather a specific value - UNLESS the CMP_WILD bitmask value
     * is set
     */

    /* check job id */
    if (PRTE_NS_CMP_JOBID & fields) {
        if (PRTE_NS_CMP_WILD & fields &&
            (PRTE_JOBID_WILDCARD == name1->jobid ||
             PRTE_JOBID_WILDCARD == name2->jobid)) {
            goto check_vpid;
        }
        if (name1->jobid < name2->jobid) {
            return PRTE_VALUE2_GREATER;
        } else if (name1->jobid > name2->jobid) {
            return PRTE_VALUE1_GREATER;
        }
    }

    /* get here if jobid's are equal, or not being checked
     * now check vpid
     */
 check_vpid:
    if (PRTE_NS_CMP_VPID & fields) {
        if (PRTE_NS_CMP_WILD & fields &&
            (PRTE_VPID_WILDCARD == name1->vpid ||
             PRTE_VPID_WILDCARD == name2->vpid)) {
            return PRTE_EQUAL;
        }
        if (name1->vpid < name2->vpid) {
            return PRTE_VALUE2_GREATER;
        } else if (name1->vpid > name2->vpid) {
            return PRTE_VALUE1_GREATER;
        }
    }

    /* only way to get here is if all fields are being checked and are equal,
    * or jobid not checked, but vpid equal,
    * only vpid being checked, and equal
    * return that fact
    */
    return PRTE_EQUAL;
}

/* hash a vpid based on Robert Jenkin's algorithm - note
 * that the precise values of the constants in the algo are
 * irrelevant.
 */
uint32_t  prte_util_hash_vpid(prte_vpid_t vpid) {
    uint32_t hash;

    hash = vpid;
    hash = (hash + 0x7ed55d16) + (hash<<12);
    hash = (hash ^ 0xc761c23c) ^ (hash>>19);
    hash = (hash + 0x165667b1) + (hash<<5);
    hash = (hash + 0xd3a2646c) ^ (hash<<9);
    hash = (hash + 0xfd7046c5) + (hash<<3);
    hash = (hash ^ 0xb55a4f09) ^ (hash>>16);
    return hash;
}

/* sysinfo conversion to and from string */
int prte_util_convert_string_to_sysinfo(char **cpu_type, char **cpu_model,
                                        const char* sysinfo_string)
{
    char *temp, *token;
    int return_code=PRTE_SUCCESS;

    /* check for NULL string - error */
    if (NULL == sysinfo_string) {
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        return PRTE_ERR_BAD_PARAM;
    }

    temp = strdup(sysinfo_string);  /** copy input string as the strtok process is destructive */
    token = strchr(temp, PRTE_SCHEMA_DELIMITER_CHAR); /** get first field -> cpu_type */

    /* check for error */
    if (NULL == token) {
        free(temp);
        PRTE_ERROR_LOG(PRTE_ERR_BAD_PARAM);
        return PRTE_ERR_BAD_PARAM;
    }
    *token = '\0';
    token++;

    /* If type is a valid string get the value otherwise leave cpu_type untouched.
     */
    if (0 != strcmp(temp, PRTE_SCHEMA_INVALID_STRING)) {
        *cpu_type = strdup(temp);
    }

    /* If type is a valid string get the value otherwise leave cpu_type untouched.
     */
    if (0 != strcmp(token, PRTE_SCHEMA_INVALID_STRING)) {
        *cpu_model = strdup(token);
    }

    free(temp);

    return return_code;
}

int prte_util_convert_sysinfo_to_string(char **sysinfo_string,
                                        const char *cpu_type, const char *cpu_model)
{
    char *tmp;

    /* check for no sysinfo values (like empty cpu_type) - where encountered, insert the
     * invalid string so we can correctly parse the name string when
     * it is passed back to us later
     */
    if (NULL == cpu_type) {
        prte_asprintf(&tmp, "%s", PRTE_SCHEMA_INVALID_STRING);
    } else {
        prte_asprintf(&tmp, "%s", cpu_type);
    }

    if (NULL == cpu_model) {
        prte_asprintf(sysinfo_string, "%s%c%s", tmp, PRTE_SCHEMA_DELIMITER_CHAR, PRTE_SCHEMA_INVALID_STRING);
    } else {
        prte_asprintf(sysinfo_string, "%s%c%s", tmp, PRTE_SCHEMA_DELIMITER_CHAR, cpu_model);
    }
    free(tmp);
    return PRTE_SUCCESS;
}

char *prte_pretty_print_timing(int64_t secs, int64_t usecs)
{
    unsigned long minutes, seconds;
    float fsecs;
    char *timestring;

    seconds = secs + (usecs / 1000000l);
    minutes = seconds / 60l;
    seconds = seconds % 60l;
    if (0 == minutes && 0 == seconds) {
        fsecs = ((float)(secs)*1000000.0 + (float)usecs) / 1000.0;
        prte_asprintf(&timestring, "%8.2f millisecs", fsecs);
    } else {
        prte_asprintf(&timestring, "%3lu:%02lu min:sec", minutes, seconds);
    }

    return timestring;
}
