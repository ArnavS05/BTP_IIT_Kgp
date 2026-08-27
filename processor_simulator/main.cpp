#include "architecture.h"
#include "tac.h"

#include <cctype>
#include <climits>
#include <iostream>

using namespace std;

bool read_number(const string& text, int& answer) {
    if(text.empty()) return false;

    long long value=0;
    for(char c : text) {
        if(!isdigit((unsigned char)c)) return false;
        value=value*10 +(c-'0');
        if(value>INT_MAX) return false;
    }
    answer=(int)value;
    return true;
}

void usage() {
    cout<<"Usage: ./processor_simulator/processor_simulator <allocated_tac.txt> "
        <<"[scalar-registers vector-registers vector-width]\n";
}

int main(int argc, char *argv[]) {
    if(argc==2 && string(argv[1])=="--help") {
        usage();
        return 0;
    }
    if(argc!=2 && argc!=5) {
        usage();
        return 1;
    }

    Architecture arch;
    string error;
    if(!load_architecture("architecture/config.txt",arch,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    if(argc==5) {
        int scalarRegs, vectorRegs, width;
        if(!read_number(argv[2],scalarRegs) || !read_number(argv[3],vectorRegs) ||
           !read_number(argv[4],width) ||
           !arch.set_configuration(scalarRegs,vectorRegs,width)) {
            cerr<<"Error: invalid processor configuration\n";
            return 1;
        }
    }

    vector<Instruction> code;
    if(!read_tac(argv[1],code,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    cout<<"Processor simulator is not implemented yet.\n";
    return 0;
}
