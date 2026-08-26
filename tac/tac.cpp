#include "tac.h"

#include <cctype>
#include <fstream>

using namespace std;

vector<string> split_line(const string& line) {
    vector<string> parts;
    string word;

    for(char c : line) {
        if(c=='|') {
            parts.push_back(word);
            word.clear();
        }
        else word +=c;
    }
    parts.push_back(word);
    return parts;
}

bool is_integer(const string &text) {
    if(text.empty()) return false;
    for(char c : text)
        if(!isdigit((unsigned char)c)) return false;
    return true;
}

bool read_tac(const string &fileName, vector<Instruction>& code, string& error) {
    ifstream file(fileName);
    if(!file) {
        error="cannot open input file";
        return false;
    }

    code.clear();
    string line;
    int fileLine=0;

    while(getline(file,line)) {
        fileLine++;
        if(line.empty() || line[0]=='#') continue;

        vector<string> p=split_line(line);
        if(p.size()!=7 || !is_integer(p[0]) || !is_integer(p[1])) {
            error="invalid TAC at file line " +to_string(fileLine);
            return false;
        }

        Instruction x;
        x.number=stoi(p[0]);
        x.line =stoi(p[1]);
        x.opcode=p[2];
        x.type =p[3];
        x.destination=p[4];
        x.argument1 =p[5];
        x.argument2=p[6];
        code.push_back(x);
    }

    return true;
}

bool write_tac(const string& fileName, const vector<Instruction> &code, string &error) {
    ofstream file(fileName);
    if(!file) {
        error="cannot open output file";
        return false;
    }

    file<<"# TINYC_TAC_V1\n";
    file<<"# instruction|line|opcode|type|destination|argument1|argument2\n";

    for(const Instruction &x : code) {
        file<<x.number<<'|'<<x.line<<'|'<<x.opcode<<'|'<<x.type<<'|';
        file<<x.destination<<'|'<<x.argument1<<'|'<<x.argument2<<'\n';
    }
    return true;
}

bool is_constant(const string& value) {
    return !value.empty() && value[0]=='#';
}

bool is_temporary(const string &value) {
    if(value.size()<2 || value[0]!='t') return false;
    return is_integer(value.substr(1));
}

bool is_label(const string& value) {
    if(value.size()<2 || value[0]!='L') return false;
    return is_integer(value.substr(1));
}
