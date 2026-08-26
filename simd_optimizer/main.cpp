#include "simd_optimizer.h"

#include <cctype>
#include <iostream>

using namespace std;

bool is_number(const string& text) {
    if(text.empty()) return false;
    for(char c : text)
        if(!isdigit((unsigned char)c)) return false;
    return true;
}

string output_name(const string &input) {
    int pos=(int)input.find_last_of("/\\");
    if(pos<0) return "simd_tac.txt";
    return input.substr(0,pos+1)+"simd_tac.txt";
}

void usage() {
    cout<<"Usage: ./simd_optimizer/simd_optimizer <scalar_tac.txt> [vector-width]\n";
}

int main(int argc, char *argv[]) {
    if(argc==2 && string(argv[1])=="--help") {
        usage();
        return 0;
    }
    if(argc!=2 && argc!=3) {
        usage();
        return 1;
    }

    Architecture arch;
    string error;
    if(!load_architecture("architecture/config.txt",arch,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    if(argc==3) {
        if(!is_number(argv[2]) ||
           !arch.set_configuration(arch.scalarRegisters,arch.vectorRegisters,stoi(argv[2]))) {
            cerr<<"Error: vector width must be 128, 256 or 512\n";
            return 1;
        }
    }

    vector<Instruction> scalarCode;
    if(!read_tac(argv[1],scalarCode,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    vector<Instruction> simdCode=optimize_simd(scalarCode,arch);
    string output=output_name(argv[1]);
    if(!write_tac(output,simdCode,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    cout<<"SIMD optimization complete\n";
    cout<<"Instructions: "<<simdCode.size()<<'\n';
    cout<<"Output: "<<output<<'\n';
    return 0;
}
