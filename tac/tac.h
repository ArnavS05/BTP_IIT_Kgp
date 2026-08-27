#ifndef TAC_H
#define TAC_H

#include <string>
#include <vector>

using namespace std;

class Instruction {
public:
    int number;
    int line;
    string opcode;
    string type;
    string destination;
    string argument1;
    string argument2;
};

bool read_tac(const string &fileName, vector<Instruction>& code, string& error);
bool write_tac(const string& fileName, const vector<Instruction> &code, string &error);

bool is_constant(const string& value);
bool is_temporary(const string &value);
bool is_vector_temporary(const string& value);
bool is_label(const string& value);

#endif
