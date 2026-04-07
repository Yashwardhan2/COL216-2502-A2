#include "preprocessor.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <iostream>
using namespace std;

// this is used to trim spaces 
static inline string ltrim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    return (start == string::npos) ? string() : s.substr(start);
}
static inline string rtrim(const string &s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return (end == string::npos) ? string() : s.substr(0, end+1);
}
static inline string trim(const string &s) {
    return rtrim(ltrim(s));
}

// to remove comment, we can simply find the first occurrence of '#' and cut the string there
string remove_comment(const string& line) {
    size_t pos = line.find('#');
    if (pos != string::npos) {
        return line.substr(0, pos);
    }
    return line;
}

// replace commas with spaces so tokenizer treats them as separators
static inline string strip_commas(const string& s) {
    string out = s;
    for (auto &c : out) if (c == ',') c = ' ';
    return out;
}

string preprocess(const string& input_file) {

    ifstream fin(input_file);
    if (!fin.is_open()) {
        cout << "Error: Cannot open file " << input_file << endl;
        return "";
    }
    string line;
    unordered_map<string, int> mem_label;
    unordered_map<string, int> code_label;
    vector<int> memory;
    vector<string> instructions;
    int mem_ptr = 0;
    int instr_ptr = 0;
    
    while (getline(fin, line)) {
        //first read all lines, remove comments and trim
        line = remove_comment(line);
        line = trim(line);
        if (line.empty()) {
            continue; // skip empty lines
        }

        //if it is memory label i.e  (.A: 1 2 3)
        if (line[0] == '.') {
                size_t colon = line.find(':');
                if (colon == string::npos) {
                    cout << "Error:  memory label (missing ':') -> " << line << endl;
                    continue;
                }
                string label = line.substr(1, colon - 1);
                mem_label[label] = mem_ptr;
                string rest = line.substr(colon + 1);
                istringstream iss(rest);
                int val;
                while (iss >> val) {
                    memory.push_back(val);
                    mem_ptr++;
                }
        }

        //if it is like loop:(works for any type of code label)
        else if (line.back() == ':') {
                string label = line.substr(0, line.size() - 1);
                code_label[label] = instr_ptr;
        }

        // instruction
        else {
                instructions.push_back(line);
                instr_ptr++;
        }
    }
    fin.close();


    
    
    for (int idx = 0; idx < (int)instructions.size(); idx++) {
        string inst = strip_commas(instructions[idx]);// convert commas into spcaces for easier tokenization
        stringstream ss(inst);
        vector<string> tokens;
        string word;
        while (ss >> word) {
            // case 1: `label(reg)` form like B(x1) then  replace label with its address
            size_t lparen = word.find('(');
            if (lparen != string::npos && lparen > 0) {
                string label = word.substr(0, lparen);
                string rest = word.substr(lparen); // includes "(x1)"
                if (mem_label.count(label)) {
                    word = to_string(mem_label[label]) + rest;
                }
            }
            // case 2: bare token is a memory label (e.g. addi x1, x0, A)
            else if (mem_label.count(word)) {
                word = to_string(mem_label[word]);
            }
            // case 3: bare token is a code label (branch target) then  store as pc-relative  offset
            else if (code_label.count(word)) {
                int offset = code_label[word] - idx;
                word = to_string(offset);
            }
            tokens.push_back(word);
        }
        // rebuild line
        string new_inst = "";
        for (int i = 0; i < (int)tokens.size(); i++) {
            if (i) new_inst += " ";
            new_inst += tokens[i];
        }
        instructions[idx] = new_inst;
    }

    // write output back to the same file (asked in the prompt to return the filename, so we assume in-place modification)
    ofstream fout(input_file);

    // first line is memory data, the rest are instructions
    for (int i = 0; i < (int)memory.size(); i++) {
        fout << memory[i];
        if (i + 1 < (int)memory.size()) fout << " ";
    }
    fout << "\n";

    // write instructions
    for (auto &inst : instructions) {
        fout << inst << "\n";
    }

    fout.close();

    return input_file;
}