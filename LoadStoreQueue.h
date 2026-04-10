#pragma once
#include <iostream>
#include <vector>
#include <string>
#include "Basics.h"

class LoadStoreQueue {
public:
    int latency = 4;
    int lsq_size = 32;
    struct LSQEntry {
        bool busy = false;
        OpCode op;                   // LW or SW
        int Vj = 0, Vk = 0;          // Vj = base addr value, Vk = store data value (SW only)
        int Qj = -1, Qk = -1;        // tags
        int imm = 0;
        int rob_idx = -1;
        int dest_reg = -1;           // LW's destination
        int instr_pc = -1;
        int cycles_remaining = -1;   // -1 = not yet started executing
    };

    std::vector<LSQEntry> queue;    // FIFO: index 0 is head

    // broadcast output (same contract as ExecutionUnit)
    bool has_result = false;
    bool has_exception = false;
    int  bcast_rob_idx = -1;
    int  bcast_value = 0;
    int  bcast_mem_addr = -1;
    int  bcast_instr_pc = -1;
    // (sw has no register result but still broadcasts "done" to retire the ROB entry)

    LoadStoreQueue() = default;

    LoadStoreQueue(int lat, int size) {
        latency = lat;
        lsq_size = size;
    }

    bool full() const { return (int)queue.size() >= lsq_size; }
    bool empty() const { return queue.empty(); }

    void clear_broadcast() {
        has_result = false;
        has_exception = false;
        bcast_rob_idx = -1;
        bcast_value = 0;
        bcast_mem_addr = -1;
        bcast_instr_pc = -1;
    }

    void flush_all() {
        queue.clear();
        clear_broadcast();
    }

    // Append a new memory op to the tail (called from Decode).
    // Returns true on success, false if full.
    bool enqueue(const LSQEntry& e) {
        if (full()) return false;
        queue.push_back(e);
        return true;
    }


    void capture(int tag, int val) {
        for (auto& e : queue) {
            if (!e.busy) continue;
            if (e.Qj == tag) { e.Vj = val; e.Qj = -1; }
            if (e.Qk == tag) { e.Vk = val; e.Qk = -1; }
        }
    }

   void executeCycle(std::vector<int>& Memory, std::vector<ROBEntry>& ROB) {
    clear_broadcast();

    //  1. START: find the next ready entry and start it 
    for (auto& e : queue) {
        if (e.cycles_remaining >= 0) continue;   // already started, skip

        // This is the next-in-order unstarted entry.
        if (e.Qj != -1) break;                  // base reg not ready -> stall
        if (e.op == OpCode::SW && e.Qk != -1) break;  // sw needs data reg ready too

        // Start it. Address computation + OOB detection will happen at finish time
        // for simplicity (we re-read Vj and imm then, same values).
        e.cycles_remaining = latency;
        break;   // only one start per cycle
    }

    // 2. DRAIN: decrement every started entry ----
    for (auto& e : queue) {
        if (e.cycles_remaining > 0) e.cycles_remaining--;
    }

    //  3. FINISH: front entry, if counter hit 0 ----
    if (queue.empty() || queue.front().cycles_remaining != 0) return;

    LSQEntry& e = queue.front();
    int addr = e.Vj + e.imm;

    has_result = true;
    bcast_rob_idx = e.rob_idx;
    bcast_instr_pc = e.instr_pc;
    bcast_mem_addr = addr;

    if (addr < 0 || addr >= (int)Memory.size()) {
        has_exception = true;
        bcast_value = 0;
    } else if (e.op == OpCode::LW) {
        // Default: read from memory. Check for store-to-load forwarding.
        int val = Memory[addr];
        int best_pc = -1;
        for (auto& r : ROB) {
            if (!r.busy || r.op != OpCode::SW || !r.ready) continue;
            if (r.mem_addr != addr) continue;
            if (r.instr_pc >= e.instr_pc) continue;   // must be older
            if (r.instr_pc > best_pc) {
                best_pc = r.instr_pc;
                val = r.store_value;
            }
        }
        bcast_value = val;
    } else {
        // SW: carry the store value on the broadcast so the ROB gets it.
        bcast_value = e.Vk;
    }

    queue.erase(queue.begin());
}
};