#ifndef RISCV_SIM_CPU_H
#define RISCV_SIM_CPU_H

#include "Memory.h"
#include "Decoder.h"
#include "RegisterFile.h"
#include "CsrFile.h"
#include "Executor.h"

class Cpu
{



public:
    Cpu(IMem& mem)
        : _mem(mem)
    {

    }
    int executedInstructions = 0;
    int processedClocks = 0;
    void Clock()
    {
        _csrf.Clock();
        processedClocks+=1;
//         Add your code here
        if (!this->isBusy) {
            _mem.Request(_ip);
            isBusy = isReading = true;
        }
        if (isReading) {
            std::optional<Word> instructionWordFromMemory = _mem.Response();
            if (instructionWordFromMemory.has_value()) {
                currentInstruction = _decoder.Decode(instructionWordFromMemory.value());
                _rf.Read(currentInstruction);
                _csrf.Read(currentInstruction);
                _exe.Execute(currentInstruction, _ip);
                executedInstructions+=1;
                _mem.Request(currentInstruction);
                isReading = false;
            }
        } else {
            bool isWritable = _mem.Response(currentInstruction);
            if (isWritable && !isReading) {
                _rf.Write(currentInstruction);
                _csrf.Write(currentInstruction);
                _csrf.InstructionExecuted();
                _ip = currentInstruction->_nextIp;
                isBusy = isReading = false;
            }
        }
    }

    void Reset(Word ip)
    {
        _csrf.Reset();
        _ip = ip;
    }

    std::optional<CpuToHostData> GetMessage()
    {
        return _csrf.GetMessage();
    }

private:
    Reg32 _ip;
    Decoder _decoder;
    RegisterFile _rf;
    CsrFile _csrf;
    Executor _exe;
    IMem& _mem;
    // Add your code here, if needed
    InstructionPtr currentInstruction;
    bool isBusy = false;
    bool isReading = false;
};


#endif //RISCV_SIM_CPU_H
