#include "reg_allocator.h"

#include <climits>
#include <cctype>
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

string output_name(const string& input) {
    int pos=(int)input.find_last_of("/\\");
    if(pos<0) return "allocated_tac.txt";
    return input.substr(0,pos+1)+"allocated_tac.txt";
}

void usage() {
    cout<<"Usage: ./reg_allocator/reg_allocator <simd_tac.txt> [scalar-registers vector-registers vector-width]\n";
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
            cerr<<"Error: register counts must be at least 2 and vector width must be 128, 256 or 512\n";
            return 1;
        }
    }

    vector<Instruction> input, output;
    if(!read_tac(argv[1],input,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    int spillLoads=0, spillStores=0;
    if(!allocate_registers(input,arch,output,spillLoads,spillStores,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    string fileName=output_name(argv[1]);
    if(!write_tac(fileName,output,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    cout<<"Register allocation complete\n";
    cout<<"Instructions: "<<output.size()<<'\n';
    cout<<"Spill loads: "<<spillLoads<<'\n';
    cout<<"Spill stores: "<<spillStores<<'\n';
    cout<<"Output: "<<fileName<<'\n';
    return 0;
}
