#include "reg_allocator.h"

#include <algorithm>
#include <climits>
#include <map>
#include <set>

using namespace std;

class Block {
public:
    int start, end;
    vector<int> next;
    set<string> use, def, liveIn, liveOut;
};

class State {
public:
    vector<string> scalarOwner, vectorOwner;
    map<string,string> tempReg;
    set<string> valid;
};

bool is_branch_op(const string& opcode) {
    return opcode=="VDIV_CHECK" || opcode.rfind("BR_",0)==0 || opcode.rfind("VCAST_CHECK_",0)==0;
}

bool is_store_op(const string& opcode) {
    return opcode=="STORE" || opcode=="VSTORE";
}

bool is_load_op(const string& opcode) {
    return opcode=="LOAD" || opcode=="VLOAD";
}

string defined_temp(const Instruction& x) {
    if(is_store_op(x.opcode) || is_branch_op(x.opcode) ||
       x.opcode=="JMP" || x.opcode=="HALT") return "";
    if(is_temporary(x.destination)) return x.destination;
    return "";
}

void add_use(vector<string>& values, const string& value) {
    if(!is_temporary(value)) return;
    if(find(values.begin(),values.end(),value)==values.end())
        values.push_back(value);
}

vector<string> used_temps(const Instruction& x) {
    vector<string> values;

    if(is_store_op(x.opcode)) {
        add_use(values,x.destination);
        add_use(values,x.argument1);
    }
    else if(is_load_op(x.opcode)) add_use(values,x.argument1);
    else if(is_branch_op(x.opcode)) {
        add_use(values,x.argument1);
        add_use(values,x.argument2);
    }
    else if(x.opcode!="JMP" && x.opcode!="HALT") {
        add_use(values,x.argument1);
        add_use(values,x.argument2);
    }
    return values;
}

bool make_blocks(const vector<Instruction>& code, vector<Block>& blocks,
                 string& error) {
    if(code.empty()) {
        error="empty TAC";
        return false;
    }

    map<int,int> position;
    for(int i=0;i<(int)code.size();i++) {
        if(position.find(code[i].number)!=position.end()) {
            error="duplicate instruction number";
            return false;
        }
        position[code[i].number]=i;
    }

    set<int> leader;
    leader.insert(0);
    for(int i=0;i<(int)code.size();i++) {
        bool jump=is_branch_op(code[i].opcode) || code[i].opcode=="JMP";
        if(jump) {
            if(!is_label(code[i].destination)) {
                error="invalid jump target at instruction " +to_string(code[i].number);
                return false;
            }
            int target=stoi(code[i].destination.substr(1));
            if(position.find(target)==position.end()) {
                error="missing jump target " +code[i].destination;
                return false;
            }
            leader.insert(position[target]);
        }
        if((jump || code[i].opcode=="HALT") && i+1<(int)code.size())
            leader.insert(i+1);
    }

    vector<int> starts(leader.begin(),leader.end());
    vector<int> blockOf(code.size());
    for(int i=0;i<(int)starts.size();i++) {
        Block block;
        block.start=starts[i];
        block.end=(i+1<(int)starts.size() ? starts[i+1]-1 : (int)code.size()-1);
        blocks.push_back(block);
        for(int j=block.start;j<=block.end;j++) blockOf[j]=i;
    }

    for(int i=0;i<(int)blocks.size();i++) {
        Instruction last=code[blocks[i].end];
        if(is_branch_op(last.opcode)) {
            int target=stoi(last.destination.substr(1));
            blocks[i].next.push_back(blockOf[position[target]]);
            if(i+1<(int)blocks.size()) blocks[i].next.push_back(i+1);
        }
        else if(last.opcode=="JMP") {
            int target=stoi(last.destination.substr(1));
            blocks[i].next.push_back(blockOf[position[target]]);
        }
        else if(last.opcode!="HALT" && i+1<(int)blocks.size())
            blocks[i].next.push_back(i+1);

        for(int j=blocks[i].start;j<=blocks[i].end;j++) {
            vector<string> used=used_temps(code[j]);
            for(string value : used)
                if(blocks[i].def.find(value)==blocks[i].def.end())
                    blocks[i].use.insert(value);

            string made=defined_temp(code[j]);
            if(!made.empty()) blocks[i].def.insert(made);
        }
    }

    bool changed=true;
    while(changed) {
        changed=false;
        for(int i=(int)blocks.size()-1;i>=0;i--) {
            set<string> out, in=blocks[i].use;
            for(int next : blocks[i].next)
                out.insert(blocks[next].liveIn.begin(),blocks[next].liveIn.end());
            for(string value : out)
                if(blocks[i].def.find(value)==blocks[i].def.end())
                    in.insert(value);

            if(out!=blocks[i].liveOut || in!=blocks[i].liveIn) {
                blocks[i].liveOut=out;
                blocks[i].liveIn=in;
                changed=true;
            }
        }
    }
    return true;
}

class Allocator {
public:
    const vector<Instruction>& code;
    const Architecture& arch;
    vector<Block> blocks;
    map<string,string> type;
    map<string,string> slot;
    map<int,int> newNumber;
    vector<Instruction> result;
    int loads=0, stores=0, slotCount=0;
    string error;

    Allocator(const vector<Instruction>& input, const Architecture& machine)
        : code(input), arch(machine) { }

    void remember(const string& value, const string& valueType) {
        if(!is_temporary(value) || type.find(value)!=type.end()) return;
        type[value]=valueType;
        slot[value]="s" +to_string(slotCount++);
    }

    void find_temporaries() {
        // Definitions establish types even when block layout puts a use first.
        for(const Instruction& x : code) remember(defined_temp(x),x.type);
        for(const Instruction& x : code) {
            remember(x.argument1,x.type);
            remember(x.argument2,x.type);
        }
    }

    int bytes(const string& value) {
        if(is_vector_temporary(value)) return arch.vectorWidth/8;
        if(type[value]=="i64" || type[value]=="f64") return 8;
        return 4;
    }

    string reg_name(bool vectorValue, int index) {
        return string(vectorValue ? "v" : "r") +to_string(index);
    }

    vector<string>& owners(State& state, bool vectorValue) {
        return vectorValue ? state.vectorOwner : state.scalarOwner;
    }

    int next_use(const string& value, int from, const Block& block) {
        for(int i=from;i<=block.end;i++) {
            vector<string> used=used_temps(code[i]);
            if(find(used.begin(),used.end(),value)!=used.end()) return i;
            if(defined_temp(code[i])==value) return -1;
        }
        return -1;
    }

    bool needed_later(const string& value, int pos, const Block& block) {
        return next_use(value,pos+1,block)>=0 ||
               block.liveOut.find(value)!=block.liveOut.end();
    }

    Instruction spill_instruction(const string& value, const string& reg,
                                  int line, bool load) {
        Instruction x;
        x.number=0;
        x.line=line;
        x.type=type[value];
        x.argument2=to_string(bytes(value));

        if(load) {
            x.opcode=is_vector_temporary(value) ? "VLOAD" : "LOAD";
            x.destination=reg;
            x.argument1=slot[value];
        }
        else {
            x.opcode=is_vector_temporary(value) ? "VSTORE" : "STORE";
            x.destination=slot[value];
            x.argument1=reg;
        }
        return x;
    }

    void store_value(const string& value, const string& reg, int line,
                     State& state, vector<Instruction>& extra) {
        if(state.valid.find(value)!=state.valid.end()) return;
        extra.push_back(spill_instruction(value,reg,line,false));
        state.valid.insert(value);
        stores++;
    }

    void remove_value(const string& value, State& state) {
        map<string,string>::iterator it=state.tempReg.find(value);
        if(it==state.tempReg.end()) return;

        string reg=it->second;
        bool vectorValue=is_vector_temporary(value);
        int index=stoi(reg.substr(1));
        owners(state,vectorValue)[index]="";
        state.tempReg.erase(it);
    }

    int choose_register(bool vectorValue, int pos, const Block& block,
                        const set<string>& protect, State& state) {
        vector<string>& owner=owners(state,vectorValue);
        for(int i=0;i<(int)owner.size();i++)
            if(owner[i].empty()) return i;

        for(int i=0;i<(int)owner.size();i++) {
            string reg=reg_name(vectorValue,i);
            if(protect.find(reg)!=protect.end()) continue;
            if(!needed_later(owner[i],pos,block)) return i;
        }

        int answer=-1, farthest=-1;
        for(int i=0;i<(int)owner.size();i++) {
            string reg=reg_name(vectorValue,i);
            if(protect.find(reg)!=protect.end()) continue;
            int next=next_use(owner[i],pos+1,block);
            if(next<0) next=INT_MAX;
            if(next>farthest) {
                farthest=next;
                answer=i;
            }
        }
        return answer;
    }

    int choose_source_register(bool vectorValue, int pos, const Block& block,
                               const set<string>& protect, State& state) {
        vector<string>& owner=owners(state,vectorValue);
        int answer=-1, farthest=-1;

        for(int i=0;i<(int)owner.size();i++) {
            string reg=reg_name(vectorValue,i);
            if(protect.find(reg)==protect.end()) continue;
            if(!needed_later(owner[i],pos,block)) return i;

            int next=next_use(owner[i],pos+1,block);
            if(next<0) next=INT_MAX;
            if(next>farthest) {
                farthest=next;
                answer=i;
            }
        }
        return answer;
    }

    void evict(bool vectorValue, int index, int pos, int line,
               const Block& block, State& state, vector<Instruction>& extra) {
        vector<string>& owner=owners(state,vectorValue);
        string old=owner[index];
        if(old.empty()) return;

        if(needed_later(old,pos,block))
            store_value(old,reg_name(vectorValue,index),line,state,extra);
        state.tempReg.erase(old);
        owner[index]="";
    }

    string load_source(const string& value, int pos, int line,
                       const Block& block, const set<string>& protect,
                       State& state, vector<Instruction>& extra) {
        map<string,string>::iterator found=state.tempReg.find(value);
        if(found!=state.tempReg.end()) return found->second;

        bool vectorValue=is_vector_temporary(value);
        int index=choose_register(vectorValue,pos,block,protect,state);
        if(index<0) {
            error="not enough registers at instruction " +to_string(code[pos].number);
            return "";
        }

        evict(vectorValue,index,pos,line,block,state,extra);
        if(state.valid.find(value)==state.valid.end()) {
            error="temporary used before definition: " +value;
            return "";
        }

        string reg=reg_name(vectorValue,index);
        extra.push_back(spill_instruction(value,reg,line,true));
        loads++;
        owners(state,vectorValue)[index]=value;
        state.tempReg[value]=reg;
        return reg;
    }

    string allocate_destination(const string& value, int pos, int line,
                                const Block& block, const set<string>& protect,
                                State& state, vector<Instruction>& extra) {
        map<string,string>::iterator old=state.tempReg.find(value);
        if(old!=state.tempReg.end()) {
            state.valid.erase(value);
            return old->second;
        }

        bool vectorValue=is_vector_temporary(value);
        int index=choose_register(vectorValue,pos,block,protect,state);
        if(index<0)
            index=choose_source_register(vectorValue,pos,block,protect,state);
        if(index<0) {
            error="not enough registers at instruction " +to_string(code[pos].number);
            return "";
        }

        evict(vectorValue,index,pos,line,block,state,extra);
        string reg=reg_name(vectorValue,index);
        owners(state,vectorValue)[index]=value;
        state.tempReg[value]=reg;
        state.valid.erase(value);
        return reg;
    }

    void replace_uses(Instruction& x, const map<string,string>& used) {
        if(is_store_op(x.opcode)) {
            if(is_temporary(x.destination)) x.destination=used.at(x.destination);
            if(is_temporary(x.argument1)) x.argument1=used.at(x.argument1);
        }
        else if(is_load_op(x.opcode)) {
            if(is_temporary(x.argument1)) x.argument1=used.at(x.argument1);
        }
        else if(is_branch_op(x.opcode)) {
            if(is_temporary(x.argument1)) x.argument1=used.at(x.argument1);
            if(is_temporary(x.argument2)) x.argument2=used.at(x.argument2);
        }
        else if(x.opcode!="JMP" && x.opcode!="HALT") {
            if(is_temporary(x.argument1)) x.argument1=used.at(x.argument1);
            if(is_temporary(x.argument2)) x.argument2=used.at(x.argument2);
        }
    }

    void release_dead(int pos, const Block& block, State& state) {
        for(int kind=0;kind<2;kind++) {
            bool vectorValue=kind==1;
            vector<string>& owner=owners(state,vectorValue);
            for(int i=0;i<(int)owner.size();i++) {
                string value=owner[i];
                if(value.empty() || needed_later(value,pos,block)) continue;
                state.tempReg.erase(value);
                owner[i]="";
            }
        }
    }

    void flush_block(const Block& block, int line, State& state,
                     vector<Instruction>& extra) {
        for(int kind=0;kind<2;kind++) {
            bool vectorValue=kind==1;
            vector<string>& owner=owners(state,vectorValue);
            for(int i=0;i<(int)owner.size();i++) {
                string value=owner[i];
                if(value.empty() || block.liveOut.find(value)==block.liveOut.end())
                    continue;
                store_value(value,reg_name(vectorValue,i),line,state,extra);
            }
        }
    }

    bool process_block(const Block& block) {
        State state;
        state.scalarOwner.resize(arch.scalarRegisters);
        state.vectorOwner.resize(arch.vectorRegisters);
        state.valid=block.liveIn;

        for(int pos=block.start;pos<=block.end;pos++) {
            Instruction core=code[pos];
            vector<Instruction> before, after;
            vector<string> values=used_temps(code[pos]);
            map<string,string> used;
            set<string> protect;

            for(string value : values) {
                map<string,string>::iterator found=state.tempReg.find(value);
                if(found!=state.tempReg.end()) protect.insert(found->second);
            }

            for(string value : values) {
                if(used.find(value)!=used.end()) continue;
                string reg=load_source(value,pos,core.line,block,protect,state,before);
                if(!error.empty()) return false;
                used[value]=reg;
                protect.insert(reg);
            }

            replace_uses(core,used);
            string made=defined_temp(code[pos]);
            if(!made.empty()) {
                string reg=allocate_destination(made,pos,core.line,block,
                                                protect,state,before);
                if(!error.empty()) return false;
                core.destination=reg;
            }

            release_dead(pos,block,state);
            bool last=pos==block.end;
            bool terminal=is_branch_op(core.opcode) || core.opcode=="JMP" ||
                          core.opcode=="HALT";
            if(last && terminal) flush_block(block,core.line,state,before);
            if(last && !terminal) flush_block(block,core.line,state,after);

            newNumber[code[pos].number]=(int)result.size();
            result.insert(result.end(),before.begin(),before.end());
            result.push_back(core);
            result.insert(result.end(),after.begin(),after.end());
        }
        return true;
    }

    bool run() {
        if(!make_blocks(code,blocks,error)) return false;
        find_temporaries();
        for(const Block& block : blocks)
            if(!process_block(block)) return false;

        for(int i=0;i<(int)result.size();i++) {
            result[i].number=i;
            if(is_label(result[i].destination)) {
                int old=stoi(result[i].destination.substr(1));
                if(newNumber.find(old)==newNumber.end()) {
                    error="cannot renumber jump target L" +to_string(old);
                    return false;
                }
                result[i].destination="L" +to_string(newNumber[old]);
            }
        }
        return true;
    }
};

bool allocate_registers(const vector<Instruction>& input,
                        const Architecture &arch,
                        vector<Instruction>& output,
                        int &spillLoads, int& spillStores,
                        string& error) {
    if(arch.scalarRegisters<2 || arch.vectorRegisters<2) {
        error="at least two scalar and two vector registers are required";
        return false;
    }

    Allocator allocator(input,arch);
    if(!allocator.run()) {
        error=allocator.error;
        return false;
    }

    output=allocator.result;
    spillLoads=allocator.loads;
    spillStores=allocator.stores;
    return true;
}
