#pragma once
#include <string>

enum class OpCode { ADD, SUB, ADDI, MUL, DIV, REM, LW, SW, BEQ, BNE, BLT, BLE, J, SLT, SLTI, AND, OR, XOR, ANDI, ORI, XORI };
enum class UnitType { ADDER, MULTIPLIER, DIVIDER, LOADSTORE, BRANCH, LOGIC };

struct Instruction {
    OpCode op;
    int dest;
    int src1;
    int src2;
    int imm;
    int pc;
};

struct ProcessorConfig {
    int num_regs = 32;
    int rob_size = 64;
    int mem_size = 1024;

    int logic_lat = 1;
    int add_lat = 2;
    int mul_lat = 4;
    int div_lat = 5;
    int mem_lat = 4;

    int logic_rs_size = 4;
    int adder_rs_size = 4;
    int mult_rs_size = 2;
    int div_rs_size = 2;
    int br_rs_size = 2;
    int lsq_rs_size = 32;
};

// ============================================================
//  Reorder Buffer entry
//  Holds everything the Commit stage needs to retire the instr:
//    - what the instruction is (op, dest_reg)
//    - the computed value (when execute finishes)
//    - exception flag (set when execute detects overflow/div0/OOB)
//    - branch resolution info (for checking prediction at commit)
//    - memory op info (store addr + value)
// ============================================================
struct ROBEntry {
    bool busy = false;           // slot is occupied
    bool ready = false;          // execute has finished, value/target known
    OpCode op;                   // which operation
    int dest_reg = -1;           // architectural reg this writes (-1 for branches/sw/j)
    int value = 0;               // computed result (arith/logic/lw) OR sw's data
    bool exception = false;      // set by execute unit on overflow / div0 / OOB
    int instr_pc = -1;           // program-order PC of this instr (for debug + exception reporting)

    // --- branch-specific ---
    bool is_branch = false;      // includes J
    int predicted_target = -1;   // what fetch predicted as next PC
    int actual_target = -1;      // computed at execute (for conditional branches)
    bool taken = false;          // was the branch actually taken

    // --- memory-specific ---
    int mem_addr = -1;           // for lw/sw
    int store_value = 0;         // for sw only (since dest_reg is -1)
};

// ============================================================
//  Reservation Station entry
//  Holds an instruction waiting to execute. Operands are either
//  ready values (Vj/Vk) or pending on a ROB tag (Qj/Qk).
//    Qj == -1  means  Vj is the actual value
//    Qj >= 0   means  waiting for ROB entry Qj to broadcast
// ============================================================
struct RSEntry {
    bool busy = false;
    OpCode op;
    int Vj = 0, Vk = 0;          // operand values (when ready)
    int Qj = -1, Qk = -1;        // ROB tags (-1 = operand is ready in Vj/Vk)
    int imm = 0;                 // immediate (for addi, lw, sw, etc.)
    int rob_idx = -1;            // which ROB slot this will write to
    int dest_reg = -1;           // for debug; ROB already has this
    int instr_pc = -1;           // original program-order PC (for age-based RS scheduling)
};

// ============================================================
//  In-flight entry inside a pipelined execution unit
//  Instruction has left its RS and is flowing through the unit's
//  internal pipeline with `cycles_remaining` cycles until broadcast.
// ============================================================
struct InFlightEntry {
    int rob_idx = -1;
    int cycles_remaining = 0;
    int result = 0;
    bool exception = false;
    // for branches — we need both to check prediction at commit
    int actual_target = -1;
    bool taken = false;
    int mem_addr = -1;           // for lw/sw final address
    int store_value = 0;         // for sw
    int instr_pc = -1;
    int rs_slot = -1; 
};