#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UnityFramework Unity;

void UnityBegin(const char *filename) {
    Unity.NumberOfTests = 0;
    Unity.TestFailures = 0;
    Unity.TestIgnores = 0;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestFile = filename;
    Unity.CurrentTestLineNumber = 0;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    printf("==== Unity Test Run (%s) ====\n", filename);
}

void UnityConcludeTest(void) {
    printf("[%s]\n", Unity.CurrentTestName ? Unity.CurrentTestName
                                           : "(unnamed test)");
    if (Unity.CurrentTestIgnored) {
        Unity.TestIgnores++;
        printf("  IGNORE at %s:%u\n", Unity.CurrentTestFile,
               Unity.CurrentTestLineNumber);
    } else if (Unity.CurrentTestFailed) {
        Unity.TestFailures++;
        printf("  FAIL at %s:%u\n", Unity.CurrentTestFile,
               Unity.CurrentTestLineNumber);
    } else {
        printf("  PASS\n");
    }
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    printf("\n");
}

int UnityEnd(void) {
    printf("==== Result ====\n");
    printf("Tests   : %d\n", Unity.NumberOfTests);
    printf("Failures: %d\n", Unity.TestFailures);
    printf("Ignored : %d\n", Unity.TestIgnores);
    printf("================\n");
    return Unity.TestFailures;
}

void UnityFail(const char *message, UNITY_LINE_TYPE line) {
    Unity.CurrentTestFailed = 1;
    Unity.CurrentTestLineNumber = line;
    printf("  %s\n", message ? message : "Assertion failed");
    longjmp(Unity.AbortFrame, 1);
}

void UnityIgnore(const char *message, UNITY_LINE_TYPE line) {
    Unity.CurrentTestIgnored = 1;
    Unity.CurrentTestLineNumber = line;
    if (message)
        printf("  %s\n", message);
    longjmp(Unity.AbortFrame, 1);
}

void UnityAssertNotNull(const void *pointer, const char *message,
                        UNITY_LINE_TYPE line) {
    if (!pointer) {
        char buffer[128];
        if (!message)
            message = "Expected non-null pointer";
        snprintf(buffer, sizeof(buffer), "%s", message);
        UnityFail(buffer, line);
    }
}

void UnityAssertEqualString(const char *expected, const char *actual,
                            const char *message, UNITY_LINE_TYPE line) {
    if (!expected || !actual) {
        UnityFail(message ? message : "String comparison with NULL", line);
    }
    if (strcmp(expected, actual) != 0) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "%s\n    expected: \"%s\"\n    actual  : \"%s\"",
                 message ? message : "Strings differ", expected, actual);
        UnityFail(buffer, line);
    }
}
