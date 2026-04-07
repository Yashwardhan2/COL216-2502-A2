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

    void flush() {};

    void broadcastOnCDB() {};

    void stageFetch() {};

    void stageDecode() {};

    void stageExecuteAndBroadcast() {};

    void stageCommit() {};

    bool step() {
        clock_cycle++;
        return true; // return false if CPU has no more to do after this cycle
    }

    void dumpArchitecturalState() {
        std::cout << "\n=== ARCHITECTURAL STATE (CYCLE " << clock_cycle << ") ===\n";
        for (int i = 0; i < ARF.size(); i++) {
            std::cout << "x" << i << ": " << std::setw(4) << ARF[i] << " | ";
            if ((i+1) % 8 == 0) std::cout << std::endl;
        }
        if (exception) {
            std::cout << "EXCEPTION raised by instruction " << pc + 1 << std::endl;
        }
        std::cout << "Branch Predictor Stats: " << bp.correct_predictions << "/" << bp.total_branches << " correct.\n";
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
};
