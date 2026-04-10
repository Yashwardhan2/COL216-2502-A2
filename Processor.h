#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include "Basics.h"
#include "BranchPredictor.h"
#include "ExecutionUnit.h"
#include "LoadStoreQueue.h"
#include <sstream>  
 #include <cstdint>       
   

class Processor {
public:
    int pc;
    int clock_cycle;

    // pipeline registers

    std::vector<Instruction> inst_memory;

    // architectural state (do not change)
    std::vector<int> ARF; // regFile
    std::vector<int> Memory; // Memory
    bool exception = false; // exception bit

    // register alias table / reorder buffer
    // ---------- Register Alias Table ----------
    // RAT[r] == -1   -> ARF[r] is current
    // RAT[r] >= 0    -> value will come from ROB entry at that index
    std::vector<int> RAT;
    // ---------- Reorder Buffer (circular vector) ----------
    std::vector<ROBEntry> ROB;
    int rob_head = 0;     // oldest in-flight instruction (next to commit)
    int rob_tail = 0;     // next free slot
    int rob_count = 0;    // number of occupied slots
    // ---------- Pipeline registers ----------
    // Fetch -> Decode latch: one instruction per cycle
    struct FetchLatch {
        bool valid = false;
        Instruction instr;
        int predicted_next_pc = -1;
    } fd_latch;
   struct DecodeSnapshot {
        int rob_count = 0;
        int adder_rs_used = 0;
        int mult_rs_used = 0;
        int div_rs_used = 0;
        int logic_rs_used = 0;
        int branch_rs_used = 0;
        int lsq_used = 0;
    } snapshot;
      // ---------- Config cached for access in stages ----------
    ProcessorConfig cfg;

    // ---------- Halt state ----------
    bool halted = false;
    bool fetch_stall_this_cycle = false;   //if we want to flush the pipeline, we need to prevent new instructions from being fetched in the same cycle, so we will set this true if we detect a mispredicted branch or an exception in the Execute stage. It will be reset to false at the beginning of the next cycle.
    std::vector<ExecutionUnit> units;
    LoadStoreQueue* lsq;
    BranchPredictor bp;

    Processor(ProcessorConfig& config) {
        pc = 0;
        clock_cycle = 0;
        ARF.resize(config.num_regs, 0);
        Memory.resize(config.mem_size);

        // Instantiate Hardware Units
        // Adder
        // Multiplier
        // Divider
        // Branch Computation
        // Bitwise Logic
        // Load-Store Unit
         // --- NEW ---
        cfg = config;
        RAT.assign(config.num_regs, -1);
        ROB.assign(config.rob_size, ROBEntry{});
        rob_head = rob_tail = rob_count = 0;

        // Instantiate Hardware Units
        units.push_back(ExecutionUnit(UnitType::ADDER,      config.add_lat,   config.adder_rs_size));
        units.push_back(ExecutionUnit(UnitType::MULTIPLIER, config.mul_lat,   config.mult_rs_size));
        units.push_back(ExecutionUnit(UnitType::DIVIDER,    config.div_lat,   config.div_rs_size));
        units.push_back(ExecutionUnit(UnitType::BRANCH,     config.add_lat,   config.br_rs_size));
        units.push_back(ExecutionUnit(UnitType::LOGIC,      config.logic_lat, config.logic_rs_size));

        lsq = new LoadStoreQueue(config.mem_lat, config.lsq_rs_size);
    }
    ~Processor() {
        delete lsq;
    }

    void loadProgram(const std::string& filename) {
        std::ifstream file(filename);

        if (!file.is_open()) {
            std::cout << "Error: Cannot open file " << filename << std::endl;
            return;
        }
        std::string line;
        // Read first line
        if (std::getline(file, line)) {
            std::stringstream ss(line);
            int value;
            int idx = 0;
            while (ss >> value) {
                if (idx < (int)Memory.size()) {
                    Memory[idx++] = value;
                }
            }
           
        }
       
        int pc_counter = 0;
        while (std::getline(file, line)) {
            if (line.empty()) continue;  
            std::stringstream ss(line);
            std::vector<std::string> tokens;
            std::string word;
            while (ss >> word) {
                tokens.push_back(word);
            }
            Instruction inst;
            inst.dest = -1;
            inst.src1 = -1;
            inst.src2 = -1;
            inst.imm = 0;
            OpCode op = parse_opcode(tokens[0]);
            inst.op = op;
            if (op == OpCode::LW) {
                    inst.dest = parse_reg(tokens[1]);
                    int imm, reg;
                    parse_mem(tokens[2], imm, reg);

                    inst.src1 = reg;
                    inst.imm = imm;
                    inst.src2 = -1; // unused
                }
            else if (op == OpCode::SW) {
                    inst.src2 = parse_reg(tokens[1]);
                    inst.dest = -1; // unused
                    int imm, reg;
                    parse_mem(tokens[2], imm, reg);
                    inst.src1 = reg;
                    inst.imm = imm;
            }
            else if (op == OpCode::BEQ || op == OpCode::BNE ||op == OpCode::BLT || op == OpCode::BLE) {
                    inst.dest = -1; // unused
                    inst.src1 = parse_reg(tokens[1]);
                    inst.src2 = parse_reg(tokens[2]);
                    inst.imm = stoi(tokens[3]);
            }
            else if (op == OpCode::J) {
                    inst.imm = stoi(tokens[1]);
                    inst.dest = -1; // unused
                    inst.src1 = -1; // unused
                    inst.src2 = -1; // unused
            }
            else if (op == OpCode::ADDI || op == OpCode::SLTI ||op == OpCode::ANDI || op == OpCode::ORI ||op == OpCode::XORI) {
                    inst.dest = parse_reg(tokens[1]);
                    inst.src1 = parse_reg(tokens[2]);
                    inst.imm = stoi(tokens[3]);
                    inst.src2 = -1; // unused
            }
            else {
                    inst.dest = parse_reg(tokens[1]);
                    inst.src1 = parse_reg(tokens[2]);
                    inst.src2 = parse_reg(tokens[3]);
            }

            inst.pc = pc_counter;
            pc_counter++;
            inst_memory.push_back(inst);
        }  
        file.close();
   }

   void flush() {
        // Clear the fetch-decode latch so nothing new enters decode next cycle.
        fd_latch.valid = false;

        // Clear all RS entries and in-flight pipelines in every execution unit.
        for (auto& u : units) u.flush_all();

        // Clear the LSQ.
        if (lsq) lsq->flush_all();

        // Clear the ROB and reset RAT to point to ARF everywhere.
        for (auto& e : ROB) e = ROBEntry{};
        rob_head = rob_tail = rob_count = 0;
        for (auto& r : RAT) r = -1;
    }

    

    void stageFetch() {
        if (fetch_stall_this_cycle) return;       // commit flushed this cycle, wait
        if (fd_latch.valid) return;               // decode stalled, can't overwrite latch
        if (pc<0 || pc >= (int)inst_memory.size()) return; // no more instructions

        Instruction ins = inst_memory[pc];

        int predicted_next_pc;
        if (ins.op == OpCode::J) {
            // Unconditional jump: resolved immediately, no BP involved
            predicted_next_pc = pc + ins.imm;
        } else if (ins.op == OpCode::BEQ || ins.op == OpCode::BNE ||
                ins.op == OpCode::BLT || ins.op == OpCode::BLE) {
            // Conditional branch: ask the branch predictor
            predicted_next_pc = bp.predict(pc, ins.imm, ins.op);
        } else {
            // Everything else: fall through to next instruction
            predicted_next_pc = pc + 1;
        }

        fd_latch.valid = true;
        fd_latch.instr = ins;
        fd_latch.predicted_next_pc = predicted_next_pc;

        pc = predicted_next_pc;
    }

    void stageDecode() {
        if (!fd_latch.valid) return;

        Instruction ins = fd_latch.instr;
        int predicted = fd_latch.predicted_next_pc;

        // ----- 1. Check resources -----
        if (!can_alloc_rob_now()) return;

        UnitType target = unit_for(ins.op);
        bool is_mem  = (ins.op == OpCode::LW || ins.op == OpCode::SW);
        bool is_jump = (ins.op == OpCode::J);

        if (is_mem) {
            if (!can_alloc_lsq_now()) return;
        } else if (!is_jump) {
            if (!can_alloc_unit_rs_now(target)) return;
        }

        // ----- 2. Allocate ROB entry -----
        int rob_idx = rob_alloc();
        ROBEntry& rob = ROB[rob_idx];
        rob.op = ins.op;
        rob.instr_pc = ins.pc;
        rob.dest_reg = writes_register(ins.op) ? ins.dest : -1;
        rob.is_branch = (target == UnitType::BRANCH) || is_jump;
        rob.predicted_target = predicted;
        rob.ready = false;
        rob.exception = false;

        // ----- 3. Read source operands via RAT (BEFORE updating RAT for this instr) -----
        auto read_src = [&](int reg, int& V, int& Q) {
            if (reg <= 0) { V = 0; Q = -1; return; }
            int producer = RAT[reg];
            if (producer == -1) {
                V = ARF[reg];
                Q = -1;
            } else if (ROB[producer].ready) {
                V = ROB[producer].value;
                Q = -1;
            } else {
                V = 0;
                Q = producer;
            }
        };

        int Vj = 0, Vk = 0, Qj = -1, Qk = -1;
        read_src(ins.src1, Vj, Qj);
        read_src(ins.src2, Vk, Qk);

        // ----- 4. Allocate RS / LSQ entry -----
        if (is_jump) {
            rob.ready = true;                  // J: no execute, retires as no-op
            fd_latch.valid = false;
            return;
        }

        if (is_mem) {
            LoadStoreQueue::LSQEntry e{};
            e.busy = true;
            e.op = ins.op;
            e.Vj = Vj; e.Qj = Qj;
            e.Vk = Vk; e.Qk = Qk;
            e.imm = ins.imm;
            e.rob_idx = rob_idx;
            e.dest_reg = rob.dest_reg;
            e.instr_pc = ins.pc;
            e.cycles_remaining = -1;
            lsq->enqueue(e);
        } else {
            ExecutionUnit* u = get_unit(target);
            int slot = u->rs_alloc();
            RSEntry& rs = u->rs[slot];
            rs.busy = true;
            rs.op = ins.op;
            rs.Vj = Vj; rs.Qj = Qj;
            rs.Vk = Vk; rs.Qk = Qk;
            rs.imm = ins.imm;
            rs.rob_idx = rob_idx;
            rs.dest_reg = rob.dest_reg;
            rs.instr_pc = ins.pc;
        }

        // ----- 5. Update RAT (for register-writing instructions only) -----
        if (rob.dest_reg > 0) {                // x0 is never renamed
            RAT[rob.dest_reg] = rob_idx;
        }

        fd_latch.valid = false;
    }

    void stageExecuteAndBroadcast() {
        for (auto& u : units) u.executeCycle();
        // LSQ stays empty for Step 4; we'll call lsq->executeCycle(Memory) in Step 5.
        lsq->executeCycle(Memory, ROB);
    }
    void broadcastOnCDB() {
    // Helper lambda to process a single broadcast (from a unit or from LSQ)
    auto handle_broadcast = [&](int tag, int val, bool exc,
                                int actual_target, bool taken,
                                int mem_addr, OpCode op_hint_for_sw)
    {
        if (tag < 0 || tag >= (int)ROB.size()) return;
        ROBEntry& e = ROB[tag];
        e.value = val;
        e.ready = true;
        e.exception = exc;
        e.actual_target = actual_target;
        e.taken = taken;
        e.mem_addr = mem_addr;
        // For SW, also stash store_value in the ROB so commit can write Memory
        if (e.op == OpCode::SW) {
            e.store_value = val;
        }

        // Snoop all RS + LSQ
        for (auto& u : units) u.capture(tag, val);
        lsq->capture(tag, val);
    };

    for (auto& u : units) {
        if (!u.has_result) continue;
        handle_broadcast(u.bcast_rob_idx, u.bcast_value, u.has_exception,
                         u.bcast_actual_target, u.bcast_taken,
                         u.bcast_mem_addr, OpCode::ADD /*unused*/);
        u.clear_broadcast();
    }

    if (lsq->has_result) {
        handle_broadcast(lsq->bcast_rob_idx, lsq->bcast_value, lsq->has_exception,
                         -1, false, lsq->bcast_mem_addr, OpCode::SW);
        lsq->clear_broadcast();
    }
}

    void stageCommit() {
        // Reset the fetch-stall flag; commit will re-set it this cycle if a flush happens.
        fetch_stall_this_cycle = false;

        // Nothing to commit?
        if (rob_empty()) return;

        ROBEntry& head = ROB[rob_head];
        if (!head.ready) return;   // oldest instr hasn't finished executing yet

        // ------------------------------------------------------------
        // CASE 1: exception at head
        // ------------------------------------------------------------
        if (head.exception) {
            pc = head.instr_pc;          // PC of the faulting instruction
            exception = true;
            flush();                     // clear everything behind it
            halted = true;
            fetch_stall_this_cycle = true;
            // Do NOT pop the head. Do NOT write ARF/Memory.
            return;
        }

        // ------------------------------------------------------------
        // CASE 2: store -- write Memory, not a register
        // ------------------------------------------------------------
        if (head.op == OpCode::SW) {
            // Bounds check already happened at execute; if we got here,
            // either it was fine or exception is set (handled above).
            Memory[head.mem_addr] = head.store_value;
            rob_pop_head();
            return;
        }

        // ------------------------------------------------------------
        // CASE 3: branch (conditional) -- resolve vs. prediction
        // ------------------------------------------------------------
        if (head.is_branch && head.op != OpCode::J) {
            bool was_correct = (head.predicted_target == head.actual_target);
            bp.update(head.instr_pc, head.actual_target, head.taken, was_correct);

            if (!was_correct) {
                // Mispredict: flush pipeline, redirect pc, stall fetch for this cycle.
                pc = head.actual_target;
                flush();
                fetch_stall_this_cycle = true;
                return;
            }
            rob_pop_head();
            return;
        }

        // ------------------------------------------------------------
        // CASE 4: unconditional jump -- retires as a no-op (handled in fetch)
        // ------------------------------------------------------------
        if (head.op == OpCode::J) {
            // J was fully resolved in Fetch; BP is not involved per our design.
            // No arch-state change at commit.
            rob_pop_head();
            return;
        }

        // ------------------------------------------------------------
        // CASE 5: normal register-writing op (arith, logic, lw, slt, ...)
        // ------------------------------------------------------------
        if (head.dest_reg > 0) {    // x0 is never written
            ARF[head.dest_reg] = head.value;
            if (RAT[head.dest_reg] == rob_head) {
                RAT[head.dest_reg] = -1;
            }
        }
        rob_pop_head();
    }

    bool step() {
        if (halted) return false;
        clock_cycle++;
       
   
        // SNAPSHOT start-of-cycle availability
        snapshot.rob_count = rob_count;
        for (auto& u : units) {
            int n = u.rs_occupied();
            switch (u.name) {
                case UnitType::ADDER:      snapshot.adder_rs_used  = n; break;
                case UnitType::MULTIPLIER: snapshot.mult_rs_used   = n; break;
                case UnitType::DIVIDER:    snapshot.div_rs_used    = n; break;
                case UnitType::LOGIC:      snapshot.logic_rs_used  = n; break;
                case UnitType::BRANCH:     snapshot.branch_rs_used = n; break;
                default: break;
            }
        }
        snapshot.lsq_used = (int)lsq->queue.size();

        stageCommit();
        stageExecuteAndBroadcast();
        broadcastOnCDB();
        stageDecode();
        stageFetch();

        if (!halted && pc >= (int)inst_memory.size() && rob_empty() && !fd_latch.valid) {
            halted = true;
        }
        return true;
    }

    void dumpArchitecturalState() {
        std::cout << "\n=== ARCHITECTURAL STATE (CYCLE " << clock_cycle << ") ===\n";
        for (int i = 0; i < (int)ARF.size(); i++) {
            std::cout << "x" << i << ": " << std::setw(4) << ARF[i] << " | ";
            if ((i+1) % 8 == 0) std::cout << std::endl;
        }
        if (exception) {
            std::cout << "EXCEPTION raised by instruction " << pc + 1 << std::endl;
        }
        std::cout << "Branch Predictor Stats: " << bp.correct_predictions << "/" << bp.total_branches << " correct.\n";
    }
     // ---------- ROB helpers ----------
    bool rob_full()   { 
        return rob_count == cfg.rob_size;
     }
    bool rob_empty(){ 
        return rob_count == 0; 
    }

    int rob_alloc() {
        // returns the index of the newly allocated slot, or -1 if full
        if (rob_full()) return -1;
        int idx = rob_tail;
        ROB[idx] = ROBEntry{};
        ROB[idx].busy = true;
        rob_tail = (rob_tail + 1) % cfg.rob_size;
        rob_count++;
        return idx;
    }

    void rob_pop_head() {
        // called from commit after retiring the head entry

        ROB[rob_head] = ROBEntry{};
        rob_head = (rob_head + 1) % cfg.rob_size;
        rob_count--;
    }
    bool can_alloc_rob_now() {
        return snapshot.rob_count < cfg.rob_size;
    }

    bool can_alloc_unit_rs_now(UnitType t) {
        switch (t) {
            case UnitType::ADDER:      return snapshot.adder_rs_used  < cfg.adder_rs_size;
            case UnitType::MULTIPLIER: return snapshot.mult_rs_used   < cfg.mult_rs_size;
            case UnitType::DIVIDER:    return snapshot.div_rs_used    < cfg.div_rs_size;
            case UnitType::LOGIC:      return snapshot.logic_rs_used  < cfg.logic_rs_size;
            case UnitType::BRANCH:     return snapshot.branch_rs_used < cfg.br_rs_size;
            default: return false;
        }
    }

    bool can_alloc_lsq_now() {
        return snapshot.lsq_used < cfg.lsq_rs_size;
    }
    
    
private:
    
    int parse_reg(const std::string& tok) {
        return stoi(tok.substr(1));
    }

    void parse_mem(const std::string& tok, int& imm, int& reg) {
        size_t lparen = tok.find('(');
        size_t rparen = tok.find(')');
          if (lparen == std::string::npos || rparen == std::string::npos) {
           std::cout << "Error: Invalid memory operand format: " << tok << std::endl;
            return;
        }
        imm = stoi(tok.substr(0, lparen));
        reg = parse_reg(tok.substr(lparen + 1, rparen - lparen - 1));
    }

    OpCode parse_opcode(const std::string& s) {
        if (s == "add") return OpCode::ADD;
        else if (s == "sub") return OpCode::SUB;
        else if (s == "addi") return OpCode::ADDI;
        else if (s == "mul") return OpCode::MUL;
        else if (s == "div") return OpCode::DIV;
        else if (s == "rem") return OpCode::REM;
        else if (s == "lw") return OpCode::LW;
        else if (s == "sw") return OpCode::SW;
        else if (s == "beq") return OpCode::BEQ;
        else if (s == "bne") return OpCode::BNE;
        else if (s == "blt") return OpCode::BLT;
        else if (s == "ble") return OpCode::BLE;
        else if (s == "j") return OpCode::J;
        else if (s == "slt") return OpCode::SLT;
        else if (s == "slti") return OpCode::SLTI;
        else if (s == "and") return OpCode::AND;
        else if (s == "or") return OpCode::OR;
        else if (s == "xor") return OpCode::XOR;
        else if (s == "andi") return OpCode::ANDI;
        else if (s == "ori") return OpCode::ORI;
        else if (s == "xori") return OpCode::XORI;
        else {
            throw std::invalid_argument("Unknown opcode: " + s);
        }
    }

    bool writes_register(OpCode op) {
        //  false for SW, all branches, J; true otherwise
        return op != OpCode::SW && op != OpCode::BEQ && op != OpCode::BNE && op != OpCode::BLT && op != OpCode::BLE && op != OpCode::J;
    }
   

    // ---------- unit selection ----------
    UnitType unit_for(OpCode op) {
        switch (op) {
            case OpCode::ADD: case OpCode::SUB: case OpCode::ADDI:
            case OpCode::SLT: case OpCode::SLTI:
                return UnitType::ADDER;
            case OpCode::MUL:
                return UnitType::MULTIPLIER;
            case OpCode::DIV: case OpCode::REM:
                return UnitType::DIVIDER;
            case OpCode::AND: case OpCode::OR:  case OpCode::XOR:
            case OpCode::ANDI:case OpCode::ORI: case OpCode::XORI:
                return UnitType::LOGIC;
            case OpCode::BEQ: case OpCode::BNE:
            case OpCode::BLT: case OpCode::BLE:
                return UnitType::BRANCH;
            case OpCode::LW:  case OpCode::SW:
                return UnitType::LOADSTORE;
            case OpCode::J:
                return UnitType::BRANCH;   // placeholder; J is handled in Fetch
        }
        return UnitType::ADDER;
    }

    // find the unit by type; returns pointer into the `units` vector, or nullptr
    ExecutionUnit* get_unit(UnitType t) {
        for (auto& u : units) if (u.name == t) return &u;
        return nullptr;
    }
};
