#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>  
#include "Basics.h"

class ExecutionUnit {
public:
   
    UnitType name;
    int latency = 1;
    int rs_size = 4;
    std::vector<RSEntry> rs;
    std::vector<InFlightEntry> in_flight;
    bool has_result = false;
    bool has_exception = false;
    int  bcast_rob_idx = -1;
    int  bcast_value = 0;
    // branch extras (only meaningful if the finishing instr was a branch)
    int  bcast_actual_target = -1;
    bool bcast_taken = false;
    int  bcast_mem_addr = -1;       // unused for non-mem
    int  bcast_instr_pc = -1;

    ExecutionUnit() = default;

    ExecutionUnit(UnitType n, int lat, int size) {
        name = n;
        latency = lat;
        rs_size = size;
        rs.assign(size, RSEntry{});
    }

   
    bool rs_full() const {
        for (auto& e : rs) if (!e.busy) return false;
        return true;
    }

    int rs_alloc() {
        // returns index of a free RS slot, or -1 if full
        for (int i = 0; i < (int)rs.size(); i++) {
            if (!rs[i].busy) return i;
        }
        return -1;
    }

    int rs_occupied() const {
        int n = 0;
        for (auto& e : rs) if (e.busy) n++;
        return n;
    }

    void clear_broadcast() {
        has_result = false;
        has_exception = false;
        bcast_rob_idx = -1;
        bcast_value = 0;
        bcast_actual_target = -1;
        bcast_taken = false;
        bcast_mem_addr = -1;
        bcast_instr_pc = -1;
    }

    void flush_all() {
        // called on branch misprediction / exception
        for (auto& e : rs) e = RSEntry{};
        in_flight.clear();
        clear_broadcast();
    }

    
    void capture(int tag, int val) {
        for (auto& e : rs) {
            if (!e.busy) continue;
            if (e.Qj == tag) { e.Vj = val; e.Qj = -1; }
            if (e.Qk == tag) { e.Vk = val; e.Qk = -1; }
        }
    }

   
    void executeCycle() {
        clear_broadcast();

       
        int oldest_idx = -1;
        int oldest_pc = INT32_MAX;
        for (int i = 0; i < (int)rs.size(); i++) {
            if (!rs[i].busy) continue;
            if (rs[i].Qj != -1 || rs[i].Qk != -1) continue;  // operands not ready

            // Skip if already in-flight (issued in a previous cycle, still executing)
            bool already_issued = false;
            for (auto& f : in_flight) {
                if (f.rs_slot == i) { 
                    already_issued = true; 
                    break;
            }
            }
            if (already_issued) continue;

            if (rs[i].instr_pc < oldest_pc) {
                oldest_pc = rs[i].instr_pc;
                oldest_idx = i;
            }
        }

        if (oldest_idx != -1) {
            RSEntry& r = rs[oldest_idx];
            InFlightEntry f{};
            f.rs_slot = oldest_idx;
            f.rob_idx = r.rob_idx;
            f.instr_pc = r.instr_pc;
            f.cycles_remaining = latency;
            f.exception = false;

            switch (r.op) {
                // ---------- ADDER ops ----------
                case OpCode::ADD: {
                    int64_t res = (int64_t)r.Vj + (int64_t)r.Vk;
                    if (res > INT32_MAX || res < INT32_MIN) f.exception = true;
                    f.result = (int)res;
                    break;
                }
                case OpCode::SUB: {
                    int64_t res = (int64_t)r.Vj - (int64_t)r.Vk;
                    if (res > INT32_MAX || res < INT32_MIN) f.exception = true;
                    f.result = (int)res;
                    break;
                }
                case OpCode::ADDI: {
                    int64_t res = (int64_t)r.Vj + (int64_t)r.imm;
                    if (res > INT32_MAX || res < INT32_MIN) f.exception = true;
                    f.result = (int)res;
                    break;
                }
                case OpCode::SLT:
                    f.result = (r.Vj < r.Vk) ? 1 : 0;
                    break;
                case OpCode::SLTI:
                    f.result = (r.Vj < r.imm) ? 1 : 0;
                    break;

                case OpCode::MUL: {
                    int64_t res = (int64_t)r.Vj * (int64_t)r.Vk;
                    if (res > INT32_MAX || res < INT32_MIN) f.exception = true;
                    f.result = (int)res;
                    break;
                }
                case OpCode::DIV: {
                    if (r.Vk == 0) {
                        f.exception = true;
                        f.result = 0;
                    } else if (r.Vj == INT32_MIN && r.Vk == -1) {
                        f.exception = true;     // INT_MIN / -1 overflows
                        f.result = 0;
                    } else {
                        f.result = r.Vj / r.Vk;
                    }
                    break;
                }
                case OpCode::REM: {
                    if (r.Vk == 0) {
                        f.exception = true;
                        f.result = 0;
                    } else if (r.Vj == INT32_MIN && r.Vk == -1) {
                        f.result = 0;           // INT_MIN % -1 == 0 mathematically, no exception
                    } else {
                        f.result = r.Vj % r.Vk;
                    }
                    break;
                }
                case OpCode::AND:  f.result = r.Vj & r.Vk;  break;
                case OpCode::OR:   f.result = r.Vj | r.Vk;  break;
                case OpCode::XOR:  f.result = r.Vj ^ r.Vk;  break;
                case OpCode::ANDI: f.result = r.Vj & r.imm; break;
                case OpCode::ORI:  f.result = r.Vj | r.imm; break;
                case OpCode::XORI: f.result = r.Vj ^ r.imm; break;
                case OpCode::BEQ: {
                    f.taken = (r.Vj == r.Vk);
                    f.actual_target = f.taken ? (r.instr_pc + r.imm) : (r.instr_pc + 1);
                    break;
                }
                case OpCode::BNE: {
                    f.taken = (r.Vj != r.Vk);
                    f.actual_target = f.taken ? (r.instr_pc + r.imm) : (r.instr_pc + 1);
                    break;
                }
                case OpCode::BLT: {
                    f.taken = (r.Vj < r.Vk);
                    f.actual_target = f.taken ? (r.instr_pc + r.imm) : (r.instr_pc + 1);
                    break;
                }
                case OpCode::BLE: {
                    f.taken = (r.Vj <= r.Vk);
                    f.actual_target = f.taken ? (r.instr_pc + r.imm) : (r.instr_pc + 1);
                    break;
                }

                default:
                    f.result = 0;
                    break;
            }

            in_flight.push_back(f);
        }
        int finisher_idx = -1;
       
        for (auto& f : in_flight) {
            f.cycles_remaining--;
            if(f.cycles_remaining <= 0){
                finisher_idx = &f - &in_flight[0]; // index of the finisher (oldest with counter <= 0)
                
            }
        }

       
       

        if (finisher_idx != -1) {
            InFlightEntry& f = in_flight[finisher_idx];
            has_result = true;
            has_exception = f.exception;
            bcast_rob_idx = f.rob_idx;
            bcast_value = f.result;
            bcast_actual_target = f.actual_target;
            bcast_taken = f.taken;
            bcast_mem_addr = f.mem_addr;
            bcast_instr_pc = f.instr_pc;

            // Free the RS entry (broadcast time announced on  Piazza).
            if (f.rs_slot >= 0 && f.rs_slot < (int)rs.size()) {
                rs[f.rs_slot] = RSEntry{};
            }

            // Remove from in_flight.
            in_flight.erase(in_flight.begin() + finisher_idx);
        }
    }
};