#include "plugin_api.h"

#include "json.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

/* as opposed to translation_block, basic_block offers guarantee that
 * any of its instructions are not in another basic_block (single entry/exit
 * point) */
class basic_block
{
public:
    enum class bb_type
    {
        EXIT_FUNC, /* ret */
        CALL,      /* call */
        OTHER,     /* jump or sequential execution */
    };

    basic_block(uint64_t id, translation_block& tb)
        : id_(id), tb_(tb), size_(tb_.size())
    {
    }

    uint64_t id() const { return id_; }
    uint64_t pc() const { return tb_.pc(); }
    size_t size() const { return size_; }
    class symbol& symbol() const { return tb_.symbol(); }
    const std::vector<basic_block*>& successors() const { return successors_; }
    bb_type type() const { return type_; }
    basic_block* loop_header() const { return loop_header_; }
    std::vector<instruction*> instructions() const
    {
        std::vector<instruction*> res;
        auto start =
            std::find_if(tb_.instructions().begin(), tb_.instructions().end(),
                         [&](instruction* i) { return i->pc() == pc(); });
        uint64_t end_pc = pc() + size();
        auto end =
            std::find_if(start, tb_.instructions().end(),
                         [&](instruction* i) { return i->pc() >= end_pc; });
        res.insert(res.end(), start, end);
        return res;
    }

    /* split current block with @new_bb (successor of current bb) */
    void split_block(basic_block& new_bb)
    {
        basic_block& orig_bb = *this;
        orig_bb.size_ -= new_bb.size();

        // report block chain
        new_bb.successors_ = orig_bb.successors_;
        orig_bb.successors_.clear();
        // chain new block to orig one
        orig_bb.chain_block(new_bb);

        new_bb.loop_header_ = orig_bb.loop_header_;

        switch (orig_bb.type()) {
        case bb_type::OTHER:
            /* current bb keeps this type */
            break;
        case bb_type::EXIT_FUNC:
        case bb_type::CALL:
            /* new bb get this type */
            new_bb.set_type(orig_bb.type());
            orig_bb.set_type(bb_type::OTHER);
            break;
        }
    }

    bool chain_block(basic_block& succ)
    {
        for (auto* s : successors_) {
            if (s == &succ)
                return false;
        }
        successors_.emplace_back(&succ);
        return true;
    }

    void set_type(bb_type type) { type_ = type; }
    void set_loop_header(basic_block* lh) { loop_header_ = lh; }

private:
    uint64_t id_;
    translation_block& tb_;
    size_t size_;
    bb_type type_ = bb_type::OTHER;
    std::vector<basic_block*> successors_;
    basic_block* loop_header_ = nullptr;
};

/* Loop detection algorithm used is:
 * A New Algorithm for Identifying Loops in Decompilation, by
 * Tao Wei, Jian Mao, Wei Zou, Yu Chen
 * http://lenx.100871.net/papers/loop-SAS.pdf
 */

struct bb_loop_info
{
    basic_block* loop_header = nullptr;
    uint64_t depth_first_search_pos = 0;
    bool visited = false;
};

using bb_loop_infos = std::unordered_map<basic_block*, bb_loop_info>;

static void tag_lhead(basic_block& b, basic_block* h, bb_loop_infos& infos);
static basic_block* trav_loops_dfs(basic_block& b0, uint64_t dfs_pos,
                                   bb_loop_infos& infos);

// procedure identify_loops (CFG G=(N,E,h0)):
//    foreach(Block b in N): // init
//        initialize(b); // zeroize flags & properties
//    trav_loops_DFS(h0,1);
static void identify_loops(basic_block* entry_block)
{
    if (!entry_block)
        return;

    bb_loop_infos infos;
    trav_loops_dfs(*entry_block, 0, infos);

    // mark new loop header
    for (auto& p : infos) {
        basic_block* bb_ptr = p.first;
        if (!bb_ptr)
            continue;
        bb_loop_info& info = p.second;
        bb_ptr->set_loop_header(info.loop_header);
    }
}

// function trav_loops_DFS (Block b0, int DFSP_pos):
////return: innermost loop header of b0
//    Mark b0 as traversed;
//    b0.DFSP_pos := DFSP_pos;//Mark b0’s position in DFSP
//    foreach (Block b in Succ(b0)):
//        if(b is not traversed):
//            // case(A), new
//            Block nh := trav_loops_DFS(b, DFSP_pos+1);
//            tag_lhead(b0, nh);
//        else:
//            if(b.DFSP_pos > 0): // b in DFSP(b0)
//                // case(B)
//                Mark b as a loop header;
//                tag_lhead(b0, b);
//            else if(b.iloop_header == nil):
//                // case(C), do nothing
//            else:
//                Block h := b.iloop_header;
//                if(h.DFSP_pos > 0): // h in DFSP(b0)
//                    // case(D)
//                    tag_lhead(b0, h);
//                else: // h not in DFSP(b0)
//                    // case(E), reentry
//                    Mark b and (b0,b) as re-entry;
//                    Mark the loop of h as irreducible;
//                    while (h.iloop_header!=nil):
//                        h := h.iloop_header;
//                        if(h.DFSP_pos > 0): // h in DFSP(b0)
//                            tag_lhead(b0, h);
//                            break;
//                        Mark the loop of h as irreducible;
//    b0.DFSP_pos := 0; // clear b0’s DFSP position
//    return b0.iloop_header;
static basic_block* trav_loops_dfs(basic_block& b0, uint64_t dfs_pos,
                                   bb_loop_infos& infos)
{
    bb_loop_info& b0_info = infos[&b0];
    b0_info.visited = true;
    b0_info.depth_first_search_pos = dfs_pos;
    for (basic_block* b : b0.successors()) {
        bb_loop_info& b_info = infos[b];
        if (!b_info.visited) {
            basic_block* nh = trav_loops_dfs(*b, dfs_pos + 1, infos);
            tag_lhead(b0, nh, infos);
        } else {
            if (b_info.depth_first_search_pos > 0) {
                // mark b as loop header
                tag_lhead(b0, b, infos);
            } else if (!b_info.loop_header) {
                // nothing
            } else {
                basic_block* h = b_info.loop_header;
                bb_loop_info* h_info_ptr = &infos[h];
                if (h_info_ptr->depth_first_search_pos > 0) {
                    tag_lhead(b0, h, infos);
                } else {
                    // Mark b and (b0,b) as re-entry
                    // Mark loop of h as irreducible
                    while (h_info_ptr->loop_header) {
                        h = h_info_ptr->loop_header;
                        h_info_ptr = &infos[h];
                        if (h_info_ptr->depth_first_search_pos > 0) {
                            tag_lhead(b0, h, infos);
                            break;
                        }
                        // Mark the loop of h as irreducible
                    }
                }
            }
        }
    }
    b0_info.depth_first_search_pos = 0;
    return b0_info.loop_header;
}

// procedure tag_lhead (Block b, Block h):
//    if(b == h or h == nil)
//        return;
//    Block cur1 := b, cur2 := h;
//    while (cur1.iloop_header!=nil):
//        Block ih := cur1.iloop_header;
//        if(ih == cur2)
//            return;
//        if(ih.DFSP_pos < cur2.DFSP_pos):
//            cur1.iloop_header := cur2;
//            cur1 := cur2;
//            cur2 := ih;
//        else:
//            cur1 := ih;
//   cur1.iloop_header := cur2;
static void tag_lhead(basic_block& b, basic_block* h, bb_loop_infos& infos)
{
    if (&b == h || !h)
        return;
    basic_block* cur1 = &b;
    basic_block* cur2 = h;
    bb_loop_info* cur1_info_ptr = &infos[cur1];
    while (cur1_info_ptr->loop_header) {
        basic_block* ih = cur1_info_ptr->loop_header;
        if (ih == cur2)
            return;
        bb_loop_info* ih_info_ptr = &infos[ih];
        bb_loop_info* cur2_info_ptr = &infos[cur2];
        if (ih_info_ptr->depth_first_search_pos <
            cur2_info_ptr->depth_first_search_pos) {
            cur1_info_ptr->loop_header = cur2;
            cur1 = cur2;
            cur1_info_ptr = cur2_info_ptr;
            cur2 = ih;
            cur2_info_ptr = ih_info_ptr;
        } else {
            cur1 = ih;
            cur1_info_ptr = ih_info_ptr;
        }
    }
    cur1_info_ptr->loop_header = cur2;
}

template <typename TypeWithId>
static void sort_vec_elem_with_id(std::vector<TypeWithId*>& vec)
{
    std::sort(vec.begin(), vec.end(), [](const auto* e1, const auto* e2) {
        return e1->id() < e2->id();
    });
}

template <typename T>
static std::vector<T*> get_vec_from_unordered_set(std::unordered_set<T*> set)
{
    std::vector<T*> vec;
    vec.reserve(set.size());
    vec.insert(vec.end(), set.begin(), set.end());
    return vec;
}

static std::vector<const source_line*> get_sorted_sources_from_instructions(
    const std::vector<instruction*>& instructions)
{
    std::unordered_set<const source_line*> src_set;
    for (auto* i : instructions) {
        const source_line* src = i->line();
        if (!src)
            continue;
        src_set.emplace(src);
    }

    std::vector<const source_line*> src_vec =
        get_vec_from_unordered_set(src_set);
    std::sort(src_vec.begin(), src_vec.end(),
              [](const auto& l1_ptr, const auto& l2_ptr) {
                  const source_line& l1 = *l1_ptr;
                  const source_line& l2 = *l2_ptr;
                  if (l1.file().path() == l2.file().path())
                      return l1.number() < l2.number();
                  return l1.file().path() < l2.file().path();
              });
    return src_vec;
}

static json json_one_source_line(const source_line* src, bool executed)
{
    json j;
    if (src) {
        j = {{"file", src->file().path()},
             {"line", src->number()},
             {"str", src->line()},
             {"executed", executed}};
    }
    return j;
}

static json json_one_instruction(const instruction& i, bool executed)
{
    // do not report instruction id
    json j_src;
    json j = {{"pc", i.pc()},
              {"size", i.size()},
              {"str", i.str()},
              {"src", json_one_source_line(i.line(), executed)},
              {"executed", executed}};
    return j;
}

static json json_one_block(const basic_block& bb)
{
    json j_succ = json::array();
    auto successors = bb.successors();
    sort_vec_elem_with_id(successors);
    for (auto* succ : successors)
        j_succ.emplace_back(succ->id());

    json j_inst = json::array();
    for (auto* inst : bb.instructions())
        j_inst.emplace_back(json_one_instruction(*inst, true));

    auto src = get_sorted_sources_from_instructions(bb.instructions());
    json j_src = json::array();
    for (auto* s : src)
        j_src.emplace_back(json_one_source_line(s, true));

    json loop_header;
    if (bb.loop_header())
        loop_header = bb.loop_header()->id();

    json j = {{"id", bb.id()},
              {"pc", bb.pc()},
              {"size", bb.size()},
              {"symbol", bb.symbol().id()},
              {"instructions", j_inst},
              {"successors", j_succ},
              {"loop_header", loop_header},
              {"src", j_src}};
    return j;
}

static json json_one_symbol(
    const symbol& s, std::vector<basic_block*>& sym_blocks,
    const std::vector<instruction*>& instructions, basic_block* entry_point,
    const std::unordered_set<symbol*>& successors,
    const std::unordered_set<const source_line*>& covered_source_lines,
    const std::unordered_set<instruction*>& covered_instructions)
{
    json j_instructions = json::array();
    for (instruction* i : instructions) {
        j_instructions.emplace_back(
            json_one_instruction(*i, covered_instructions.count(i) != 0));
    }

    json j_entry_point;
    if (entry_point)
        j_entry_point = entry_point->id();

    sort_vec_elem_with_id(sym_blocks);
    json j_blocks = json::array();
    for (const auto& b : sym_blocks)
        j_blocks.emplace_back(b->id());

    auto src = get_sorted_sources_from_instructions(instructions);
    json j_src = json::array();
    for (auto* s : src)
        j_src.emplace_back(
            json_one_source_line(s, covered_source_lines.count(s) != 0));

    json j_succ = json::array();
    auto vec_succ = get_vec_from_unordered_set(successors);
    sort_vec_elem_with_id(vec_succ);
    for (const auto* succ : vec_succ) {
        j_succ.emplace_back(succ->id());
    }

    json j_name;
    const std::string& name = s.name();
    if (!name.empty())
        j_name = name;

    json j_file;
    const std::string& file = s.file().path();
    if (!file.empty())
        j_file = file;

    json j = {{"id", s.id()},
              {"pc", s.pc()},
              {"size", s.size()},
              {"file", j_file},
              {"name", j_name},
              {"instructions", j_instructions},
              {"basic_blocks", j_blocks},
              {"successors", j_succ},
              {"entry_point", j_entry_point},
              {"src", j_src}};
    return j;
}

class plugin_function_call_graph : public plugin
{
public:
    plugin_function_call_graph()
        : plugin("function_call_graph", "compute call graph from symbols "
                                        "and outputs json description for it")
    {
    }

private:
    void
    on_block_transition(translation_block& next, translation_block* prev,
                        translation_block::block_transition_type type,
                        translation_block* return_original_caller_tb) override
    {
        using tt = translation_block::block_transition_type;
        using bt = basic_block::bb_type;

        switch (type) {
        case tt::START:
            return;
        case tt::SEQUENTIAL:
        case tt::JUMP: {
            add_transition(*prev, next);
        } break;
        case tt::CALL: {
            translation_block& caller_tb = *prev;
            translation_block& callee_tb = next;

            set_block_type(caller_tb, bt::CALL);
            set_as_entry_point(callee_tb);
            add_transition(caller_tb, callee_tb);
        } break;
        case tt::RETURN: {
            translation_block& caller_tb = *return_original_caller_tb;
            translation_block& callee_tb = *prev;
            translation_block& returned_tb = next;

            set_block_type(callee_tb, bt::EXIT_FUNC);
            add_transition(caller_tb, returned_tb);
        } break;
        }
    }

    void set_as_entry_point(translation_block& b)
    {
        symbol& sym = b.symbol();
        basic_block& bb = get_basic_block(b);
        entry_points_[&sym] = &bb;
    }

    void add_transition(translation_block& previous, translation_block& next)
    {
        basic_block& prev_bb_end = get_basic_block_ending(previous);
        basic_block& next_bb_start = get_basic_block(next);
        bool new_trans = prev_bb_end.chain_block(next_bb_start);
        if (new_trans)
            identify_loops(entry_points_[&previous.symbol()]);
    }

    void set_block_type(translation_block& b, basic_block::bb_type type)
    {
        basic_block& bb_start = get_basic_block(b);
        basic_block& bb_end = get_basic_block_ending(b);

        using bt = basic_block::bb_type;

        switch (type) {
        case bt::OTHER:
            bb_start.set_type(type);
            break;
        case bt::EXIT_FUNC:
        case bt::CALL:
            bb_end.set_type(type);
            break;
        }
    }

    basic_block& get_basic_block_ending(translation_block& tb)
    {
        basic_block& bb_start = get_basic_block(tb); // may split tb

        if (bb_start.size() == tb.size()) { // tb was not splitted
            return bb_start;
        }

        // tb was splitted
        uint64_t last_pc = tb.pc() + tb.size() - 1;
        basic_block& bb_end = *blocks_map_[last_pc];
        return bb_end;
    }

    /* get or create basic block, automatically split one if necessary. */
    basic_block& get_basic_block(translation_block& tb)
    {
        basic_block* previous_bb = nullptr;
        auto it = blocks_map_.find(tb.pc());
        if (it != blocks_map_.end()) { // a bb already covers this pc
            previous_bb = it->second;
            if (previous_bb->pc() == tb.pc()) // exact mapping
                return *previous_bb;
        }

        uint64_t new_id = basic_block_id_;
        ++basic_block_id_;

        // create a new basic block from scratch
        basic_block& new_bb = blocks_
                                  .emplace(std::piecewise_construct,
                                           std::forward_as_tuple(new_id),
                                           std::forward_as_tuple(new_id, tb))
                                  .first->second;
        if (previous_bb) { // jump in the middle of a block that already
                           // exists
            // split existing basic block
            previous_bb->split_block(new_bb);
        }

        // add mapping for new_bb
        for (uint64_t pc_it = new_bb.pc(); pc_it != new_bb.pc() + new_bb.size();
             ++pc_it) {
            basic_block*& mapped = blocks_map_[pc_it];
            if (mapped == previous_bb) { // jump in the middle of a block
                mapped = &new_bb;
            } else { // jump before a block that already exists
                new_bb.split_block(*mapped);
                break;
            }
        }
        return new_bb;
    }

    std::vector<basic_block*> get_vec_blocks()
    {
        std::vector<basic_block*> blocks;
        blocks.reserve(blocks_.size());
        for (auto& p : blocks_) {
            basic_block& bb = p.second;
            blocks.emplace_back(&bb);
        }
        return blocks;
    }

    json json_blocks(std::vector<basic_block*>& blocks)
    {
        json j = json::array();
        sort_vec_elem_with_id(blocks);

        for (auto* bb_ptr : blocks) {
            const basic_block& bb = *bb_ptr;
            j.emplace_back(json_one_block(bb));
        }

        return j;
    }

    json json_symbols(
        std::unordered_map<symbol*, std::unordered_set<basic_block*>>&
            symbols_to_blocks,
        std::unordered_map<symbol*, std::unordered_set<symbol*>>&
            symbols_successors,
        const std::unordered_set<const source_line*>& covered_source_lines,
        const std::unordered_set<instruction*>& covered_instructions)
    {
        json j = json::array();

        std::vector<symbol*> symbols;
        symbols.reserve(symbols_to_blocks.size());
        for (const auto& p : symbols_to_blocks)
            symbols.emplace_back(p.first);
        sort_vec_elem_with_id(symbols);

        for (auto* sym_ptr : symbols) {
            symbol& s = *sym_ptr;

            std::vector<basic_block*> sym_blocks =
                get_vec_from_unordered_set(symbols_to_blocks[&s]);

            basic_block* entry_point = entry_points_[&s];

            std::vector<instruction*> instructions;
            if (s.size() != 0) // disassemble whole symbol
            {
                csh handle = instruction::get_capstone_handle();
                instruction::capstone_inst_ptr cs_inst =
                    instruction::get_new_capstone_instruction();
                uint64_t pc = s.pc();
                size_t size = s.size();
                const uint8_t* code = s.code();
                while (
                    cs_disasm_iter(handle, &code, &size, &pc, cs_inst.get())) {
                    uint64_t i_pc = cs_inst->address;
                    instruction& i =
                        plugin::get_instruction(i_pc, s, std::move(cs_inst));
                    instructions.emplace_back(&i);
                    cs_inst = instruction::get_new_capstone_instruction();
                }
            }

            std::unordered_set<symbol*> successors = symbols_successors[&s];

            j.emplace_back(json_one_symbol(
                s, sym_blocks, instructions, entry_point, successors,
                covered_source_lines, covered_instructions));
        }

        return j;
    }

    void on_program_end() override
    {
        json j;
        std::vector<basic_block*> blocks = get_vec_blocks();
        j["basic_blocks"] = json_blocks(blocks);

        std::unordered_set<const source_line*> covered_source_lines;
        std::unordered_set<instruction*> covered_instructions;
        std::unordered_map<symbol*, std::unordered_set<basic_block*>>
            symbols_to_blocks;
        std::unordered_map<symbol*, std::unordered_set<symbol*>>
            symbols_successors;

        for (auto* b : blocks) {
            symbol* b_sym = &b->symbol();
            symbols_to_blocks[b_sym].emplace(b);
            for (auto* succ : b->successors()) {
                symbol* succ_sym = &succ->symbol();
                if (b_sym == succ_sym)
                    continue;
                symbols_successors[b_sym].emplace(succ_sym);
            }
            for (auto* i : b->instructions())
                covered_instructions.emplace(i);
        }
        for (auto* i : covered_instructions) {
            const source_line* src = i->line();
            if (!src)
                continue;
            covered_source_lines.emplace(src);
        }

        j["symbols"] = json_symbols(symbols_to_blocks, symbols_successors,
                                    covered_source_lines, covered_instructions);
        std::cerr << j.dump(4, ' ');
    }

    uint64_t basic_block_id_ = 0;
    std::unordered_map<uint64_t /* id */, basic_block> blocks_;
    std::unordered_map<uint64_t /* pc */, basic_block*> blocks_map_;
    std::unordered_map<symbol*, basic_block*> entry_points_;
};

REGISTER_PLUGIN(plugin_function_call_graph);
