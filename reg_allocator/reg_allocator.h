#ifndef REG_ALLOCATOR_H
#define REG_ALLOCATOR_H

#include "architecture.h"
#include "tac.h"

#include <string>
#include <vector>

using namespace std;

bool allocate_registers(const vector<Instruction>& input,
                        const Architecture &arch,
                        vector<Instruction>& output,
                        int &spillLoads, int& spillStores,
                        string& error);

#endif
