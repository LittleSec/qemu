#pragma once

#include <capstone/capstone.h>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class source_file;
class binary_file;
class translation_block;

class source_line
{
public:
    source_line(unsigned int number, const std::string& line, source_file& file)
        : number_(number), line_(line), file_(file)
    {
    }

    unsigned int number() const { return number_; }
    const std::string& line() const { return line_; }
    source_file& file() const { return file_; }

private:
    unsigned int number_;
    const std::string line_;
    source_file& file_;
};

class source_file
{
public:
    source_file(const std::string& path) : path_(path)
    {
        source_file& file = *this;
        // empty first line
        lines_.emplace_back(0, "", file);

        std::ifstream in(path);
        std::string line;
        unsigned int line_number = 1;
        while (std::getline(in, line)) {
            lines_.emplace_back(line_number, line, file);
            ++line_number;
        }
    }
    const std::string& path() const { return path_; }

    // return empty if number is more than number of lines in file
    const source_line& get_line(unsigned int number) const
    {
        if (number > lines_.size())
            return lines_[0];
        return lines_[number];
    }

private:
    const std::string path_;
    std::vector<source_line> lines_;
};

// a symbol in binary @file with given @name, at address @pc, @size bytes, with
// @code
class symbol
{
public:
    symbol(uint64_t id, const std::string& name, uint64_t pc, size_t size,
           const uint8_t* code, binary_file& file)
        : id_(id), name_(name), pc_(pc), size_(size), code_(code), file_(file)
    {
    }

    uint64_t id() const { return id_; }
    const std::string& name() const { return name_; }
    uint64_t pc() const { return pc_; }
    size_t size() const { return size_; }
    const uint8_t* code() const { return code_; }
    binary_file& file() const { return file_; }

private:
    uint64_t id_;
    std::string name_;
    uint64_t pc_;
    size_t size_;
    const uint8_t* code_;
    binary_file& file_;
};

// a binary file at @path references several symbols
class binary_file
{
public:
    binary_file(const std::string& path) : path_(path) {}

    void add_symbol(symbol& s) { symbols_.emplace_back(&s); }

    const std::string& path() const { return path_; }
    const std::vector<symbol*>& symbols() const { return symbols_; }
private:
    std::string path_;
    std::vector<symbol*> symbols_;
};

// a single instruction in the program, one per pc
class instruction
{
public:
    using capstone_inst_ptr = std::unique_ptr<cs_insn, void (*)(cs_insn*)>;

    instruction(uint64_t id, class symbol& symbol,
                capstone_inst_ptr capstone_inst, const source_line* line)
        : id_(id), symbol_(&symbol), capstone_inst_(std::move(capstone_inst)),
          line_(line)
    {
    }

    uint64_t id() const { return id_; }
    class symbol& symbol() const { return *symbol_; }
    uint64_t pc() const { return capstone_inst().address; }
    const std::string str() const
    {
        return capstone_inst().mnemonic + std::string(" ") +
               capstone_inst().op_str;
    }
    size_t size() const { return capstone_inst().size; }
    const cs_insn& capstone_inst() const { return *capstone_inst_; }
    const source_line* line() const { return line_; }
    void set_symbol(class symbol& symbol) { symbol_ = &symbol; }

    static csh get_capstone_handle();
    // allocate a new capstone instruction
    static capstone_inst_ptr get_new_capstone_instruction();
private:
    uint64_t id_;
    class symbol* symbol_;
    capstone_inst_ptr capstone_inst_;
    const source_line* line_;
};

// a sequence of instruction without any branching
// different from a basic block (no single entry point)
// two translation_block may contains the same set of instructions (one of the
// blocks overlaps on the other)
class translation_block
{
public:
    enum class block_transition_type
    {
        START,      /* program start */
        SEQUENTIAL, /* execute sequentially code */
        JUMP,       /* jump to a different block (no call) */
        CALL,       /* call new function */
        RETURN      /* return from call */
    };

    translation_block(uint64_t id, uint64_t pc, size_t size, symbol& symbol)
        : id_(id), pc_(pc), size_(size), symbol_(&symbol)
    {
    }

    uint64_t id() const { return id_; }
    uint64_t pc() const { return pc_; }
    size_t size() const { return size_; }
    class symbol& symbol() const { return *symbol_; }
    const std::vector<instruction*>& instructions() const
    {
        return instructions_;
    }
    void set_symbol(class symbol& symbol)
    {
        symbol_ = &symbol;
        for (instruction* i : instructions_) {
            i->set_symbol(symbol);
        }
    }

    void add_instruction(instruction& i) { instructions_.emplace_back(&i); }

private:
    uint64_t id_;
    uint64_t pc_;
    size_t size_;
    class symbol* symbol_;
    std::vector<instruction*> instructions_;
};

using call_stack = std::vector<const instruction*>;

// interface for a plugin (interesting event functions must be overrided)
// instruction and translation_block references remains valid/the same all along
// program execution, thus their addresses can be used as identifiers.
class plugin
{
public:
    plugin(const std::string& name, const std::string& description)
        : name_(name), description_(description)
    {
    }
    virtual ~plugin() {}
    virtual void on_program_start() {}

    /* called for each block transition
     * if type is RETURN, @return_original_caller_tb is set to tb that was used
     * to call this function.
     * if type is START, @prev is null. */
    virtual void
    on_block_transition(translation_block& next, translation_block* prev,
                        translation_block::block_transition_type type,
                        translation_block* return_original_caller_tb)
    {
        (void)next;
        (void)prev;
        (void)type;
        (void)return_original_caller_tb;
    }
    virtual void on_block_enter(translation_block&) {}
    virtual void on_instruction_exec(translation_block&, instruction&) {}
    virtual void on_block_exit(translation_block&) {}
    virtual void on_program_end() {}
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }

protected:
    // get or create an instruction
    static instruction&
    get_instruction(uint64_t pc, symbol& sym,
                    instruction::capstone_inst_ptr capstone_inst);
    /* return current call_stack. Current instruction is not included,
     * only all callers that lead to current stack */
    static call_stack get_call_stack();

private:
    const std::string name_;
    const std::string description_;
};

// macro to register a plugin from @class_name
#define REGISTER_PLUGIN(class_name)                                            \
    static bool register_##class_name()                                        \
    {                                                                          \
        static class_name plugin;                                              \
        register_plugin(plugin);                                               \
        return true;                                                           \
    }                                                                          \
    static bool register_##class_name##_ = register_##class_name()

// function to register an existing plugin
void register_plugin(plugin& p);
