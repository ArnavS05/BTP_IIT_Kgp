#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "architecture.h"
#include "tac.h"

#include <map>
#include <string>
#include <vector>

using namespace std;

class LineResult {
public:
    long long instructions;
    long long cycles;
    long long scalarInstructions;
    long long vectorInstructions;

    LineResult();
};

class SimulationResult {
public:
    long long totalInstructions;
    long long totalCycles;
    long long stallCycles;
    long long scalarInstructions;
    long long vectorInstructions;
    long long controlInstructions;
    long long memoryOperations;
    long long branchesTaken;
    long long branchesNotTaken;
    long long spillLoads;
    long long spillStores;
    long long scalarEquivalentCycles;
    map<int,LineResult> lines;

    SimulationResult();
};

bool simulate_program(const vector<Instruction>& code, const Architecture& arch,
                      SimulationResult& result, string& error);

bool write_simulation_report(const string& fileName, const Architecture& arch,
                             const SimulationResult& result, string& error);

#endif
