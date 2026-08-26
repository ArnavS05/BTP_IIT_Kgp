#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#include <string>

using namespace std;

class Architecture {
public:
    int scalarRegisters;
    int vectorRegisters;
    int vectorWidth;
    int memorySize;
    int scalarUnits;
    int vectorUnits;

    int integerAddLatency;
    int integerMultiplyLatency;
    int integerDivideLatency;
    int floatAddLatency;
    int floatMultiplyLatency;
    int floatDivideLatency;
    int memoryLatency;
    int castLatency;
    int branchLatency;

    Architecture();

    bool set_configuration(int scalarRegs, int vectorRegs, int width);
    int vector_lanes(const string& type) const;
    int instruction_latency(const string &opcode, const string& type) const;
};

bool load_architecture(const string& fileName, Architecture &arch, string& error);

#endif
