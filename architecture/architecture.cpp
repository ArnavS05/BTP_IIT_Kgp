#include "architecture.h"

#include <fstream>

using namespace std;

Architecture::Architecture() {
    scalarRegisters=16;
    vectorRegisters =16;
    vectorWidth=256;
    memorySize =1048576;
    scalarUnits=1;
    vectorUnits =1;

    integerAddLatency=1;
    integerMultiplyLatency =3;
    integerDivideLatency=10;
    floatAddLatency =3;
    floatMultiplyLatency=4;
    floatDivideLatency =10;
    memoryLatency=4;
    castLatency =2;
    branchLatency=1;
}

bool Architecture::set_configuration(int scalarRegs, int vectorRegs, int width) {
    if(scalarRegs<2 || vectorRegs<2) return false;
    if(width!=128 && width!=256 && width!=512) return false;

    scalarRegisters=scalarRegs;
    vectorRegisters =vectorRegs;
    vectorWidth=width;
    return true;
}

int Architecture::vector_lanes(const string& type) const {
    if(type=="i32" || type=="f32") return vectorWidth/32;
    if(type=="i64" || type=="f64") return vectorWidth/64;
    return 0;
}

int Architecture::instruction_latency(const string &opcode, const string& type) const {
    if(opcode=="VBROADCAST" || opcode.rfind("VCAST_",0)==0) return castLatency;
    if(opcode=="VREDUCE_ADD") return integerAddLatency;
    if(opcode=="LOAD" || opcode=="STORE" || opcode=="VLOAD" || opcode=="VSTORE")
        return memoryLatency;
    if(opcode.rfind("CAST_",0)==0) return castLatency;
    if(opcode=="VDIV_CHECK" || opcode=="JMP" || opcode.rfind("BR_",0)==0)
        return branchLatency;
    if(opcode=="HALT") return 1;

    bool floating=type=="f32" || type=="f64";
    if(opcode=="MUL" || opcode=="VMUL")
        return floating ? floatMultiplyLatency : integerMultiplyLatency;
    if(opcode=="DIV" || opcode=="REM" || opcode=="VDIV")
        return floating ? floatDivideLatency : integerDivideLatency;
    if(opcode=="ADD" || opcode=="SUB" || opcode=="VADD" || opcode=="VSUB")
        return floating ? floatAddLatency : integerAddLatency;

    return 1;
}

bool load_architecture(const string& fileName, Architecture &arch, string& error) {
    ifstream file(fileName);
    if(!file) {
        error="cannot open architecture configuration";
        return false;
    }

    string name;
    int value;
    while(file>>name>>value) {
        if(name=="scalar_registers") arch.scalarRegisters=value;
        else if(name=="vector_registers") arch.vectorRegisters =value;
        else if(name=="vector_width") arch.vectorWidth=value;
        else if(name=="memory_size") arch.memorySize =value;
        else if(name=="scalar_units") arch.scalarUnits=value;
        else if(name=="vector_units") arch.vectorUnits =value;
        else if(name=="integer_add_latency") arch.integerAddLatency=value;
        else if(name=="integer_multiply_latency") arch.integerMultiplyLatency =value;
        else if(name=="integer_divide_latency") arch.integerDivideLatency=value;
        else if(name=="float_add_latency") arch.floatAddLatency =value;
        else if(name=="float_multiply_latency") arch.floatMultiplyLatency=value;
        else if(name=="float_divide_latency") arch.floatDivideLatency =value;
        else if(name=="memory_latency") arch.memoryLatency=value;
        else if(name=="cast_latency") arch.castLatency =value;
        else if(name=="branch_latency") arch.branchLatency=value;
    }

    if(!arch.set_configuration(arch.scalarRegisters,arch.vectorRegisters,arch.vectorWidth)) {
        error="invalid register count or vector width";
        return false;
    }
    return true;
}
