
#ifndef RISCV_SIM_DATAMEMORY_H
#define RISCV_SIM_DATAMEMORY_H

#include "Instruction.h"
#include <iostream>
#include <fstream>
#include <elf.h>
#include <cstring>
#include <vector>
#include <cassert>
#include <map>
#include <deque> // dequeue because there is erase function (queue has not)
#include <algorithm>

//static constexpr size_t memSize = 4*1024*1024; // memory size in 4-byte words
static constexpr size_t memSize = 1024*1024; // memory size in 4-byte words

static constexpr size_t lineSizeBytes = 32;
static constexpr size_t lineSizeWords = lineSizeBytes / sizeof(Word);
static constexpr size_t cacheRequestCycles = 3;
static constexpr size_t defaultWordSize = 4;

static Word ToWordAddr(Word addr) { return addr >> 2u; }
static Word ToLineAddr(Word addr) { return addr & ~(lineSizeBytes - 1); }
static Word ToLineOffset(Word addr) { return ToWordAddr(addr) & (lineSizeWords - 1); }


class MemoryStorage {
public:

    MemoryStorage()
    {
        _mem.resize(memSize);
    }

    bool LoadElf(const std::string &elf_filename) {
        std::ifstream elffile;
        elffile.open(elf_filename, std::ios::in | std::ios::binary);

        if (!elffile.is_open()) {
            std::cerr << "ERROR: load_elf: failed opening file \"" << elf_filename << "\"" << std::endl;
            return false;
        }

        elffile.seekg(0, elffile.end);
        size_t buf_sz = elffile.tellg();
        elffile.seekg(0, elffile.beg);

        // Read the entire file. If it doesn't fit in host memory, it won't fit in the risc-v processor
        std::vector<char> buf(buf_sz);
        elffile.read(buf.data(), buf_sz);

        if (!elffile) {
            std::cerr << "ERROR: load_elf: failed reading elf header" << std::endl;
            return false;
        }

        if (buf_sz < sizeof(Elf32_Ehdr)) {
            std::cerr << "ERROR: load_elf: file too small to be a valid elf file" << std::endl;
            return false;
        }

        // make sure the header matches elf32 or elf64
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *) buf.data();
        unsigned char* e_ident = ehdr->e_ident;
        if (e_ident[EI_MAG0] != ELFMAG0
            || e_ident[EI_MAG1] != ELFMAG1
            || e_ident[EI_MAG2] != ELFMAG2
            || e_ident[EI_MAG3] != ELFMAG3) {
            std::cerr << "ERROR: load_elf: file is not an elf file" << std::endl;
            return false;
        }

        if (e_ident[EI_CLASS] == ELFCLASS32) {
            // 32-bit ELF
            return this->LoadElfSpecific<Elf32_Ehdr, Elf32_Phdr>(buf.data(), buf_sz);
        } else if (e_ident[EI_CLASS] == ELFCLASS64) {
            // 64-bit ELF
            return this->LoadElfSpecific<Elf64_Ehdr, Elf64_Phdr>(buf.data(), buf_sz);
        } else {
            std::cerr << "ERROR: load_elf: file is neither 32-bit nor 64-bit" << std::endl;
            return false;
        }
    }

    Word Read(Word ip)
    {
        return _mem[ToWordAddr(ip)];
    }

    void Write(Word ip, Word data)
    {
        _mem[ToWordAddr(ip)] = data;
    }

private:
    template <typename Elf_Ehdr, typename Elf_Phdr>
    bool LoadElfSpecific(char *buf, size_t buf_sz) {
        // 64-bit ELF
        Elf_Ehdr *ehdr = (Elf_Ehdr*) buf;
        Elf_Phdr *phdr = (Elf_Phdr*) (buf + ehdr->e_phoff);
        if (buf_sz < ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf_Phdr)) {
            std::cerr << "ERROR: load_elf: file too small for expected number of program header tables" << std::endl;
            return false;
        }
        auto memptr = reinterpret_cast<char*>(_mem.data());
        // loop through program header tables
        for (int i = 0 ; i < ehdr->e_phnum ; i++) {
            if ((phdr[i].p_type == PT_LOAD) && (phdr[i].p_memsz > 0)) {
                if (phdr[i].p_memsz < phdr[i].p_filesz) {
                    std::cerr << "ERROR: load_elf: file size is larger than memory size" << std::endl;
                    return false;
                }
                if (phdr[i].p_filesz > 0) {
                    if (phdr[i].p_offset + phdr[i].p_filesz > buf_sz) {
                        std::cerr << "ERROR: load_elf: file section overflow" << std::endl;
                        return false;
                    }

                    // start of file section: buf + phdr[i].p_offset
                    // end of file section: buf + phdr[i].p_offset + phdr[i].p_filesz
                    // start of memory: phdr[i].p_paddr
                    std::memcpy(memptr + phdr[i].p_paddr, buf + phdr[i].p_offset, phdr[i].p_filesz);
                }
                if (phdr[i].p_memsz > phdr[i].p_filesz) {
                    // copy 0's to fill up remaining memory
                    size_t zeros_sz = phdr[i].p_memsz - phdr[i].p_filesz;
                    std::memset(memptr + phdr[i].p_paddr + phdr[i].p_filesz, 0, zeros_sz);
                }
            }
        }
        return true;
    }

    std::vector<Word> _mem;
};


class IMem
{
public:
    IMem() = default;
    virtual ~IMem() = default;
    IMem(const IMem &) = delete;
    IMem(IMem &&) = delete;

    IMem& operator=(const IMem&) = delete;
    IMem& operator=(IMem&&) = delete;

    virtual void Request(Word ip) = 0;
    virtual std::optional<Word> Response() = 0;
    virtual void Request(InstructionPtr &instr) = 0;
    virtual bool Response(InstructionPtr &instr) = 0;
    virtual void Clock() = 0;
};

class CachedMem : public IMem
{
public:
    explicit CachedMem(MemoryStorage& amem)
            : _mem(amem)
    {

    }
    void Request(Word ip)
    {
        _requestedIp = ip;
//        _waitCycles = latency;
        cacheController(cacheType::instruction);
    }

    void Request(InstructionPtr &instr)
    {
        if (instr->_type != IType::Ld && instr->_type != IType::St)
            return;
//        Request(instr->_addr);
        _requestedIp = instr->_addr;
        cacheController(cacheType::calculation);
    }

    std::optional<Word> Response()
    {
        if (_waitCycles > 0)
            return std::optional<Word>();
        //        return _mem.Read(_requestedIp);
        Word lineTag = ToLineAddr(_requestedIp),
                lineOffset = ToLineOffset(_requestedIp);
        //main point, loading value from cache, not from memory
        return _cacheInstructions[lineTag][lineOffset];
    }

    bool Response(InstructionPtr &instr)
    {
        if (instr->_type != IType::Ld && instr->_type != IType::St)
            return true;

        if (_waitCycles != 0)
            return false;
        if (instr->_type == IType::Ld) {
            //instr->_data = _mem.Read(instr->_addr);
            Word lineTag = ToLineAddr(_requestedIp),
                    lineOffset = ToLineOffset(_requestedIp);
            //main point, we loading value from cache;
            instr->_data= _cacheCalculations[lineTag][lineOffset];
        }
        else if (instr->_type == IType::St){
            //_mem.Write(instr->_addr, instr->_data);
            Word lineTag = ToLineAddr(_requestedIp),
                    lineOffset = ToLineOffset(_requestedIp);
            //main point, we sending value to cache;
            _cacheCalculations[lineTag][lineOffset]=instr->_data;
        }

        return true;
    }

    void Clock()
    {
        if (_waitCycles > 0)
            --_waitCycles;
    }
private:
    static constexpr size_t latency =128;
    Word _requestedIp = 0;
    size_t _waitCycles = 0;
    MemoryStorage& _mem;

    std::map<Word,std::map<Word,Word>> _cacheInstructions;
    std::map<Word,std::map<Word,Word>> _cacheCalculations;

    std::deque<Word> _commonlyUsedInstructions;
    std::deque<Word> _commonlyUsedCalculations;

    std::map<Word,std::map<Word,Word>> _currentCache;
    std::deque<Word> _currentCommonlyUsed;

    enum cacheType : size_t{
        instruction = 0,
        calculation = 1
    };

    //cacheType = 0 - instruction
    //cacheType = 1 - calculation
    void cacheController(cacheType type){
        std::map<Word,std::map<Word,Word>> & cache = type == cacheType::instruction ? _cacheInstructions : _cacheCalculations;
        std::deque<Word>& commonlyUsed = type == cacheType::instruction ? _commonlyUsedInstructions : _commonlyUsedCalculations;
        //tag of line we requested for
        Word cacheSize, lineTag = ToLineAddr(_requestedIp);

        //define cache, size and commonlyUsed queue by type
        switch (type) {
            case cacheType::instruction :
                cache = _cacheInstructions;
                commonlyUsed = _commonlyUsedInstructions;
                cacheSize = 1024 / (lineSizeWords * sizeof(Word));
                break;
            case cacheType::calculation :
                cache = _cacheCalculations;
                commonlyUsed = _commonlyUsedCalculations;
                cacheSize = 2048 / (lineSizeWords * sizeof(Word));
                break;
            default:
                return;
        }

        auto foundLineTag = cache.find(lineTag), emptyElement = cache.end();
        if(foundLineTag == emptyElement){
            //we didn't find it in cache, write new value
            //if overflowed, pop first, push last
            if(cacheSize < cache.size()){
                //waiting usual latency
                _waitCycles = latency;
                auto lineToWrite=commonlyUsed.front();
                commonlyUsed.pop_front();
                for(int i = 0 ; i < lineSizeWords ; i++){
                    //also we write it to memory directly with offset i describe later
                    _mem.Write(lineToWrite+defaultWordSize*i,cache[lineToWrite][i]);
                }
                cache.erase(lineToWrite);
            }
            commonlyUsed.push_back(lineTag);
            //rewrite the tag with new values (number of values in line = lineSizeWords)
            //new value will be set in tag address with offset = tag + index_of_value * word_size
            for(int i = 0 ; i < lineSizeWords ; i++){
                Word offset = lineTag + defaultWordSize * i;
                cache[lineTag][ToLineOffset(offset)] = _mem.Read(offset);
            }
            return;
        }
        else{
            //we found it, simulating fast loading and shuffling queue
            _waitCycles = cacheRequestCycles;
            //here we sending found value to end of queue
            auto lineToDelete = commonlyUsed.begin();
            //finding line and move it from it place to the end
            for (auto i = commonlyUsed.begin(); i < commonlyUsed.end(); i++) if (*i == lineTag) lineToDelete = i;
            commonlyUsed.push_back(lineTag);
            commonlyUsed.erase(lineToDelete);
        }
    }
};

class UncachedMem : public IMem
{
public:
    explicit UncachedMem(MemoryStorage& amem)
        : _mem(amem)
    {

    }

    void Request(Word ip)
    {
        _requestedIp = ip;
        _waitCycles = latency;
    }

    std::optional<Word> Response()
    {
        if (_waitCycles > 0)
            return std::optional<Word>();
        return _mem.Read(_requestedIp);
    }

    void Request(InstructionPtr &instr)
    {
        if (instr->_type != IType::Ld && instr->_type != IType::St)
            return;

        Request(instr->_addr);
    }

    bool Response(InstructionPtr &instr)
    {
        if (instr->_type != IType::Ld && instr->_type != IType::St)
            return true;

        if (_waitCycles != 0)
            return false;

        if (instr->_type == IType::Ld)
            instr->_data = _mem.Read(instr->_addr);
        else if (instr->_type == IType::St)
            _mem.Write(instr->_addr, instr->_data);

        return true;
    }

    void Clock()
    {
        if (_waitCycles > 0)
            --_waitCycles;
    }
private:
    static constexpr size_t latency = 128;
    Word _requestedIp = 0;
    size_t _waitCycles = 0;
    MemoryStorage& _mem;
};



#endif //RISCV_SIM_DATAMEMORY_H
