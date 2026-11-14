/*
 * @Description: 测试用例与断言接口。
 *               用例编号由测试代码显式给出，不使用运行时自动编号。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_TEST_HARNESS_H_
#define _INCLUDE_TEST_HARNESS_H_

#include <crystal_cluster.h>
#include <stdio.h>
#include <string.h>

/* 一轮测试的生命周期 */
void cc_test_begin(const char *build, const char *level, CCUINT32 seed);
void cc_test_print_summary(void);

/* 套件边界 */
void cc_test_suite_begin(const char *suite, const char *title);
void cc_test_suite_end(void);

/* 用例边界：编号和名称由调用方显式给出。 */
void cc_test_case_begin(const char *id, const char *name,
                        const char *file, int line);
void cc_test_case_end(void);
void cc_test_case_skip(const char *reason);
void cc_test_manual(const char *id, const char *title,
                    const char *file, int line);

/* 断言 */
void cc_test_check(CCBOOL condition, const char *label,
                   const char *expected, const char *actual,
                   const char *file, int line);

/* 统计 */
CCINT32 cc_test_failures(void);
CCINT32 cc_test_suite_failures(void);
CCUINT32 cc_test_harness_errors(void);
CCBOOL cc_test_level_full(void);
CCUINT32 cc_test_seed(void);

#define CC_CASE(id, name) \
    cc_test_case_begin((id), (name), __FILE__, __LINE__)

#define CC_CASE_END() cc_test_case_end()

#define CC_CHECK(condition, label) \
    cc_test_check((condition), (label), NULL, NULL, __FILE__, __LINE__)

#define CC_CHECK_EQ_I64(label, expected, actual) \
    do { \
        char cc_expected_[32]; \
        char cc_actual_[32]; \
        snprintf(cc_expected_, sizeof(cc_expected_), "%lld", (long long)(expected)); \
        snprintf(cc_actual_, sizeof(cc_actual_), "%lld", (long long)(actual)); \
        cc_test_check((CCINT64)(expected) == (CCINT64)(actual), (label), \
                      cc_expected_, cc_actual_, __FILE__, __LINE__); \
    } while (0)

#define CC_CHECK_EQ_U64(label, expected, actual) \
    do { \
        char cc_expected_[32]; \
        char cc_actual_[32]; \
        snprintf(cc_expected_, sizeof(cc_expected_), "%llu", \
                 (unsigned long long)(expected)); \
        snprintf(cc_actual_, sizeof(cc_actual_), "%llu", \
                 (unsigned long long)(actual)); \
        cc_test_check((CCUINT64)(expected) == (CCUINT64)(actual), (label), \
                      cc_expected_, cc_actual_, __FILE__, __LINE__); \
    } while (0)

#define CC_CHECK_EQ_STR(label, expected, actual) \
    do { \
        const char *cc_expected_ = (expected); \
        const char *cc_actual_ = (actual); \
        CCBOOL cc_equal_ = CCFALSE; \
        if (!cc_expected_ && !cc_actual_) cc_equal_ = CCTRUE; \
        else if (cc_expected_ && cc_actual_) \
            cc_equal_ = (strcmp(cc_expected_, cc_actual_) == 0) ? CCTRUE : CCFALSE; \
        cc_test_check(cc_equal_, (label), \
                      cc_expected_ ? cc_expected_ : "(null)", \
                      cc_actual_ ? cc_actual_ : "(null)", \
                      __FILE__, __LINE__); \
    } while (0)

#define CC_CHECK_MEM(label, expected, expected_len, actual, actual_len) \
    do { \
        CCUINT64 cc_expected_len_ = (CCUINT64)(expected_len); \
        CCUINT64 cc_actual_len_ = (CCUINT64)(actual_len); \
        CCBOOL cc_equal_ = (cc_expected_len_ == cc_actual_len_) ? CCTRUE : CCFALSE; \
        if (cc_equal_ && cc_expected_len_ > 0) \
            cc_equal_ = (memcmp((expected), (actual), (size_t)cc_expected_len_) == 0) \
                ? CCTRUE : CCFALSE; \
        cc_test_check(cc_equal_, (label), "bytes match", "bytes differ", \
                      __FILE__, __LINE__); \
    } while (0)

#define CC_SKIP(reason) cc_test_case_skip((reason))

#define CC_MANUAL(id, title) \
    cc_test_manual((id), (title), __FILE__, __LINE__)

#endif
