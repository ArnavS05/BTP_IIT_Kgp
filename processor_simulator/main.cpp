#include "simulator.h"

#include <cctype>
#include <climits>
#include <iostream>

using namespace std;

bool read_number(const string& text, int& answer) {
    if(text.empty()) return false;

    long long value=0;
    for(char c : text) {
        if(!isdigit((unsigned char)c)) return false;
        int digit=c-'0';
        if(value>(INT_MAX-digit)/10) return false;
        value=value*10 +digit;
    }
    answer=(int)value;
    return true;
}

string output_name(const string& input) {
    int pos=(int)input.find_last_of("/\\");
    if(pos<0) return "simulation_report.txt";
    return input.substr(0,pos+1)+"simulation_report.txt";
}

string shown_name(const string& path) {
    int pos=(int)path.find("output_");
    if(pos>=0) return path.substr(pos);

    pos=(int)path.find_last_of("/\\");
    if(pos<0) return path;
    return path.substr(pos+1);
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

    SimulationResult result;
    if(!simulate_program(code,arch,result,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    string output=output_name(argv[1]);
    if(!write_simulation_report(output,arch,result,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    cout<<"Processor simulation complete\n";
    cout<<"Instructions executed: "<<result.totalInstructions<<'\n';
    cout<<"Total cycles: "<<result.totalCycles<<'\n';
    cout<<"Output: "<<shown_name(output)<<'\n';
    return 0;
}
