#include "simulator.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;

const long long MAX_EXECUTED_INSTRUCTIONS=1000000;

class Value {
public:
    string type;
    long long integerValue;
    double floatValue;
    bool valid;

    Value() {
        type="";
        integerValue=0;
        floatValue=0;
        valid=false;
    }
};

LineResult::LineResult() {
    instructions=0;
    cycles=0;
    scalarInstructions=0;
    vectorInstructions=0;
}

SimulationResult::SimulationResult() {
    totalInstructions=0;
    totalCycles=0;
    stallCycles=0;
    scalarInstructions=0;
    vectorInstructions=0;
    controlInstructions=0;
    memoryOperations=0;
    branchesTaken=0;
    branchesNotTaken=0;
    spillLoads=0;
    spillStores=0;
    scalarEquivalentCycles=0;
    memoryChecksum=0;
}

bool is_type(const string& type) {
    return type=="i32" || type=="i64" || type=="f32" || type=="f64";
}

bool is_float_type(const string& type) {
    return type=="f32" || type=="f64";
}

int type_bytes(const string& type) {
    if(type=="i32" || type=="f32") return 4;
    if(type=="i64" || type=="f64") return 8;
    return 0;
}

bool read_unsigned_number(const string& text, int& answer) {
    if(text.empty()) return false;

    long long value=0;
    for(char c : text) {
        if(c<'0' || c>'9') return false;
        int digit=c-'0';
        if(value>(2147483647-digit)/10) return false;
        value=value*10 +digit;
    }
    answer=(int)value;
    return true;
}

bool register_number(const string& text, char prefix, int count, int& index) {
    if(text.size()<2 || text[0]!=prefix) return false;
    if(!read_unsigned_number(text.substr(1),index)) return false;
    return index>=0 && index<count;
}

bool is_spill(const string& text) {
    int number;
    if(text.size()<2 || text[0]!='s') return false;
    return read_unsigned_number(text.substr(1),number);
}

Value integer_value(long long number, const string& type) {
    Value value;
    value.type=type;
    if(type=="i32") {
        uint32_t bits=(uint32_t)number;
        if(bits<=2147483647U) value.integerValue=bits;
        else value.integerValue=(long long)bits-4294967296LL;
    }
    else value.integerValue=number;
    value.floatValue=(double)value.integerValue;
    value.valid=true;
    return value;
}

Value floating_value(double number, const string& type) {
    Value value;
    value.type=type;
    if(type=="f32") value.floatValue=(float)number;
    else value.floatValue=number;
    value.integerValue=0;
    value.valid=true;
    return value;
}

class Simulator {
public:
    const vector<Instruction>& code;
    const Architecture& arch;
    vector<Value> scalarRegisters;
    vector<vector<Value>> vectorRegisters;
    vector<unsigned char> memory;
    map<string,vector<unsigned char>> spillMemory;
    map<int,int> position;   // TAC instruction number -> index inside the code vector
    SimulationResult result;
    string error;
    int pc;
    bool halted;

    Simulator(const vector<Instruction>& input, const Architecture& machine)
        : code(input), arch(machine) {
        scalarRegisters.resize(arch.scalarRegisters);
        vectorRegisters.resize(arch.vectorRegisters);
        memory.resize(arch.memorySize,0);
        pc=0;
        halted=false;
    }

    bool fail(const string& message) {
        if(pc>=0 && pc<(int)code.size())
            error="instruction " +to_string(code[pc].number)+": "+message;
        else error=message;
        return false;
    }

    // Make instruction number lookup.
    bool make_positions() {
        if(code.empty()) {
            error="empty TAC";
            return false;
        }

        for(int i=0;i<(int)code.size();i++) {
            if(position.find(code[i].number)!=position.end()) {
                error="duplicate instruction number "+to_string(code[i].number);
                return false;
            }
            position[code[i].number]=i;

            if(is_temporary(code[i].destination) ||
               is_temporary(code[i].argument1) ||
               is_temporary(code[i].argument2)) {
                error="virtual temporary remains at instruction "+to_string(code[i].number);
                return false;
            }
        }
        return true;
    }

    // Read a constant value.
    bool immediate(const string& text, const string& type, Value& value) {
        if(text.size()<2 || text[0]!='#') return fail("invalid immediate "+text);

        istringstream stream(text.substr(1));
        if(is_float_type(type)) {
            double number;
            stream>>number;
            if(!stream || !stream.eof()) return fail("invalid immediate "+text);
            value=floating_value(number,type);
        }
        else {
            long long number;
            stream>>number;
            if(!stream || !stream.eof()) return fail("invalid immediate "+text);
            value=integer_value(number,type);
        }
        return true;
    }

    // Get one scalar operand.
    bool scalar_value(const string& text, const string& type, Value& value) {
        if(!is_type(type)) return fail("invalid type "+type);
        if(!text.empty() && text[0]=='#') return immediate(text,type,value);

        int index;
        if(!register_number(text,'r',arch.scalarRegisters,index))
            return fail("invalid scalar operand "+text);
        if(!scalarRegisters[index].valid)
            return fail("uninitialized register "+text);
        if(scalarRegisters[index].type!=type)
            return fail("type mismatch for register "+text);

        value=scalarRegisters[index];
        return true;
    }

    // Get all vector lanes.
    bool vector_value(const string& text, const string& type,
                      vector<Value>& values) {
        int lanes=arch.vector_lanes(type);
        if(lanes<=0) return fail("invalid vector type "+type);

        if(!text.empty() && text[0]=='#') {
            Value value;
            if(!immediate(text,type,value)) return false;
            values.assign(lanes,value);
            return true;
        }

        int index;
        if(!register_number(text,'v',arch.vectorRegisters,index))
            return fail("invalid vector operand "+text);
        if((int)vectorRegisters[index].size()!=lanes)
            return fail("uninitialized vector register "+text);

        for(Value value : vectorRegisters[index])
            if(!value.valid || value.type!=type)
                return fail("type mismatch for vector register "+text);

        values=vectorRegisters[index];
        return true;
    }

    // Read a memory address.
    bool address_value(const string& text, long long& address) {
        if(!text.empty() && text[0]=='#') {
            Value value;
            if(!immediate(text,"i64",value)) return false;
            address=value.integerValue;
        }
        else {
            int index;
            if(!register_number(text,'r',arch.scalarRegisters,index))
                return fail("invalid memory address "+text);
            if(!scalarRegisters[index].valid)
                return fail("uninitialized address register "+text);
            if(is_float_type(scalarRegisters[index].type))
                return fail("floating-point memory address in "+text);
            address=scalarRegisters[index].integerValue;
        }
        return true;
    }

    // Load raw bytes from memory.
    bool read_bytes(const string& place, int count, vector<unsigned char>& bytes) {
        if(is_spill(place)) {
            map<string,vector<unsigned char>>::iterator found=spillMemory.find(place);
            if(found==spillMemory.end()) return fail("uninitialized spill location "+place);
            if((int)found->second.size()!=count)
                return fail("wrong spill size for "+place);
            bytes=found->second;
            return true;
        }

        long long address;
        if(!address_value(place,address)) return false;
        if(address<0 || address>(long long)memory.size()-count)
            return fail("memory access is outside the configured memory");

        bytes.assign(memory.begin()+address,memory.begin()+address+count);
        return true;
    }

    // Store raw bytes in memory.
    bool write_bytes(const string& place, const vector<unsigned char>& bytes) {
        if(is_spill(place)) {
            spillMemory[place]=bytes;
            return true;
        }

        long long address;
        if(!address_value(place,address)) return false;
        if(address<0 || address>(long long)memory.size()-(long long)bytes.size())
            return fail("memory access is outside the configured memory");

        for(int i=0;i<(int)bytes.size();i++) memory[address+i]=bytes[i];
        return true;
    }

    // Convert a value to bytes.
    vector<unsigned char> value_bytes(const Value& value) {
        vector<unsigned char> bytes(type_bytes(value.type));
        if(value.type=="i32") {
            int32_t number=(int32_t)value.integerValue;
            memcpy(bytes.data(),&number,4);
        }
        else if(value.type=="i64") {
            int64_t number=(int64_t)value.integerValue;
            memcpy(bytes.data(),&number,8);
        }
        else if(value.type=="f32") {
            float number=(float)value.floatValue;
            memcpy(bytes.data(),&number,4);
        }
        else {
            double number=value.floatValue;
            memcpy(bytes.data(),&number,8);
        }
        return bytes;
    }

    // Convert bytes back to a value.
    Value bytes_value(const vector<unsigned char>& bytes, int start,
                      const string& type) {
        if(type=="i32") {
            int32_t number;
            memcpy(&number,bytes.data()+start,4);
            return integer_value(number,type);
        }
        if(type=="i64") {
            int64_t number;
            memcpy(&number,bytes.data()+start,8);
            return integer_value(number,type);
        }
        if(type=="f32") {
            float number;
            memcpy(&number,bytes.data()+start,4);
            return floating_value(number,type);
        }

        double number;
        memcpy(&number,bytes.data()+start,8);
        return floating_value(number,type);
    }

    bool size_field(const string& text, int expected) {
        int size;
        if(!read_unsigned_number(text,size) || size!=expected)
            return fail("invalid load or store size "+text);
        return true;
    }

    bool scalar_destination(const string& text, int& index) {
        if(!register_number(text,'r',arch.scalarRegisters,index))
            return fail("invalid scalar destination "+text);
        return true;
    }

    bool vector_destination(const string& text, int& index) {
        if(!register_number(text,'v',arch.vectorRegisters,index))
            return fail("invalid vector destination "+text);
        return true;
    }

    // Execute a scalar load.
    bool scalar_load(const Instruction& x) {
        if(!is_type(x.type)) return fail("invalid type "+x.type);
        int destination;
        if(!scalar_destination(x.destination,destination)) return false;
        if(!size_field(x.argument2,type_bytes(x.type))) return false;

        vector<unsigned char> bytes;
        if(!read_bytes(x.argument1,type_bytes(x.type),bytes)) return false;
        scalarRegisters[destination]=bytes_value(bytes,0,x.type);

        result.memoryOperations++;
        if(is_spill(x.argument1)) result.spillLoads++;
        return true;
    }

    // Execute a scalar store.
    bool scalar_store(const Instruction& x) {
        if(!is_type(x.type)) return fail("invalid type "+x.type);
        if(!size_field(x.argument2,type_bytes(x.type))) return false;

        Value value;
        if(!scalar_value(x.argument1,x.type,value)) return false;
        if(!write_bytes(x.destination,value_bytes(value))) return false;

        result.memoryOperations++;
        if(is_spill(x.destination)) result.spillStores++;
        return true;
    }

    // Execute a vector load.
    bool vector_load(const Instruction& x) {
        int lanes=arch.vector_lanes(x.type);
        if(lanes<=0) return fail("invalid vector type "+x.type);
        int destination;
        if(!vector_destination(x.destination,destination)) return false;

        int bytesPerValue=type_bytes(x.type);
        int totalBytes=bytesPerValue*lanes;
        if(!size_field(x.argument2,totalBytes)) return false;

        vector<unsigned char> bytes;
        if(!read_bytes(x.argument1,totalBytes,bytes)) return false;

        vector<Value> values;
        for(int i=0;i<lanes;i++)
            values.push_back(bytes_value(bytes,i*bytesPerValue,x.type));
        vectorRegisters[destination]=values;

        result.memoryOperations++;
        if(is_spill(x.argument1)) result.spillLoads++;
        return true;
    }

    // Execute a vector store.
    bool vector_store(const Instruction& x) {
        int lanes=arch.vector_lanes(x.type);
        if(lanes<=0) return fail("invalid vector type "+x.type);
        int totalBytes=type_bytes(x.type)*lanes;
        if(!size_field(x.argument2,totalBytes)) return false;

        vector<Value> values;
        if(!vector_value(x.argument1,x.type,values)) return false;

        vector<unsigned char> bytes;
        for(Value value : values) {
            vector<unsigned char> part=value_bytes(value);
            bytes.insert(bytes.end(),part.begin(),part.end());
        }
        if(!write_bytes(x.destination,bytes)) return false;

        result.memoryOperations++;
        if(is_spill(x.destination)) result.spillStores++;
        return true;
    }

    // Do integer arithmetic safely.
    bool integer_operation(const string& opcode, const Value& a,
                           const Value& b, const string& type, Value& answer) {
        long long x=a.integerValue;
        long long y=b.integerValue;

        if((opcode=="DIV" || opcode=="REM") && y==0)
            return fail("division by zero");
        bool i32Overflow=type=="i32" && x==numeric_limits<int32_t>::min();
        bool i64Overflow=type=="i64" && x==numeric_limits<int64_t>::min();
        if((opcode=="DIV" || opcode=="REM") && y==-1 &&
           (i32Overflow || i64Overflow))
            return fail("integer division overflow");

        if(opcode=="DIV") answer=integer_value(x/y,type);
        else if(opcode=="REM") answer=integer_value(x%y,type);
        else if(type=="i32") {
            uint32_t left=(uint32_t)(int32_t)x;
            uint32_t right=(uint32_t)(int32_t)y;
            uint32_t value=0;
            if(opcode=="ADD") value=left+right;
            else if(opcode=="SUB") value=left-right;
            else if(opcode=="MUL") value=left*right;
            else return fail("invalid arithmetic opcode "+opcode);
            answer=integer_value((int32_t)value,type);
        }
        else {
            uint64_t left=(uint64_t)x;
            uint64_t right=(uint64_t)y;
            uint64_t value=0;
            if(opcode=="ADD") value=left+right;
            else if(opcode=="SUB") value=left-right;
            else if(opcode=="MUL") value=left*right;
            else return fail("invalid arithmetic opcode "+opcode);
            int64_t signedValue;
            memcpy(&signedValue,&value,8);
            answer=integer_value(signedValue,type);
        }
        return true;
    }

    // Do arithmetic for one value.
    bool arithmetic_value(const string& opcode, const Value& a,
                          const Value& b, const string& type, Value& answer) {
        if(!is_float_type(type))
            return integer_operation(opcode,a,b,type,answer);

        double x=a.floatValue;
        double y=b.floatValue;
        if(opcode=="REM") return fail("floating-point remainder is not supported");
        if(opcode=="DIV" && y==0) return fail("division by zero");

        double value=0;
        if(opcode=="ADD") value=x+y;
        else if(opcode=="SUB") value=x-y;
        else if(opcode=="MUL") value=x*y;
        else if(opcode=="DIV") value=x/y;
        else return fail("invalid arithmetic opcode "+opcode);

        answer=floating_value(value,type);
        return true;
    }

    // Execute scalar arithmetic.
    bool scalar_arithmetic(const Instruction& x) {
        if(!is_type(x.type)) return fail("invalid type "+x.type);

        Value a,b,answer;
        if(!scalar_value(x.argument1,x.type,a)) return false;
        if(!scalar_value(x.argument2,x.type,b)) return false;
        if(!arithmetic_value(x.opcode,a,b,x.type,answer)) return false;

        int destination;
        if(!scalar_destination(x.destination,destination)) return false;
        scalarRegisters[destination]=answer;
        return true;
    }

    // Execute arithmetic lane by lane.
    bool vector_arithmetic(const Instruction& x) {
        if(!is_type(x.type)) return fail("invalid type "+x.type);

        vector<Value> a,b,answer;
        if(!vector_value(x.argument1,x.type,a)) return false;
        if(!vector_value(x.argument2,x.type,b)) return false;

        string opcode=x.opcode.substr(1);
        for(int i=0;i<(int)a.size();i++) {
            Value value;
            if(!arithmetic_value(opcode,a[i],b[i],x.type,value)) return false;
            answer.push_back(value);
        }

        int destination;
        if(!vector_destination(x.destination,destination)) return false;
        vectorRegisters[destination]=answer;
        return true;
    }

    string lower_type(const string& text) {
        if(text=="I32") return "i32";
        if(text=="I64") return "i64";
        if(text=="F32") return "f32";
        if(text=="F64") return "f64";
        return "";
    }

    // Read both types from a cast.
    bool cast_types(const string& opcode, string& from, string& to) {
        if(opcode.rfind("CAST_",0)!=0) return false;
        string rest=opcode.substr(5);
        int split=(int)rest.find('_');
        if(split<0) return false;
        from=lower_type(rest.substr(0,split));
        to=lower_type(rest.substr(split+1));
        return !from.empty() && !to.empty();
    }

    // Convert one numeric value.
    bool cast_value(const Value& source, const string& to, Value& answer) {
        if(!is_float_type(source.type) && !is_float_type(to)) {
            answer=integer_value(source.integerValue,to);
            return true;
        }
        if(!is_float_type(source.type) && is_float_type(to)) {
            answer=floating_value((double)source.integerValue,to);
            return true;
        }
        if(is_float_type(source.type) && is_float_type(to)) {
            answer=floating_value(source.floatValue,to);
            return true;
        }

        long double value=source.floatValue;
        if(!isfinite((double)value)) return fail("invalid floating-point to integer cast");
        value=trunc(value);
        if(to=="i32" && (value<numeric_limits<int32_t>::min() ||
                         value>numeric_limits<int32_t>::max()))
            return fail("floating-point cast is outside i32 range");
        if(to=="i64" && (value<numeric_limits<int64_t>::min() ||
                         value>numeric_limits<int64_t>::max()))
            return fail("floating-point cast is outside i64 range");

        answer=integer_value((long long)value,to);
        return true;
    }

    // Execute a cast instruction.
    bool cast_instruction(const Instruction& x) {
        string from,to;
        if(!cast_types(x.opcode,from,to)) return fail("invalid cast opcode "+x.opcode);
        if(to!=x.type) return fail("cast result type does not match TAC type");

        Value source,answer;
        if(!scalar_value(x.argument1,from,source)) return false;
        if(!cast_value(source,to,answer)) return false;

        int destination;
        if(!scalar_destination(x.destination,destination)) return false;
        scalarRegisters[destination]=answer;
        return true;
    }

    // Compare two scalar values.
    bool comparison(const string& opcode, const Value& a, const Value& b) {
        if(is_float_type(a.type)) {
            if(opcode=="BR_EQ") return a.floatValue==b.floatValue;
            if(opcode=="BR_NE") return a.floatValue!=b.floatValue;
            if(opcode=="BR_LT") return a.floatValue<b.floatValue;
            if(opcode=="BR_LE") return a.floatValue<=b.floatValue;
            if(opcode=="BR_GT") return a.floatValue>b.floatValue;
            return a.floatValue>=b.floatValue;
        }

        if(opcode=="BR_EQ") return a.integerValue==b.integerValue;
        if(opcode=="BR_NE") return a.integerValue!=b.integerValue;
        if(opcode=="BR_LT") return a.integerValue<b.integerValue;
        if(opcode=="BR_LE") return a.integerValue<=b.integerValue;
        if(opcode=="BR_GT") return a.integerValue>b.integerValue;
        return a.integerValue>=b.integerValue;
    }

    // Move to a TAC label.
    bool jump_to(const string& label) {
        if(label.size()<2 || label[0]!='L') return fail("invalid jump target "+label);
        int number;
        if(!read_unsigned_number(label.substr(1),number))
            return fail("invalid jump target "+label);
        map<int,int>::iterator found=position.find(number);
        if(found==position.end()) return fail("missing jump target "+label);
        pc=found->second;
        return true;
    }

    // Execute a conditional branch.
    bool branch_instruction(const Instruction& x) {
        if(!is_type(x.type)) return fail("invalid branch type "+x.type);

        Value a,b;
        if(!scalar_value(x.argument1,x.type,a)) return false;
        if(!scalar_value(x.argument2,x.type,b)) return false;

        bool taken=comparison(x.opcode,a,b);
        if(taken) {
            result.branchesTaken++;
            return jump_to(x.destination);
        }

        result.branchesNotTaken++;
        pc++;
        return true;
    }

    bool is_branch(const string& opcode) {
        return opcode=="BR_EQ" || opcode=="BR_NE" || opcode=="BR_LT" ||
               opcode=="BR_LE" || opcode=="BR_GT" || opcode=="BR_GE";
    }

    bool is_scalar_arithmetic(const string& opcode) {
        return opcode=="ADD" || opcode=="SUB" || opcode=="MUL" ||
               opcode=="DIV" || opcode=="REM";
    }

    bool is_vector_arithmetic(const string& opcode) {
        return opcode=="VADD" || opcode=="VSUB" || opcode=="VMUL" ||
               opcode=="VDIV";
    }

    // Execute one TAC instruction.
    bool execute(const Instruction& x) {
        if(x.opcode=="LOAD") {
            if(!scalar_load(x)) return false;
            pc++;
        }
        else if(x.opcode=="STORE") {
            if(!scalar_store(x)) return false;
            pc++;
        }
        else if(x.opcode=="VLOAD") {
            if(!vector_load(x)) return false;
            pc++;
        }
        else if(x.opcode=="VSTORE") {
            if(!vector_store(x)) return false;
            pc++;
        }
        else if(is_scalar_arithmetic(x.opcode)) {
            if(!scalar_arithmetic(x)) return false;
            pc++;
        }
        else if(is_vector_arithmetic(x.opcode)) {
            if(!vector_arithmetic(x)) return false;
            pc++;
        }
        else if(x.opcode.rfind("CAST_",0)==0) {
            if(!cast_instruction(x)) return false;
            pc++;
        }
        else if(is_branch(x.opcode)) {
            if(!branch_instruction(x)) return false;
        }
        else if(x.opcode=="JMP") {
            if(!jump_to(x.destination)) return false;
        }
        else if(x.opcode=="HALT") halted=true;
        else return fail("unsupported opcode "+x.opcode);

        return true;
    }

    // Update timing and instruction counts.
    void count_instruction(const Instruction& x) {
        int latency=arch.instruction_latency(x.opcode,x.type);
        bool vectorInstruction=!x.opcode.empty() && x.opcode[0]=='V';
        bool control=is_branch(x.opcode) || x.opcode=="JMP" || x.opcode=="HALT";

        result.totalInstructions++;
        result.totalCycles +=latency;
        result.stallCycles +=latency-1;

        if(vectorInstruction) result.vectorInstructions++;
        else if(control) result.controlInstructions++;
        else result.scalarInstructions++;

        int lanes=vectorInstruction ? arch.vector_lanes(x.type) : 1;
        if(lanes<1) lanes=1;
        result.scalarEquivalentCycles +=(long long)latency*lanes;

        if(x.line>0) {
            LineResult& line=result.lines[x.line];
            line.instructions++;
            line.cycles +=latency;
            if(vectorInstruction) line.vectorInstructions++;
            else if(!control) line.scalarInstructions++;
        }
    }

    // Calculate final memory checksum.
    void find_checksum() {
        unsigned long long hash=1469598103934665603ULL;
        for(unsigned char byte : memory) {
            hash ^=byte;
            hash *=1099511628211ULL;
        }
        result.memoryChecksum=hash;
    }

    // Run instructions until HALT.
    bool run() {
        if(arch.memorySize<=0) {
            error="memory size must be positive";
            return false;
        }
        if(!make_positions()) return false;

        while(pc>=0 && pc<(int)code.size()) {
            if(result.totalInstructions>=MAX_EXECUTED_INSTRUCTIONS)
                return fail("execution limit reached; possible infinite loop");

            int oldPc=pc;
            if(!execute(code[oldPc])) return false;
            count_instruction(code[oldPc]);
            if(halted) break;
        }

        if(!halted) return fail("program ended without HALT");
        find_checksum();
        return true;
    }
};

// Check configuration and run simulator.
bool simulate_program(const vector<Instruction>& code, const Architecture& arch,
                      SimulationResult& result, string& error) {
    if(arch.scalarRegisters<2 || arch.vectorRegisters<2 || arch.memorySize<=0 ||
       arch.scalarUnits<1 || arch.vectorUnits<1) {
        error="invalid processor configuration";
        return false;
    }
    if(arch.vectorWidth!=128 && arch.vectorWidth!=256 && arch.vectorWidth!=512) {
        error="invalid vector width";
        return false;
    }
    if(arch.integerAddLatency<1 || arch.integerMultiplyLatency<1 ||
       arch.integerDivideLatency<1 || arch.floatAddLatency<1 ||
       arch.floatMultiplyLatency<1 || arch.floatDivideLatency<1 ||
       arch.memoryLatency<1 || arch.castLatency<1 || arch.branchLatency<1) {
        error="instruction latencies must be positive";
        return false;
    }

    Simulator simulator(code,arch);
    if(!simulator.run()) {
        error=simulator.error;
        return false;
    }
    result=simulator.result;
    return true;
}

// Write final simulation report.
bool write_simulation_report(const string& fileName, const Architecture& arch,
                             const SimulationResult& result, string& error) {
    ofstream file(fileName);
    if(!file) {
        error="cannot create simulation report";
        return false;
    }

    double speedup=0;
    if(result.totalCycles>0)
        speedup=(double)result.scalarEquivalentCycles/result.totalCycles;

    file<<"PROCESSOR SIMULATION REPORT\n\n";
    file<<"Configuration\n";
    file<<"Scalar registers: "<<arch.scalarRegisters<<'\n';
    file<<"Vector registers: "<<arch.vectorRegisters<<'\n';
    file<<"Vector width: "<<arch.vectorWidth<<" bits\n";
    file<<"Memory size: "<<arch.memorySize<<" bytes\n";
    file<<"Execution model: in-order, single-issue\n\n";

    file<<"Execution summary\n";
    file<<"Total instructions: "<<result.totalInstructions<<'\n';
    file<<"Total cycles: "<<result.totalCycles<<'\n';
    file<<"Stall cycles: "<<result.stallCycles<<'\n';
    file<<"Scalar instructions: "<<result.scalarInstructions<<'\n';
    file<<"Vector instructions: "<<result.vectorInstructions<<'\n';
    file<<"Control instructions: "<<result.controlInstructions<<'\n';
    file<<"Memory operations: "<<result.memoryOperations<<'\n';
    file<<"Branches taken: "<<result.branchesTaken<<'\n';
    file<<"Branches not taken: "<<result.branchesNotTaken<<'\n';
    file<<"Spill loads: "<<result.spillLoads<<'\n';
    file<<"Spill stores: "<<result.spillStores<<'\n';
    file<<fixed<<setprecision(4);
    file<<"Estimated SIMD speedup: "<<speedup<<"x\n\n";

    file<<"Source-line information\n";
    file<<left<<setw(12)<<"Line"<<setw(16)<<"Instructions"<<setw(12)<<"Cycles"
        <<setw(12)<<"Scalar"<<"Vector\n";
    for(const pair<const int,LineResult>& item : result.lines) {
        const LineResult& line=item.second;
        file<<left<<setw(12)<<item.first<<setw(16)<<line.instructions
            <<setw(12)<<line.cycles<<setw(12)<<line.scalarInstructions
            <<line.vectorInstructions<<'\n';
    }
    if(!file) {
        error="could not write simulation report";
        return false;
    }
    return true;
}
