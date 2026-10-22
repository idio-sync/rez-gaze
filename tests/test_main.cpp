#include "test_main.h"

#include <cstring>

// Runs every registered test, or only those whose name contains argv[1].
int main(int argc, char** argv) {
    int ran = 0, failed_tests = 0;
    for (auto& t : testing::Registry()) {
        if (argc > 1 && !std::strstr(t.name, argv[1])) continue;
        int before = testing::Failures();
        t.fn();
        ++ran;
        bool ok = testing::Failures() == before;
        if (!ok) ++failed_tests;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", t.name);
    }
    std::printf("%d/%d tests passed\n", ran - failed_tests, ran);
    return failed_tests == 0 && ran > 0 ? 0 : 1;
}
