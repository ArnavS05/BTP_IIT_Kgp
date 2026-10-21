#include "simd_optimizer.h"

#include <map>
#include <climits>
#include <set>
#include <queue>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <cctype>

using namespace std;

class SimdBlock {
public:
    int start, end;
    vector<int> next, previous;
};

class SimdLoop {
public:
    int header;
    vector<int> latches;
    set<int> blocks;
};

class LoopControl {
public:
    string address, bound, boundAddress;
    int condition=-1, indexLoad=-1, boundLoad=-1;
    int updateLoad=-1, updateOp=-1, update=-1;
    int body=-1, exit=-1, step=1;
    bool inclusive=false;
};

class AffineValue {
public:
    bool known=false;
    int base=0, stride=0;
};

class MemoryAccess {
public:
    int position, bytes;
    bool store=false, contiguous=false;
    AffineValue address;
};

class LoopLimits {
public:
    int first, end;
    bool runtime=false;
    bool runtimeStart=false;
};

class MemoryRange {
public:
    long long start, end;
};

class SimdConstant {
public:
    string type;
    long long integerValue=0;
    double floatValue=0;
};

class SimdReduction {
public:
    int load=-1, operation=-1, store=-1;
    string address, type, accumulator;
};

class VectorPlan {
public:
    string type;
    int lanes=0;
    vector<int> body;
    vector<bool> vectorOp;
    vector<bool> skip, castCheck;
    map<string,string> names;
    map<string,string> vectorTypes;
    map<string,SimdConstant> constants;
    map<int,string> broadcasts;
    vector<bool> divideCheck;
    vector<SimdReduction> reductions;
};

// Find each instruction's position in the list.
bool make_positions(const vector<Instruction>& code, map<int,int>& position) {
    position.clear();

    for(int i=0;i<(int)code.size();i++) {
        int number=code[i].number;

        if(position.count(number)) {
            position.clear();
            return false;
        }

        position[number] =i;
    }

    return true;
}

bool is_simd_branch(const string& opcode) {
    if(opcode.rfind("VCAST_CHECK_",0)==0 || opcode=="VDIV_CHECK") return true;
    return opcode=="BR_EQ" || opcode=="BR_NE" ||
           opcode=="BR_LT" || opcode=="BR_LE" ||
           opcode=="BR_GT" || opcode=="BR_GE";
}

// Convert a jump label to its instruction position.
int find_jump_position(const string& label, const map<int,int>& position) {
    if(!is_label(label)) return -1;

    int number=0;
    for(int i=1;i<(int)label.size();i++) {
        int digit=label[i]-'0';
        if(number>(INT_MAX-digit)/10) return -1;
        number=number*10+digit;
    }

    if(!position.count(number)) return -1;
    return position.at(number);
}

// Split TAC into blocks and connect their possible paths.
bool make_simd_blocks(const vector<Instruction>& code,
                      vector<SimdBlock>& blocks) {
    blocks.clear();

    map<int,int> position;
    if(!make_positions(code,position)) return false;
    if(code.empty()) return true;

    int n=(int)code.size();
    vector<bool> leader(n,false);
    vector<int> target(n,-1), blockOf(n);
    leader[0]=true;

    for(int i=0;i<n;i++) {
        string opcode=code[i].opcode;
        bool branch=is_simd_branch(opcode);
        bool jump=branch || opcode=="JMP";

        if(jump) {
            target[i]=find_jump_position(code[i].destination,position);
            if(target[i]<0) return false;
            leader[target[i]]=true;
        }

        if((jump || opcode=="HALT") && i+1<n)
            leader[i+1]=true;
    }

    for(int i=0;i<n;i++) {
        if(leader[i]) {
            SimdBlock block;
            block.start=i;
            block.end=i;
            blocks.push_back(block);
        }

        blockOf[i]=(int)blocks.size()-1;
        blocks.back().end=i;
    }

    for(int i=0;i<(int)blocks.size();i++) {
        int end=blocks[i].end;
        string opcode=code[end].opcode;

        if(target[end]>=0)
            blocks[i].next.push_back(blockOf[target[end]]);

        if(opcode!="JMP" && opcode!="HALT" && i+1<(int)blocks.size()) {
            // Avoid repeating an edge when both paths reach the same block.
            if(blocks[i].next.empty() || blocks[i].next[0]!=i+1)
                blocks[i].next.push_back(i+1);
        }
    }

    for(int i=0;i<(int)blocks.size();i++) {
        for(int next : blocks[i].next)
            blocks[next].previous.push_back(i);
    }

    return true;
}

// Find blocks reachable from the program entry.
vector<bool> find_reachable_blocks(const vector<SimdBlock>& blocks) {
    vector<bool> reachable(blocks.size(),false);
    if(blocks.empty()) return reachable;

    queue<int> pending;
    pending.push(0);
    reachable[0]=true;

    while(!pending.empty()) {
        int block=pending.front();
        pending.pop();
        for(int next : blocks[block].next) {
            if(!reachable[next]) {
                reachable[next]=true;
                pending.push(next);
            }
        }
    }

    return reachable;
}

// Find blocks that every entry path must pass through.
vector<vector<bool>> find_dominators(const vector<SimdBlock>& blocks,
                                     const vector<bool>& reachable) {
    int n=(int)blocks.size();
    // dom[i][j]: every path to i passes through j.
    vector<vector<bool>> dom(n,vector<bool>(n,false));
    if(n==0) return dom;

    // Entry is dominated only by itself.
    dom[0][0]=true;
    // Start with all reachable blocks, then remove impossible ones.
    for(int i=1;i<n;i++)
        if(reachable[i]) dom[i]=reachable;

    bool changed=true;
    while(changed) {
        changed=false;
        for(int i=1;i<n;i++) {
            if(!reachable[i]) continue;

            // Keep only dominators shared by all reachable predecessors.
            vector<bool> common=reachable;
            for(int previous : blocks[i].previous) {
                if(!reachable[previous]) continue;
                for(int j=0;j<n;j++)
                    common[j]=common[j] && dom[previous][j];
            }
            common[i]=true;  // Every block dominates itself.

            if(common!=dom[i]) {
                dom[i]=common;
                changed=true;  // Repeat until nothing changes.
            }
        }
    }

    return dom;
}

// Find back edges and collect their natural loops.
vector<SimdLoop> find_simd_loops(const vector<SimdBlock>& blocks) {
    vector<SimdLoop> loops;
    vector<bool> reachable=find_reachable_blocks(blocks);
    vector<vector<bool>> dom=find_dominators(blocks,reachable);
    vector<int> loopOf(blocks.size(),-1);

    // A latch is a block with a back edge to the loop header.
    for(int latch=0;latch<(int)blocks.size();latch++) {
        if(!reachable[latch]) continue;

        for(int header : blocks[latch].next) {
            if(!dom[latch][header]) continue;

            if(loopOf[header]<0) {
                SimdLoop loop;
                loop.header=header;
                loop.blocks.insert(header);
                loopOf[header]=(int)loops.size();
                loops.push_back(loop);
            }

            // A header can have several latches, e.g. with continue; break exits.
            SimdLoop& loop=loops[loopOf[header]];
            loop.latches.push_back(latch);
            queue<int> pending;
            if(!loop.blocks.count(latch)) {
                loop.blocks.insert(latch);
                pending.push(latch);
            }

            // Walk backwards to collect the loop, stopping at its header.
            while(!pending.empty()) {
                int block=pending.front();
                pending.pop();
                for(int previous : blocks[block].previous) {
                    if(reachable[previous] && !loop.blocks.count(previous)) {
                        loop.blocks.insert(previous);
                        pending.push(previous);
                    }
                }
            }
        }
    }

    return loops;
}

// Find a temporary's latest definition earlier in the same block.
int find_local_definition(const vector<Instruction>& code, const SimdBlock& block,
                          int before, const string& value) {
    if(!is_temporary(value)) return -1;

    for(int i=before-1;i>=block.start;i--) {
        if(code[i].opcode=="STORE" || code[i].opcode=="VSTORE") continue;
        if(code[i].destination==value) return i;
    }

    return -1;
}

// Recognize i=i+1 in the latch.
bool find_index_update(const vector<Instruction>& code,
                       const vector<SimdBlock>& blocks,
                       const SimdLoop& loop, LoopControl& control) {
    // Find the counter's only direct write in this loop.
    int update=-1;
    for(int block : loop.blocks) {
        for(int i=blocks[block].start;i<=blocks[block].end;i++) {
            if(code[i].opcode!="STORE" && code[i].opcode!="VSTORE") continue;
            if(code[i].destination!=control.address) continue;
            if(update>=0) return false;
            update=i;
        }
    }

    // The update must be in the latch.
    if(update<0) return false;
    const SimdBlock& latch=blocks[loop.latches[0]];
    if(update<latch.start || update>latch.end) return false;

    const Instruction& store=code[update];
    if(store.opcode!="STORE" || store.type!="i32" || store.argument2!="4")
        return false;

    // Trace the stored value back to its addition.
    int operation=find_local_definition(code,latch,update,store.argument1);
    if(operation<0) return false;
    const Instruction& add=code[operation];
    if(add.opcode!="ADD" || add.type!="i32") return false;

    // Accept both i+1 and 1+i.
    string source=add.argument1;
    string step=add.argument2;
    if(source=="#1") {
        source=add.argument2;
        step=add.argument1;
    }
    if(step!="#1") return false;

    // The old value must come from the same counter.
    int load=find_local_definition(code,latch,operation,source);
    if(load<0) return false;
    const Instruction& old=code[load];
    if(old.opcode!="LOAD" || old.type!="i32" ||
       old.argument1!=control.address || old.argument2!="4") return false;

    control.updateLoad=load;
    control.updateOp=operation;
    control.update=update;
    return true;
}

// Recognize an increasing i32 loop and its exit test.
bool find_loop_control(const vector<Instruction>& code,
                       const vector<SimdBlock>& blocks,
                       const SimdLoop& loop, LoopControl& control) {
    control=LoopControl();
    // For now, accept one latch and a straight-line body.
    if(loop.latches.size()!=1 || loop.latches[0]==loop.header) return false;

    const SimdBlock& header=blocks[loop.header];
    const SimdBlock& latch=blocks[loop.latches[0]];
    if(header.next.size()!=2 || code[latch.end].opcode!="JMP") return false;

    // Taken branch enters the body; fall-through exits.
    int body=header.next[0], exit=header.next[1];
    if(!loop.blocks.count(body) || loop.blocks.count(exit)) return false;
    for(int block : loop.blocks) {
        if(block==loop.header) continue;
        if(blocks[block].next.size()!=1) return false;
        if(!loop.blocks.count(blocks[block].next[0])) return false;
        if(is_simd_branch(code[blocks[block].end].opcode)) return false;
    }

    // Treat n>i the same as i<n.
    const Instruction& branch=code[header.end];
    if(branch.type!="i32") return false;
    string index=branch.argument1, bound=branch.argument2;
    control.inclusive=branch.opcode=="BR_LE" || branch.opcode=="BR_GE";
    if(branch.opcode=="BR_GT" || branch.opcode=="BR_GE") {
        index=branch.argument2;
        bound=branch.argument1;
    }
    else if(branch.opcode!="BR_LT" && branch.opcode!="BR_LE" &&
            branch.opcode!="BR_NE") return false;

    // Find the counter's memory address.
    int load=find_local_definition(code,header,header.end,index);
    if(load<0) return false;
    const Instruction& value=code[load];
    if(value.opcode!="LOAD" || value.type!="i32" || value.argument2!="4")
        return false;
    if(!is_constant(value.argument1)) return false;

    control.address=value.argument1;
    control.indexLoad=load;
    control.condition=header.end;
    control.bound=bound;
    control.body=body;
    control.exit=exit;

    // A nonconstant bound must be loaded from a fixed address.
    if(!is_constant(bound)) {
        int boundLoad=find_local_definition(code,header,header.end,bound);
        if(boundLoad<0) return false;
        const Instruction& value=code[boundLoad];
        if(value.opcode!="LOAD" || value.type!="i32" || value.argument2!="4")
            return false;
        if(!is_constant(value.argument1)) return false;
        control.boundLoad=boundLoad;
        control.boundAddress=value.argument1;
    }

    if(!find_index_update(code,blocks,loop,control)) return false;
    // Reject direct writes to the bound inside the loop.
    for(int block : loop.blocks) {
        for(int i=blocks[block].start;i<=blocks[block].end;i++) {
            if(code[i].opcode!="STORE" && code[i].opcode!="VSTORE") continue;
            if(code[i].destination==control.boundAddress) return false;
        }
    }

    return true;
}

// Read an i32 immediate, including negative values.
bool read_i32_immediate(const string& text, int& value) {
    if(!is_constant(text) || text.size()<2) return false;

    int start=1;
    bool negative=text[start]=='-';
    if(negative) start++;
    if(start==(int)text.size()) return false;

    long long number=0, limit=INT_MAX;
    if(negative) limit=-(long long)INT_MIN;
    for(int i=start;i<(int)text.size();i++) {
        int digit=text[i]-'0';
        if(digit<0 || digit>9) return false;
        // Check before multiplying so parsing cannot overflow.
        if(number>(limit-digit)/10) return false;
        number=number*10+digit;
    }

    if(negative) number=-number;
    value=(int)number;
    return true;
}

int wrap_affine_i32(long long value) {
    value%=4294967296LL;
    if(value>INT_MAX) value-=4294967296LL;
    if(value<INT_MIN) value+=4294967296LL;
    return (int)value;
}

AffineValue get_affine_value(const string& value,
                             const map<string,AffineValue>& values) {
    AffineValue result;
    if(read_i32_immediate(value,result.base)) result.known=true;
    else if(values.count(value)) result=values.at(value);
    return result;
}

// Combine expressions of the form base+stride*i.
AffineValue calculate_affine_value(const Instruction& x,
                                   const map<string,AffineValue>& values) {
    AffineValue result;
    if(x.type!="i32") return result;
    AffineValue left=get_affine_value(x.argument1,values);
    AffineValue right=get_affine_value(x.argument2,values);
    if(!left.known || !right.known) return result;

    if(x.opcode=="ADD") {
        result.base=wrap_affine_i32((long long)left.base+right.base);
        result.stride=wrap_affine_i32((long long)left.stride+right.stride);
    }
    else if(x.opcode=="SUB") {
        result.base=wrap_affine_i32((long long)left.base-right.base);
        result.stride=wrap_affine_i32((long long)left.stride-right.stride);
    }
    else if(x.opcode=="MUL") {
        // Multiplying two i-dependent expressions is nonlinear.
        if(left.stride!=0 && right.stride!=0) return result;
        result.base=wrap_affine_i32((long long)left.base*right.base);
        result.stride=wrap_affine_i32((long long)left.stride*right.base+
                                    (long long)right.stride*left.base);
    }
    else return result;

    // Coefficients follow the same i32 wrap rule as TAC arithmetic.
    result.known=true;
    return result;
}

// Collect memory accesses and their candidate address expressions.
bool find_loop_accesses(const vector<Instruction>& code,
                        const vector<SimdBlock>& blocks,
                        const SimdLoop& loop, const LoopControl& control,
                        vector<MemoryAccess>& accesses) {
    accesses.clear();
    int indexAddress;
    if(!read_i32_immediate(control.address,indexAddress)) return false;

    // i means the counter's value at the loop header.
    map<string,AffineValue> values;
    AffineValue index;
    index.known=true;
    index.stride=1;
    int block=loop.header;

    // Follow execution order rather than block numbering.
    do {
        for(int pos=blocks[block].start;pos<=blocks[block].end;pos++) {
            const Instruction& x=code[pos];
            if(x.opcode=="VLOAD" || x.opcode=="VSTORE") return false;

            if(x.opcode=="LOAD" || x.opcode=="STORE") {
                int bytes=4;
                if(x.type=="i64" || x.type=="f64") bytes=8;
                else if(x.type!="i32" && x.type!="f32") return false;
                if(x.argument2!=to_string(bytes)) return false;

                MemoryAccess access;
                access.position=pos;
                access.bytes=bytes;
                access.store=x.opcode=="STORE";
                string address=x.argument1;
                if(access.store) address=x.destination;
                access.address=get_affine_value(address,values);
                access.contiguous=access.address.known &&
                    (long long)access.address.stride*control.step==bytes;
                accesses.push_back(access);

                if(!access.store) {
                    AffineValue loaded;
                    // Other stores still need alias checks before vectorization.
                    if(x.type=="i32" && access.address.known &&
                       access.address.stride==0 && access.address.base==indexAddress)
                        loaded=index;
                    values[x.destination]=loaded;
                }
                // Counter loads after its update now refer to i+1.
                else if(pos==control.update)
                    index=get_affine_value(x.argument1,values);
            }
            else if(is_temporary(x.destination))
                values[x.destination]=calculate_affine_value(x,values);
        }

        if(block==loop.header) block=control.body;
        else block=blocks[block].next[0];
    } while(block!=loop.header);

    return true;
}

int simd_constant_bytes(const string& type) {
    if(type=="i32" || type=="f32") return 4;
    if(type=="i64" || type=="f64") return 8;
    return 0;
}

bool set_float_constant(double number, const string& type, SimdConstant& value) {
    if(!isfinite(number)) return false;
    if(type=="f32" && (number>FLT_MAX || number<-FLT_MAX)) return false;
    value.type=type;
    value.floatValue=type=="f32" ? (float)number:number;
    return true;
}

// Read and round an immediate according to its TAC type.
bool read_simd_constant(const string& text, const string& type, SimdConstant& value) {
    if(!is_constant(text) || text.size()<2 || !simd_constant_bytes(type)) return false;
    istringstream input(text.substr(1));
    if(type=="f32" || type=="f64") {
        double number;
        input>>number;
        if(!input || !input.eof()) return false;
        return set_float_constant(number,type,value);
    }
    long long number;
    input>>number;
    if(!input || !input.eof()) return false;
    value.type=type;
    value.integerValue=type=="i32" ? wrap_affine_i32(number):number;
    return true;
}

string constant_text(const SimdConstant& value) {
    if(value.type=="i32" || value.type=="i64")
        return "#"+to_string(value.integerValue);
    ostringstream output;
    output<<'#'<<setprecision(17)<<value.floatValue;
    return output.str();
}

bool get_simd_constant(const string& text, const string& type,
                       const map<string,SimdConstant>& values, SimdConstant& value) {
    if(is_constant(text)) return read_simd_constant(text,type,value);
    if(!values.count(text) || values.at(text).type!=type) return false;
    value=values.at(text);
    return true;
}

// Match the simulator's conversions, including truncation and range checks.
bool cast_simd_constant(const SimdConstant& source, const string& type,
                        SimdConstant& value) {
    if(!simd_constant_bytes(type)) return false;
    bool fromFloat=source.type=="f32" || source.type=="f64";
    if(type=="f32" || type=="f64") {
        double number=fromFloat ? source.floatValue:(double)source.integerValue;
        return set_float_constant(number,type,value);
    }
    value.type=type;
    if(!fromFloat) {
        value.integerValue=source.integerValue;
        if(type=="i32") value.integerValue=wrap_affine_i32(value.integerValue);
        return true;
    }
    long double number=trunc((long double)source.floatValue);
    // Exclusive upper limits avoid rounding LLONG_MAX up to 2^63.
    long double limit=type=="i32" ? 2147483648.0L:9223372036854775808.0L;
    if(!isfinite(number) || number< -limit || number>=limit) return false;
    value.integerValue=(long long)number;
    return true;
}

// Fold one typed operation; each floating operation keeps its own rounding.
bool fold_scalar_constant(const Instruction& x,
                          const map<string,SimdConstant>& values, SimdConstant& answer) {
    SimdConstant left,right;
    if(x.opcode.size()==12 && x.opcode.substr(0,5)=="CAST_" && x.opcode[8]=='_') {
        string from=x.opcode.substr(5,3), to=x.opcode.substr(9,3);
        for(char& c : from) c=(char)tolower((unsigned char)c);
        for(char& c : to) c=(char)tolower((unsigned char)c);
        if(to!=x.type || !get_simd_constant(x.argument1,from,values,left)) return false;
        return cast_simd_constant(left,to,answer);
    }
    if(x.opcode!="ADD" && x.opcode!="SUB" && x.opcode!="MUL" && x.opcode!="DIV")
        return false;
    if(!get_simd_constant(x.argument1,x.type,values,left) ||
       !get_simd_constant(x.argument2,x.type,values,right)) return false;
    if(x.type=="f32" || x.type=="f64") {
        double a=left.floatValue, b=right.floatValue, result;
        if(x.opcode=="ADD") result=a+b;
        else if(x.opcode=="SUB") result=a-b;
        else if(x.opcode=="MUL") result=a*b;
        else {
            if(b==0) return false;
            result=a/b;
        }
        return set_float_constant(result,x.type,answer);
    }
    long long a=left.integerValue, b=right.integerValue;
    answer.type=x.type;
    if(x.opcode=="DIV") {
        long long minimum=x.type=="i32" ? INT_MIN:LLONG_MIN;
        if(b==0 || (a==minimum && b==-1)) return false;
        answer.integerValue=a/b;
        return true;
    }
    uint64_t bits;
    if(x.opcode=="ADD") bits=(uint64_t)a+(uint64_t)b;
    else if(x.opcode=="SUB") bits=(uint64_t)a-(uint64_t)b;
    else bits=(uint64_t)a*(uint64_t)b;
    int64_t result;
    memcpy(&result,&bits,8);
    answer.integerValue=result;
    if(x.type=="i32") answer.integerValue=wrap_affine_i32(answer.integerValue);
    return true;
}

// Find a typed constant stored before entering the loop.
bool find_preloop_constant(const vector<Instruction>& code,
                           const vector<SimdBlock>& blocks, const SimdLoop& loop,
                           int address, const string& type, SimdConstant& value) {
    int size=simd_constant_bytes(type);
    if(size==0) return false;
    vector<bool> reachable=find_reachable_blocks(blocks);
    vector<bool> visited(blocks.size(),false);
    int block=loop.header;

    // Walk back through earlier blocks until a defining store is found.
    while(true) {
        // Ignore back edges only when choosing the loop's entry path.
        int previous=-1;
        for(int next : blocks[block].previous) {
            if(!reachable[next]) continue;
            if(block==loop.header && loop.blocks.count(next)) continue;
            // Two incoming paths may have assigned different values.
            if(previous>=0) return false;
            previous=next;
        }
        // Stop if history ends or the backward walk enters a cycle.
        if(previous<0 || visited[previous]) return false;
        block=previous;
        visited[block]=true;

        // The last overlapping write decides the current value.
        for(int i=blocks[block].end;i>=blocks[block].start;i--) {
            const Instruction& x=code[i];
            if(x.opcode!="STORE" && x.opcode!="VSTORE") continue;
            int destination, bytes;
            // A computed address might overwrite the value we are tracing.
            if(!read_i32_immediate(x.destination,destination)) return false;
            if(!read_i32_immediate("#"+x.argument2,bytes) || bytes<=0) return false;
            if((long long)destination+bytes<=address ||
               destination>=(long long)address+size) continue;

            if(x.opcode!="STORE" || x.type!=type ||
               bytes!=size || destination!=address) return false;
            if(is_constant(x.argument1)) return read_simd_constant(x.argument1,type,value);

            // Trace literal arithmetic and casts in this block; loads stay unknown.
            map<string,SimdConstant> values;
            for(int j=blocks[block].start;j<i;j++) {
                const Instruction& definition=code[j];
                if(definition.opcode=="STORE" || definition.opcode=="VSTORE" ||
                   !is_temporary(definition.destination)) continue;
                SimdConstant result;
                if(fold_scalar_constant(definition,values,result))
                    values[definition.destination]=result;
                else values.erase(definition.destination);
            }
            return get_simd_constant(x.argument1,type,values,value);
        }
    }
}

bool find_preloop_value(const vector<Instruction>& code,
                        const vector<SimdBlock>& blocks,
                        const SimdLoop& loop, int address, int& value) {
    SimdConstant constant;
    if(!find_preloop_constant(code,blocks,loop,address,"i32",constant)) return false;
    value=(int)constant.integerValue;
    return true;
}

// Find the initial counter and exclusive upper bound.
bool find_loop_limits(const vector<Instruction>& code,
                      const vector<SimdBlock>& blocks, const SimdLoop& loop,
                      const LoopControl& control, LoopLimits& limits) {
    int address;
    if(control.step!=1 || !read_i32_immediate(control.address,address)) return false;
    if(!find_preloop_value(code,blocks,loop,address,limits.first)) return false;

    // A variable bound needs its earlier stored value.
    if(!read_i32_immediate(control.bound,limits.end)) {
        if(!read_i32_immediate(control.boundAddress,address)) return false;
        if(!find_preloop_value(code,blocks,loop,address,limits.end)) return false;
    }

    if(control.inclusive) {
        if(limits.end==INT_MAX) return false;
        limits.end++;
    }
    // Empty loops do not need vectorization.
    return limits.first<limits.end;
}

bool ranges_overlap(const MemoryRange& left, const MemoryRange& right) {
    return left.start<right.end && right.start<left.end;
}

// Find all bytes touched by an access across the loop.
bool find_access_range(const MemoryAccess& access, const LoopLimits& limits,
                       const Architecture& arch, MemoryRange& range) {
    if(!access.address.known || access.bytes<=0 || limits.first>=limits.end)
        return false;

    // A linear expression reaches its extremes at the endpoint iterations.
    long long first=access.address.base+
                    (long long)access.address.stride*limits.first;
    long long last=access.address.base+
                   (long long)access.address.stride*((long long)limits.end-1);
    if(first>last) swap(first,last);  // Negative strides reverse address order.

    // Reject ranges that wrap or leave simulated memory.
    if(first<0 || last>INT_MAX || last+access.bytes>arch.memorySize) return false;
    range.start=first;
    range.end=last+access.bytes;  // Exclusive end, including the last value's bytes.
    return true;
}

// Recognize isolated integer sums; intermediate accumulator values cannot escape.
vector<SimdReduction> find_simd_reductions(const vector<Instruction>& code,
                                          const vector<SimdBlock>& blocks,
                                          const SimdLoop& loop, const LoopControl& control,
                                          const vector<MemoryAccess>& accesses) {
    vector<SimdReduction> reductions;
    for(const MemoryAccess& access : accesses) {
        const Instruction& store=code[access.position];
        if(!access.store || !access.address.known || access.address.stride!=0 ||
           (store.type!="i32" && store.type!="i64") ||
           store.destination==control.address || store.destination==control.boundAddress)
            continue;
        int member=-1;
        for(int block : loop.blocks)
            if(access.position>=blocks[block].start && access.position<=blocks[block].end)
                member=block;
        if(member<0) continue;
        int operation=find_local_definition(code,blocks[member],access.position,store.argument1);
        if(operation<0) continue;
        const Instruction& add=code[operation];
        if((add.opcode!="ADD" && add.opcode!="SUB") || add.type!=store.type) continue;
        int load=-1;
        for(string source : {add.argument1,add.argument2}) {
            if(add.opcode=="SUB" && source!=add.argument1) continue;
            int candidate=find_local_definition(code,blocks[member],operation,source);
            if(candidate>=0 && code[candidate].opcode=="LOAD" &&
               code[candidate].argument1==store.destination && code[candidate].type==store.type)
                load=candidate;
        }
        if(load<0) continue;
        bool isolated=true;
        for(int block : loop.blocks) {
            for(int pos=blocks[block].start;pos<=blocks[block].end;pos++) {
                const Instruction& x=code[pos];
                for(string source : {x.argument1,x.argument2}) {
                    if(source==code[load].destination && pos!=operation) isolated=false;
                    if(source==add.destination && pos!=access.position) isolated=false;
                }
                if((x.opcode=="STORE" || x.opcode=="VSTORE") &&
                   (x.destination==code[load].destination || x.destination==add.destination))
                    isolated=false;
            }
        }
        // Both operands being the accumulator is not an independent contribution.
        if(add.argument1==add.argument2) isolated=false;
        if(!isolated) continue;
        SimdReduction reduction;
        reduction.load=load;
        reduction.operation=operation;
        reduction.store=access.position;
        reduction.address=store.destination;
        reduction.type=store.type;
        reductions.push_back(reduction);
    }
    return reductions;
}

bool reduction_memory(int position, const vector<SimdReduction>& reductions) {
    for(const SimdReduction& reduction : reductions)
        if(position==reduction.load || position==reduction.store) return true;
    return false;
}

// Check memory bounds and rule out dependencies between iterations.
bool check_loop_memory(const vector<Instruction>& code,
                       const vector<SimdBlock>& blocks, const SimdLoop& loop,
                       const LoopControl& control, const LoopLimits& limits,
                       const vector<MemoryAccess>& accesses,
                       const Architecture& arch) {
    if(arch.memorySize<=0) return false;
    const SimdBlock& header=blocks[loop.header];
    // Keep only the control loads and branch in the header.
    for(int i=header.start;i<=header.end;i++) {
        if(i!=control.indexLoad && i!=control.boundLoad && i!=control.condition)
            return false;
    }

    int address;
    if(!read_i32_immediate(control.address,address)) return false;
    MemoryRange counter;
    counter.start=address;
    counter.end=(long long)address+4;
    if(counter.start<0 || counter.end>arch.memorySize) return false;

    MemoryRange bound;
    bool boundInMemory=!control.boundAddress.empty();
    if(boundInMemory) {
        if(!read_i32_immediate(control.boundAddress,address)) return false;
        bound.start=address;
        bound.end=(long long)address+4;
        if(bound.start<0 || bound.end>arch.memorySize ||
           ranges_overlap(counter,bound)) return false;
    }

    vector<MemoryRange> ranges(accesses.size());
    vector<SimdReduction> reductions=find_simd_reductions(code,blocks,loop,control,accesses);
    for(int i=0;i<(int)accesses.size();i++) {
        const MemoryAccess& access=accesses[i];
        if(!find_access_range(access,limits,arch,ranges[i])) return false;

        // Only ordinary counter loads and its update may touch counter memory.
        bool counterLoad=!access.store && access.address.stride==0 &&
            access.address.base==counter.start && access.bytes==4 &&
            code[access.position].type=="i32";
        bool counterUpdate=access.position==control.update;
        if(ranges_overlap(ranges[i],counter) && !counterLoad && !counterUpdate)
            return false;
        if(access.store && boundInMemory && ranges_overlap(ranges[i],bound))
            return false;

        // Strided accesses and repeated scalar stores stay scalar for now.
        if(access.address.stride!=0 && !access.contiguous) return false;
        if(access.store && !counterUpdate && !access.contiguous &&
           !reduction_memory(access.position,reductions)) return false;
    }

    // A reduction slot must not alias any other memory operation.
    for(const SimdReduction& reduction : reductions) {
        MemoryRange slot;
        int start;
        if(!read_i32_immediate(reduction.address,start)) return false;
        slot.start=start;
        slot.end=(long long)start+simd_constant_bytes(reduction.type);
        for(int i=0;i<(int)accesses.size();i++)
            if(accesses[i].position!=reduction.load && accesses[i].position!=reduction.store &&
               ranges_overlap(slot,ranges[i])) return false;
    }

    for(int i=0;i<(int)accesses.size();i++) {
        for(int j=i+1;j<(int)accesses.size();j++) {
            const MemoryAccess& left=accesses[i];
            const MemoryAccess& right=accesses[j];
            if(!left.store && !right.store) continue;  // Two reads cannot conflict.
            if(left.position==control.update || right.position==control.update)
                continue;
            if(reduction_memory(left.position,reductions) ||
               reduction_memory(right.position,reductions)) continue;
            if(!ranges_overlap(ranges[i],ranges[j])) continue;

            // Allow A[i]=A[i]+1; reject shifted overlap such as A[i+1]=A[i].
            if(left.contiguous && right.contiguous && left.bytes==right.bytes &&
               left.address.base==right.address.base &&
               left.address.stride==right.address.stride) continue;
            return false;
        }
    }

    return true;
}

// Find a safe interval when the initial counter or bound is known only at runtime.
bool find_runtime_limits(const vector<Instruction>& code,
                         const vector<SimdBlock>& blocks, const SimdLoop& loop,
                         const LoopControl& control,
                         const vector<MemoryAccess>& accesses,
                         const Architecture& arch, LoopLimits& limits) {
    int address, bound;
    if(control.step!=1) return false;
    bool knownBound=read_i32_immediate(control.bound,bound);
    if(!knownBound) {
        if(!read_i32_immediate(control.boundAddress,address)) return false;
        knownBound=find_preloop_value(code,blocks,loop,address,bound);
    }
    if(knownBound && control.inclusive) {
        if(bound==INT_MAX) return false;
        bound++;
    }
    if(!read_i32_immediate(control.address,address)) return false;
    limits.runtimeStart=!find_preloop_value(code,blocks,loop,address,limits.first);
    // Unknown starts use the safe interval beginning at zero; negative starts fall back.
    if(limits.runtimeStart) limits.first=0;
    if(knownBound && !limits.runtimeStart) return false;
    if(limits.first==INT_MAX) return false;

    int low=limits.first+1, high=INT_MAX;
    if(knownBound) high=bound;
    if(low>high) return false;
    limits.end=low;
    if(!check_loop_memory(code,blocks,loop,control,limits,accesses,arch)) return false;

    // Increasing end only expands the ranges, so safety can be binary-searched.
    while(low<high) {
        int middle=(int)((long long)low+((long long)high-low+1)/2);
        limits.end=middle;
        if(check_loop_memory(code,blocks,loop,control,limits,accesses,arch))
            low=middle;
        else high=middle-1;
    }
    limits.end=low;
    limits.runtime=true;
    return true;
}

string fresh_scalar_temp(set<string>& used, int& next) {
    string name="t"+to_string(next++);
    while(used.count(name)) name="t"+to_string(next++);
    used.insert(name);
    return name;
}

string fresh_vector_temp(set<string>& used, int& next) {
    string name="vt"+to_string(next++);
    while(used.count(name)) name="vt"+to_string(next++);
    used.insert(name);
    return name;
}

// Recover fixed numeric values loaded inside a memory-safe loop.
map<string,SimdConstant> find_loop_constants(const vector<Instruction>& code,
                                      const vector<SimdBlock>& blocks,
                                      const SimdLoop& loop, const LoopControl& control,
                                      const vector<MemoryAccess>& accesses) {
    map<string,SimdConstant> constants;
    int counter;
    if(!read_i32_immediate(control.address,counter)) return constants;
    for(const MemoryAccess& access : accesses) {
        const Instruction& x=code[access.position];
        if(access.store || !access.address.known ||
           access.address.stride!=0) continue;
        // The counter changes even when its initial value is known.
        if(access.address.base==counter) continue;
        SimdConstant value;
        // Earlier memory checks ruled out writes overlapping this load.
        if(find_preloop_constant(code,blocks,loop,access.address.base,x.type,value))
            constants[x.destination]=value;
    }
    return constants;
}

string vector_operand(const string& value, const VectorPlan& plan) {
    if(plan.names.count(value)) return plan.names.at(value);
    if(plan.constants.count(value)) return constant_text(plan.constants.at(value));
    return value;
}

// A fixed divisor avoids introducing lane-dependent division failures.
bool can_vectorize_division(const Instruction& x, const VectorPlan& plan) {
    SimdConstant divisor;
    if(!read_simd_constant(vector_operand(x.argument2,plan),x.type,divisor)) return false;
    if(x.type=="i32" || x.type=="i64")
        return divisor.integerValue!=0 && divisor.integerValue!=-1;
    return divisor.floatValue!=0;
}

string simd_cast_source(const Instruction& x) {
    if(x.opcode.size()!=12 || x.opcode.substr(0,5)!="CAST_" || x.opcode[8]!='_') return "";
    string from=x.opcode.substr(5,3), to=x.opcode.substr(9,3);
    for(char& c : from) c=(char)tolower((unsigned char)c);
    for(char& c : to) c=(char)tolower((unsigned char)c);
    if(to!=x.type || !simd_constant_bytes(from)) return "";
    return from;
}

void name_vector_value(const string& value, const string& type, VectorPlan& plan,
                        set<string>& used, int& next) {
    if(!plan.names.count(value)) plan.names[value]=fresh_vector_temp(used,next);
    plan.vectorTypes[value]=type;
}

// Plan data operations separately from scalar addresses and the loop counter.
bool make_vector_plan(const vector<Instruction>& code,
                      const vector<SimdBlock>& blocks, const SimdLoop& loop,
                      const LoopControl& control, const LoopLimits& limits,
                      const vector<MemoryAccess>& accesses,
                      const Architecture& arch, VectorPlan& plan) {
    plan=VectorPlan();
    plan.vectorOp.resize(code.size(),false);
    plan.skip.resize(code.size(),false);
    plan.castCheck.resize(code.size(),false);
    plan.divideCheck.resize(code.size(),false);
    plan.lanes=arch.vectorWidth/32;
    plan.constants=find_loop_constants(code,blocks,loop,control,accesses);
    plan.reductions=find_simd_reductions(code,blocks,loop,control,accesses);
    map<int,MemoryAccess> memory;
    set<string> defined,bodyDefinitions,used;
    for(const Instruction& x : code)
        for(string value : {x.destination,x.argument1,x.argument2}) used.insert(value);
    int next=0;
    bool hasStore=!plan.reductions.empty();
    for(const MemoryAccess& access : accesses) {
        memory[access.position]=access;
        const Instruction& x=code[access.position];
        if(access.contiguous) {
            plan.lanes=min(plan.lanes,arch.vector_lanes(x.type));
            if(access.store) hasStore=true;
        }
    }
    for(SimdReduction& reduction : plan.reductions) {
        // The initial sum is constant, but its value changes inside the loop.
        plan.constants.erase(code[reduction.load].destination);
        reduction.accumulator=fresh_vector_temp(used,next);
        for(int pos : {reduction.load,reduction.operation}) {
            plan.names[code[pos].destination]=reduction.accumulator;
            plan.vectorTypes[code[pos].destination]=reduction.type;
        }
        plan.skip[reduction.load]=true;
        plan.skip[reduction.store]=true;
        plan.lanes=min(plan.lanes,arch.vector_lanes(reduction.type));
    }
    int counter;
    if(!read_i32_immediate(control.address,counter)) return false;
    // A read-only scalar becomes a real broadcast, not a free scalar vector operand.
    for(const MemoryAccess& access : accesses) {
        const Instruction& x=code[access.position];
        if(access.store || access.contiguous || !access.address.known ||
           access.address.stride!=0 || access.address.base==counter ||
           reduction_memory(access.position,plan.reductions) ||
           plan.constants.count(x.destination)) continue;
        bool usedInBody=false;
        for(int block : loop.blocks) {
            if(block==loop.header) continue;
            for(int pos=blocks[block].start;pos<=blocks[block].end;pos++)
                if(code[pos].argument1==x.destination || code[pos].argument2==x.destination)
                    usedInBody=true;
        }
        if(!usedInBody) continue;
        name_vector_value(x.destination,x.type,plan,used,next);
        plan.broadcasts[access.position]=plan.names[x.destination];
        plan.lanes=min(plan.lanes,arch.vector_lanes(x.type));
    }
    if(!hasStore) return false;
    defined.insert(code[control.indexLoad].destination);
    if(control.boundLoad>=0) defined.insert(code[control.boundLoad].destination);
    bool updated=false, committed=false;
    int block=control.body;
    do {
        for(int pos=blocks[block].start;pos<=blocks[block].end;pos++) {
            const Instruction& x=code[pos];
            if(x.opcode=="JMP") continue;
            if(updated) return false;
            plan.body.push_back(pos);
            vector<string> sources={x.argument1,x.argument2};
            if(x.opcode=="STORE") sources.push_back(x.destination);
            for(const string& source : sources)
                if(is_temporary(source) && !defined.count(source)) return false;
            bool vectorValue=false;
            if(x.opcode=="LOAD" || x.opcode=="STORE") {
                vectorValue=memory.at(pos).contiguous;
                if(x.opcode=="STORE") {
                    if(vectorValue && !is_constant(x.argument1) &&
                       !plan.names.count(x.argument1) && !plan.constants.count(x.argument1))
                        return false;
                    if(vectorValue && plan.constants.count(x.argument1) &&
                       plan.constants.at(x.argument1).type!=x.type) return false;
                    if(vectorValue && plan.names.count(x.argument1) &&
                       plan.vectorTypes[x.argument1]!=x.type) return false;
                    if(!vectorValue && plan.names.count(x.argument1) && !plan.skip[pos])
                        return false;
                    if(pos==control.update) updated=true;
                    if(!plan.skip[pos] && pos!=control.update) committed=true;
                }
            }
            else {
                SimdConstant constant;
                if(fold_scalar_constant(x,plan.constants,constant))
                    plan.constants[x.destination]=constant;
                else {
                    string from=simd_cast_source(x);
                    if(!from.empty()) {
                        if(!plan.names.count(x.argument1) || plan.vectorTypes[x.argument1]!=from)
                            return false;
                        vectorValue=true;
                        if((from=="f32" || from=="f64") && (x.type=="i32" || x.type=="i64")) {
                            // Retrying a chunk is safe only before stores or sum updates.
                            if(committed) return false;
                            plan.castCheck[pos]=true;
                        }
                    }
                    else {
                        if(x.opcode!="ADD" && x.opcode!="SUB" && x.opcode!="MUL" &&
                           x.opcode!="DIV") return false;
                        vectorValue=plan.names.count(x.argument1) || plan.names.count(x.argument2);
                        if(vectorValue) {
                            for(const string& source : sources) {
                                if(!is_constant(source) && !plan.names.count(source) &&
                                   !plan.constants.count(source)) return false;
                                if(plan.constants.count(source) &&
                                   plan.constants.at(source).type!=x.type) return false;
                                if(plan.names.count(source) && plan.vectorTypes[source]!=x.type)
                                    return false;
                            }
                            if(x.opcode=="DIV" && !can_vectorize_division(x,plan)) {
                                if(committed) return false;
                                plan.divideCheck[pos]=true;
                            }
                        }
                        else if(x.type!="i32" || x.opcode=="DIV") return false;
                    }
                }
            }
            plan.vectorOp[pos]=vectorValue;
            if(x.opcode!="STORE") {
                if(!is_temporary(x.destination) || is_vector_temporary(x.destination) ||
                   defined.count(x.destination)) return false;
                defined.insert(x.destination);
                bodyDefinitions.insert(x.destination);
                if(vectorValue) {
                    name_vector_value(x.destination,x.type,plan,used,next);
                    plan.lanes=min(plan.lanes,arch.vector_lanes(x.type));
                }
            }
            for(const SimdReduction& reduction : plan.reductions)
                if(pos==reduction.operation) committed=true;
        }
        block=blocks[block].next[0];
    } while(block!=loop.header);
    if(plan.lanes<2 || (long long)limits.end-limits.first<plan.lanes) return false;
    vector<bool> inside(code.size(),false);
    for(int member : loop.blocks)
        for(int i=blocks[member].start;i<=blocks[member].end;i++) inside[i]=true;
    for(int i=0;i<(int)code.size();i++) {
        if(inside[i]) continue;
        const Instruction& x=code[i];
        if(bodyDefinitions.count(x.argument1) || bodyDefinitions.count(x.argument2) ||
           bodyDefinitions.count(x.destination)) return false;
    }
    return true;
}

Instruction make_simd_instruction(int line, const string& opcode, const string& type,
                                  const string& destination, const string& argument1,
                                  const string& argument2) {
    Instruction x;
    x.number=0;
    x.line=line;
    x.opcode=opcode;
    x.type=type;
    x.destination=destination;
    x.argument1=argument1;
    x.argument2=argument2;
    return x;
}

// Build complete chunks, then commit sums before entering the original scalar loop.
vector<Instruction> make_vector_loop(const vector<Instruction>& code,
                                     const vector<SimdBlock>& blocks,
                                     const SimdLoop& loop, const LoopControl& control,
                                     const LoopLimits& limits, const VectorPlan& plan,
                                     const Architecture& arch) {
    vector<Instruction> result;
    int start=blocks[loop.header].start;
    long long count=(long long)limits.end-limits.first;
    long long vectorEnd=limits.first+(count/plan.lanes)*plan.lanes;
    int line=code[control.condition].line;
    set<string> used;
    for(const Instruction& x : code)
        for(string value : {x.destination,x.argument1,x.argument2}) used.insert(value);
    int next=0;
    vector<int> checks;
    // Every exit can combine these lanes, even if no vector chunk ran.
    for(const SimdReduction& reduction : plan.reductions)
        result.push_back(make_simd_instruction(line,"VBROADCAST",reduction.type,
            reduction.accumulator,"#0",to_string(plan.lanes)));
    string last="#"+to_string(vectorEnd), comparison="BR_LT";
    if(limits.runtime) {
        // Guard the bounds before subtracting the chunk size.
        last=fresh_scalar_temp(used,next);
        if(limits.runtimeStart) {
            result.push_back(code[control.indexLoad]);
            checks.push_back((int)result.size());
            result.push_back(make_simd_instruction(line,"BR_LT","i32","-",
                code[control.indexLoad].destination,"#"+to_string(limits.first)));
        }
        if(control.boundLoad>=0) result.push_back(code[control.boundLoad]);
        int inclusive=control.inclusive ? 1:0;
        checks.push_back((int)result.size());
        result.push_back(make_simd_instruction(line,"BR_GT","i32","-",
            control.bound,"#"+to_string((long long)limits.end-inclusive)));
        checks.push_back((int)result.size());
        result.push_back(make_simd_instruction(line,"BR_LT","i32","-",
            control.bound,"#"+to_string((long long)limits.first+plan.lanes-inclusive)));
        result.push_back(make_simd_instruction(line,"SUB","i32",last,
            control.bound,"#"+to_string(plan.lanes-inclusive)));
        comparison="BR_LE";
    }
    int repeat=start+(int)result.size();
    for(int pos=blocks[loop.header].start;pos<control.condition;pos++) {
        result.push_back(code[pos]);
        if(plan.broadcasts.count(pos))
            result.push_back(make_simd_instruction(code[pos].line,"VBROADCAST",code[pos].type,
                plan.broadcasts.at(pos),code[pos].destination,to_string(plan.lanes)));
    }
    int guard=(int)result.size();
    result.push_back(make_simd_instruction(line,comparison,"i32","-",
        code[control.indexLoad].destination,last));
    int fallback=(int)result.size();
    result.push_back(make_simd_instruction(line,"JMP","none","-","-","-"));
    int body=(int)result.size();
    for(int pos : plan.body) {
        Instruction x=code[pos];
        if(plan.skip[pos] || (x.opcode!="STORE" && plan.constants.count(x.destination))) continue;
        if(plan.castCheck[pos]) {
            checks.push_back((int)result.size());
            result.push_back(make_simd_instruction(x.line,"VCAST_CHECK_"+x.opcode.substr(5),
                x.type,"-",vector_operand(x.argument1,plan),"-"));
        }
        if(plan.divideCheck[pos]) {
            checks.push_back((int)result.size());
            result.push_back(make_simd_instruction(x.line,"VDIV_CHECK",x.type,"-",
                vector_operand(x.argument1,plan),vector_operand(x.argument2,plan)));
        }
        if(plan.vectorOp[pos]) {
            x.opcode="V"+x.opcode;
            if(x.opcode=="VSTORE") x.argument1=vector_operand(x.argument1,plan);
            else {
                x.destination=plan.names.at(x.destination);
                if(x.opcode!="VLOAD") {
                    x.argument1=vector_operand(x.argument1,plan);
                    x.argument2=vector_operand(x.argument2,plan);
                }
            }
            if(x.opcode=="VLOAD" || x.opcode=="VSTORE")
                x.argument2=to_string(plan.lanes*simd_constant_bytes(x.type));
        }
        else if(pos==control.updateOp) {
            if(x.argument1=="#1") x.argument1="#"+to_string(plan.lanes);
            else x.argument2="#"+to_string(plan.lanes);
        }
        else {
            if(plan.constants.count(x.argument1)) x.argument1=constant_text(plan.constants.at(x.argument1));
            if(plan.constants.count(x.argument2)) x.argument2=constant_text(plan.constants.at(x.argument2));
            if(x.opcode=="STORE" && plan.constants.count(x.destination))
                x.destination=constant_text(plan.constants.at(x.destination));
        }
        result.push_back(x);
        if(plan.broadcasts.count(pos))
            result.push_back(make_simd_instruction(x.line,"VBROADCAST",x.type,
                plan.broadcasts.at(pos),x.destination,to_string(plan.lanes)));
    }
    result.push_back(make_simd_instruction(code[blocks[loop.latches[0]].end].line,
        "JMP","none","L"+to_string(repeat),"-","-"));
    string finish="L"+to_string(start+(int)result.size());
    result[guard].destination="L"+to_string(start+body);
    result[fallback].destination=finish;
    for(int check : checks) result[check].destination=finish;
    // Commit completed chunks before the original scalar loop resumes.
    for(const SimdReduction& reduction : plan.reductions) {
        string partial=fresh_scalar_temp(used,next);
        string initial=fresh_scalar_temp(used,next);
        string total=fresh_scalar_temp(used,next);
        string bytes=to_string(simd_constant_bytes(reduction.type));
        result.push_back(make_simd_instruction(line,"VREDUCE_ADD",reduction.type,
            partial,reduction.accumulator,"-"));
        result.push_back(make_simd_instruction(line,"LOAD",reduction.type,initial,reduction.address,bytes));
        result.push_back(make_simd_instruction(line,"ADD",reduction.type,total,initial,partial));
        result.push_back(make_simd_instruction(line,"STORE",reduction.type,reduction.address,total,bytes));
    }
    (void)arch;
    return result;
}

// Insert the vector loop and repair original jump targets.
vector<Instruction> insert_vector_loop(const vector<Instruction>& code,
                                       const vector<SimdBlock>& blocks,
                                       const SimdLoop& loop,
                                       const vector<Instruction>& vectorCode) {
    int start=blocks[loop.header].start, added=(int)vectorCode.size();
    vector<Instruction> result;
    result.insert(result.end(),code.begin(),code.begin()+start);
    result.insert(result.end(),vectorCode.begin(),vectorCode.end());
    result.insert(result.end(),code.begin()+start,code.end());

    map<int,int> position;
    if(!make_positions(code,position)) return code;
    vector<bool> inside(code.size(),false);
    for(int block : loop.blocks)
        for(int i=blocks[block].start;i<=blocks[block].end;i++) inside[i]=true;

    for(int i=0;i<(int)code.size();i++) {
        int newPosition=i;
        if(i>=start) newPosition+=added;
        Instruction& x=result[newPosition];
        if(is_simd_branch(x.opcode) || x.opcode=="JMP") {
            int target=find_jump_position(x.destination,position);
            if(target<0) return code;
            int newTarget=target;
            if(target>=start) newTarget+=added;
            // New entries use the vector loop; scalar back edges stay scalar.
            if(target==start && !inside[i]) newTarget=start;
            x.destination="L"+to_string(newTarget);
        }
    }
    for(int i=0;i<(int)result.size();i++) result[i].number=i;
    return result;
}

// Analyze again after each insertion because positions and blocks have changed.
vector<Instruction> vectorize_independent_loops(const vector<Instruction>& code,
                                               const Architecture& arch) {
    vector<Instruction> result=code;
    bool changed=true;
    while(changed) {
        changed=false;
        vector<SimdBlock> blocks;
        if(!make_simd_blocks(result,blocks)) return result;
        vector<SimdLoop> loops=find_simd_loops(blocks);
        for(const SimdLoop& loop : loops) {
            LoopControl control;
            if(!find_loop_control(result,blocks,loop,control)) continue;
            vector<MemoryAccess> accesses;
            if(!find_loop_accesses(result,blocks,loop,control,accesses)) continue;
            LoopLimits limits;
            if(find_loop_limits(result,blocks,loop,control,limits)) {
                if(!check_loop_memory(result,blocks,loop,control,limits,accesses,arch))
                    continue;
            }
            else if(!find_runtime_limits(result,blocks,loop,control,accesses,arch,limits))
                continue;
            VectorPlan plan;
            if(!make_vector_plan(result,blocks,loop,control,limits,accesses,arch,plan))
                continue;
            vector<Instruction> vectorCode=
                make_vector_loop(result,blocks,loop,control,limits,plan,arch);
            result=insert_vector_loop(result,blocks,loop,vectorCode);
            changed=true;
            break;
        }
    }
    return result;
}

vector<Instruction> optimize_simd(const vector<Instruction>& code,
                                  const Architecture& arch) {
    return vectorize_independent_loops(code,arch);
}
