#pragma once

#include <iostream>
#include <string>
#include <vector>

// Minimal, dependency-free host test framework. Tests are permanent and live
// in tst/ beside the library; `make test` compiles this runner with g++.

class Test {
  public:
    explicit Test(std::string name) : name(std::move(name)) {}

    template <typename A, typename B> void assertEqual(A actual, B expected, const std::string &message) {
        const long long a = static_cast<long long>(actual);
        const long long b = static_cast<long long>(expected);
        if (a != b) {
            failures.push_back(message + " (have " + std::to_string(a) + ", want " + std::to_string(b) + ")");
        } else {
            ++passes;
        }
    }

    void assertTrue(bool value, const std::string &message) {
        assertEqual(value ? 1 : 0, 1, message);
    }

    void print() const {
        const char *color = failures.empty() ? "\033[32m" : "\033[31m";
        std::cout << color << "---------- " << name << " ----------\033[0m\n";
        std::cout << "Passed: " << passes << "\nFailed: " << failures.size() << "\n";
        for (const auto &failure : failures)
            std::cout << "\033[31m" << failure << "\033[0m\n";
    }

    bool failed() const { return !failures.empty(); }

  private:
    std::string name;
    int passes = 0;
    std::vector<std::string> failures;
};

class TestRunner {
  public:
    void add(const Test &test) { tests.push_back(test); }

    int printSummary() const {
        int passed = 0;
        int failed = 0;
        for (const auto &test : tests) {
            test.print();
            passed += test.failed() ? 0 : 1;
            failed += test.failed() ? 1 : 0;
        }
        std::cout << "\n========== " << passed << " passed, " << failed << " failed ==========\n";
        return failed == 0 ? 0 : 1;
    }

  private:
    std::vector<Test> tests;
};
