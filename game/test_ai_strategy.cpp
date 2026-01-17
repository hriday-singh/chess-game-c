#include <iostream>
#include <iomanip>
#include <cassert>
#include "ai_engine.h"

// Define a simple test runner
void test_elo_thresholds() {
    std::cout << "Running AI Strategy ELO Threshold Tests..." << std::endl;
    std::cout << "------------------------------------------" << std::endl;

    struct TestCase {
        int elo;
        int expected_ms;
        std::string label;
    };

    TestCase cases[] = {
        {400, 1000, "Low ELO (<900)"},
        {899, 1000, "Boundary <900"},
        {900, 2000, "Boundary 900 (Mid)"},
        {1500, 2000, "Typical Mid ELO"},
        {1799, 2000, "Boundary <1800"},
        {1800, 5000, "Boundary 1800 (High)"},
        {2500, 5000, "Grandmaster ELO"}
    };

    bool all_passed = true;
    for (const auto& tc : cases) {
        AiDifficultyParams params = ai_get_difficulty_params(tc.elo);
        bool passed = (params.move_time_ms == tc.expected_ms);
        
        std::cout << std::left << std::setw(20) << tc.label 
                  << " ELO: " << std::setw(5) << tc.elo 
                  << " | Expected: " << std::setw(5) << tc.expected_ms << "ms"
                  << " | Result: " << std::setw(5) << params.move_time_ms << "ms"
                  << " | " << (passed ? "PASS" : "FAIL") << std::endl;
        
        if (!passed) all_passed = false;
    }

    std::cout << "------------------------------------------" << std::endl;
    if (all_passed) {
        std::cout << "SUCCESS: All AI Strategy tests passed!" << std::endl;
    } else {
        std::cout << "FAILURE: Some AI Strategy tests failed!" << std::endl;
        exit(1);
    }
}

int main() {
    test_elo_thresholds();
    return 0;
}
