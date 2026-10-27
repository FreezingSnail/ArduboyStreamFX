// Host test entry point. `make test` compiles this with the native compiler.
#include "codec_test.hpp"
#include "player_test.hpp"
#include "test.hpp"

int main() {
    TestRunner runner;
    codec_test::suite(runner);
    player_test::suite(runner);
    return runner.printSummary();
}
