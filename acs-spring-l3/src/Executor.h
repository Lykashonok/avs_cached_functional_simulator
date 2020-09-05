#ifndef RISCV_SIM_EXECUTOR_H
#define RISCV_SIM_EXECUTOR_H

#include "Instruction.h"

class Executor
{
public:
    void Execute(InstructionPtr& instr, Word ip)
    {
        // Add your code here.
        _ip = ip;
        Word secondValue = instr->_imm.has_value() ? instr->_imm.value() : instr->_src2Val;
        switch (instr->_type){
            case IType::Ld :
                instr->_addr = processAlu(instr, secondValue);
                break;
            case IType::St :
                instr->_addr = processAlu(instr, secondValue);
                instr->_data = instr->_src2Val;
                break;
            case IType::Csrr :
                instr->_data = instr->_csrVal;
                break;
            case IType::Csrw :
                instr->_data = instr->_src1Val;
                break;
            case IType::J :
                instr->_data = _ip + 0b100;
                break;
            case IType::Jr:
                instr->_data = _ip + 0b100;
                break;
            case IType::Auipc:
                instr->_data = _ip + instr->_imm.value();
                break;
            case IType::Alu:
                instr->_data = processAlu(instr, secondValue);
                break;
        }
        bool condition;
        switch (instr->_brFunc){
            case BrFunc::Eq :
                condition = instr->_src1Val == instr->_src2Val;
                break;
            case BrFunc::Neq :
                condition = instr->_src1Val != instr->_src2Val;
                break;
            case BrFunc::Lt :
                condition = SignedWord (instr->_src1Val) < SignedWord (instr->_src2Val);
                break;
            case BrFunc::Ltu :
                condition = instr->_src1Val < instr->_src2Val;
                break;
            case BrFunc::Ge :
                condition = SignedWord (instr->_src1Val) >= SignedWord (instr->_src2Val);
                break;
            case BrFunc::Geu :
                condition = instr->_src1Val >= instr->_src2Val;
                break;
            case BrFunc::AT :
                condition = true;
                break;
            case BrFunc::NT :
                condition = false;
                break;
        }
        if (condition){
            switch (instr->_type) {
                case IType::Br :
                    instr->_nextIp = _ip + instr->_imm.value();
                    break;
                case IType::J :
                    instr->_nextIp = _ip + instr->_imm.value();
                    break;
                case IType::Jr :
                    instr->_nextIp = instr->_src1Val + instr->_imm.value();
                    break;
                default:
                    instr->_nextIp = _ip + 0b100;
            }
        } else {
            instr->_nextIp = _ip + 0b100;
        }
    }

private:
    Word _ip;
    // Add helper functions here

    Word processAlu(InstructionPtr& instr, SignedWord secondValue) {
        switch (instr->_aluFunc){
            case AluFunc::Add :
                return instr->_src1Val + secondValue;
            case AluFunc::Or :
                return instr->_src1Val | secondValue;
            case AluFunc::And :
                return instr->_src1Val & secondValue;
            case AluFunc::Sub :
                return instr->_src1Val - secondValue;
            case AluFunc::Xor :
                return instr->_src1Val ^ secondValue;
            case AluFunc::Slt :
                return SignedWord (instr->_src1Val) < SignedWord (secondValue);
            case AluFunc::Sltu :
                return instr->_src1Val < secondValue;
            case AluFunc::Sll :
                return instr->_src1Val << (secondValue % 0b100000);
            case AluFunc::Srl :
                return instr->_src1Val >> (secondValue % 0b100000);
            case AluFunc::Sra :
                return (SignedWord (instr->_src1Val) >> SignedWord (secondValue % 0b100000));
        }
    }
};
#endif // RISCV_SIM_EXECUTOR_H