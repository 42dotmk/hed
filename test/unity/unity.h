#ifndef UNITY_H
#define UNITY_H

#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int UNITY_LINE_TYPE;

typedef struct {
    const char *CurrentTestName;
    const char *CurrentTestFile;
    UNITY_LINE_TYPE CurrentTestLineNumber;
    int NumberOfTests;
    int TestFailures;
    int TestIgnores;
    int CurrentTestFailed;
    int CurrentTestIgnored;
    jmp_buf AbortFrame;
} UnityFramework;

extern UnityFramework Unity;

void UnityBegin(const char *filename);
int UnityEnd(void);
void UnityConcludeTest(void);
void UnityFail(const char *message, UNITY_LINE_TYPE line);
void UnityIgnore(const char *message, UNITY_LINE_TYPE line);
void UnityAssertNotNull(const void *pointer, const char *message,
                        UNITY_LINE_TYPE line);
void UnityAssertEqualString(const char *expected, const char *actual,
                            const char *message, UNITY_LINE_TYPE line);

void setUp(void);
void tearDown(void);

#define UNITY_BEGIN() (UnityBegin(__FILE__))
#define UNITY_END() (UnityEnd())
#define RUN_TEST(func)                                                          \
    do {                                                                        \
        Unity.CurrentTestName = #func;                                          \
        Unity.CurrentTestFile = __FILE__;                                       \
        Unity.CurrentTestLineNumber = __LINE__;                                 \
        Unity.NumberOfTests++;                                                  \
        if (setjmp(Unity.AbortFrame) == 0) {                                    \
            setUp();                                                            \
            func();                                                             \
            tearDown();                                                         \
        }                                                                       \
        UnityConcludeTest();                                                    \
    } while (0)

#define TEST_FAIL_MESSAGE(message) UnityFail((message), __LINE__)

#define TEST_IGNORE_MESSAGE(message)                                            \
    do {                                                                        \
        UnityIgnore((message), __LINE__);                                       \
        return;                                                                 \
    } while (0)

#define TEST_ASSERT_TRUE_MESSAGE(condition, message)                            \
    do {                                                                        \
        if (!(condition)) {                                                     \
            UnityFail((message), __LINE__);                                     \
        }                                                                       \
    } while (0)

#define TEST_ASSERT_NOT_NULL_MESSAGE(pointer, message)                          \
    UnityAssertNotNull((pointer), (message), __LINE__)

#define TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, actual, message)             \
    UnityAssertEqualString((expected), (actual), (message), __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* UNITY_H */
