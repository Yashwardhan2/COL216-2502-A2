#pragma once
#include "Basics.h"
#include <iostream>
#include <vector>
#include <unordered_map>

class BranchPredictor {
public:
    int total_branches = 0;
    int correct_predictions = 0;
    std::unordered_map<int, int> state;

    int predict(int current_pc, int imm, OpCode op) {
        int p = state[current_pc];
        if(p == 0 || p == 1) {
            return current_pc + imm; //taken
        } else {
            return current_pc + 1; //not taken
        }
    }

    void update(int pc, int actual_target, bool taken, bool was_correct) {
        total_branches++;
        if (was_correct) {
            correct_predictions++;
        }
        int &p = state[pc];
        if (taken) {
            if (p > 0) p--;
        } else {
            if (p < 3) p++;
        }
    }
};