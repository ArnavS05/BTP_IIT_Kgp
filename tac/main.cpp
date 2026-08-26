#include "tac.h"

#include <iostream>

using namespace std;

int main(int argc, char *argv[]) {
    if(argc!=2 || string(argv[1])=="--help") {
        cout<<"Usage: ./tac/tac_reader <scalar_tac.txt>\n";
        return argc==2 ? 0 : 1;
    }

    vector<Instruction> code;
    string error;

    if(!read_tac(argv[1],code,error)) {
        cerr<<"Error: "<<error<<'\n';
        return 1;
    }

    cout<<"TAC parsed successfully\n";
    cout<<"Instructions: "<<code.size()<<'\n';
    return 0;
}
