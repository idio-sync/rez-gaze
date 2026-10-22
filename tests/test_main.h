// A minimal test framework: TEST(name) registers a test, EXPECT(cond) records a failure.
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace testing {

struct Test {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Test>& Registry() {
    static std::vector<Test> tests;
    return tests;
}

inline int& Failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, fn}); }
};

}  // namespace testing

#define TEST(name)                                                   \
    static void test_##name();                                       \
    static testing::Registrar registrar_##name(#name, test_##name);  \
    static void test_##name()

#define EXPECT(cond)                                                                \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            ++testing::Failures();                                                  \
        }                                                                           \
    } while (0)
