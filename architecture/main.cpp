#include "architecture.h"

#include <cctype>
#include <iostream>

using namespace std;

bool is_number(const string& text) {
    if(text.empty()) return false;
    for(char c : text)
        if(!isdigit((unsigned char)c)) return false;
    return true;
}

void usage() {
    cout<<"Usage: ./architecture/architecture_info [scalar-registers vector-registers vector-width]\n";
}

int main(int argc, char *argv[]) {
    if(argc==2 && string(argv[1])=="--help") {
        usage();
        return 0;
    }
    if(argc!=1 && argc!=4) {
        usage();
        return 1;
    }

    Architecture arch;
    string error;
    if(!load_architecture("architecture/config.txt",arch,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    if(argc==4) {
        if(!is_number(argv[1]) || !is_number(argv[2]) || !is_number(argv[3]) ||
           !arch.set_configuration(stoi(argv[1]),stoi(argv[2]),stoi(argv[3]))) {
            cerr<<"Error: register counts must be at least 2 and vector width must be 128, 256 or 512\n";
            usage();
            return 1;
        }
    }

    cout<<"Processor configuration\n";
    cout<<"Scalar registers: "<<arch.scalarRegisters<<'\n';
    cout<<"Vector registers: "<<arch.vectorRegisters<<'\n';
    cout<<"Vector width: "<<arch.vectorWidth<<" bits\n";
    cout<<"32-bit vector lanes: "<<arch.vector_lanes("i32")<<'\n';
    cout<<"64-bit vector lanes: "<<arch.vector_lanes("i64")<<'\n';
    cout<<"Memory: "<<arch.memorySize<<" bytes\n";
    cout<<"Execution: in-order, single-issue\n";
    return 0;
}
