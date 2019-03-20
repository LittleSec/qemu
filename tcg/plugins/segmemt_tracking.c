#include <stdio.h>
#include "tcg-plugin.h"
#include "wycinwyc.h"
#include "stl/vector.h"

extern vector * mappings; // mappings.item.memory_range

// extern void PrintVector(vector *head);

// extern vector *CreateNodeVector(const vector_item data);

// extern vector *PushBackVector(const vector_item data, vector *head);

// extern void SwapVectorNode(vector *p1, vector *p2);

// extern vector *GetVectorEnd(vector *head);

// extern int CmpVectorNode(const vector p1, const vector p2);

// extern void QuickSortVector(vector *head, vector *end);

// extern void BubbleSortVector(vector *head);

// extern vector *DestoryVector(vector *head);

// extern vector *PopBackVector(vector *head);

// extern size_t GetVectorSize(vector *head);

// extern vector* ReverseVector(vector *head);

static uint32_t memory_op_size(TCGMemOp memflags)
{
    switch (memflags & MO_SIZE)
    {
    case MO_8:
        return 1;
    case MO_16:
        return 2;
    case MO_32:
        return 4;
    case MO_64:
        return 8;
    }
    assert(false);
    return 0;
}

void phys_mem_write_segment_cb(CPUArchState *env, target_ulong pc, target_ulong addr, target_ulong size)
{
    target_ulong env_pc = env->regs[15];
    vector *m = mappings;
    for (; m != NULL; m = m->next)
    {
        if (m->item.mr.address > addr + size)
            break;

        if ((m->item.mr.address < addr + size) && (addr + size < m->item.mr.address + m->item.mr.size))
        {
            if (!(m->item.mr.perms & 2))
            {
                printf("[!] Found write to non-readable address (0x%08x) at pc=0x%08x\n", addr, env_pc);
            }
            else
            {
                return;
            }
        }
    }
    printf("[!] Found write to non-mapped address (0x%08x) at pc=0x%08x\n", addr, env_pc);
}

void phys_mem_read_segment_cb(CPUArchState *env, target_ulong pc, target_ulong addr, target_ulong size)
{
    target_ulong env_pc = env->regs[15];
    vector *m = mappings;
    for (; m != NULL; m = m->next)
    {
        if (m->item.mr.address > addr + size)
            break;

        if ((m->item.mr.address < addr + size) && (addr + size < m->item.mr.address + m->item.mr.size))
        {
            if (!(m->item.mr.perms & 4))
            {
                printf("[!] Found read from non-readable address (0x%08x) at pc=0x%08x\n", addr, env_pc);
            }
            else
            {
                return;
            }
        }
    }
    printf("[!] Found read from non-mapped address (0x%08x) at pc=0x%08x\n", addr, env_pc);
}

static void after_gen_opc(const TCGPluginInterface *tpi, const TPIOpCode *op)
{
    const TCGOpcode opc = op->opcode->opc;
    uint64_t pc = op->pc;

    bool is_load = false;

    // detect load/store
    switch (opc)
    {
    // load/store from guest memory
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        is_load = true;
        break;
    case INDEX_op_qemu_st_i64:
    case INDEX_op_qemu_st_i32:
        is_load = false;
        break;
    default:
        return;
    }

    const TCGMemOpIdx flags = op->opargs[2];
    const TCGMemOp memflags = get_memop(flags);
    uint32_t memory_size = memory_op_size(memflags);

    CPUArchState *env = tpi_current_cpu_arch(tpi);
    TCGv_ptr t_env = tcg_const_ptr(env);
    TCGv_i64 t_pc = tcg_const_i64(pc);
    TCGArg addr = op->opargs[1];
    TCGv_i32 t_size = tcg_const_i32(memory_size);

    TCGTemp *args[] = {tcgv_ptr_temp(t_env), tcgv_i64_temp(t_pc),
                       arg_temp(addr), tcgv_i32_temp(t_size)};

    if (is_load)
    {
        tcg_gen_callN(phys_mem_read_segment_cb, TCG_CALL_DUMMY_ARG,
                      sizeof(args) / sizeof(args[0]), args);
    }
    else
    {
        tcg_gen_callN(phys_mem_write_segment_cb, TCG_CALL_DUMMY_ARG,
                      sizeof(args) / sizeof(args[0]), args);
    }

    tcg_temp_free_ptr(t_env);
    tcg_temp_free_i64(t_pc);
    tcg_temp_free_i32(t_size);
}

void tpi_init(TCGPluginInterface *tpi)
{
    memory_range mr1 = {1610612736, 2097152, 5, 1};
    memory_range mr2 = {0, 2097152, 5, 1};
    memory_range mr3 = {536805376, 262144, 6, 0};
    memory_range mr4 = {2684289024, 262144, 6, 0};
    memory_range mr5 = {1073741824, 1342177280, 6, 0};
    // PrintVector(mappings);

    vector_item vit;
    vit.mr = mr1;
    mappings = CreateNodeVector(vit);
    vit.mr = mr2;
    mappings = PushBackVector(vit, mappings);
    vit.mr = mr3;
    mappings = PushBackVector(vit, mappings);
    vit.mr = mr4;
    mappings = PushBackVector(vit, mappings);
    vit.mr = mr5;
    mappings = PushBackVector(vit, mappings);
    // PrintVector(mappings);
    // vector* mapend = GetVectorEnd(mappings);
    // QuickSortVector(mappings, mapend);
    BubbleSortVector(mappings);
    
    TPI_INIT_VERSION(tpi);
    TPI_DECL_FUNC_4(tpi, phys_mem_write_segment_cb, void, ptr, i64, i64, i64);
    TPI_DECL_FUNC_4(tpi, phys_mem_read_segment_cb, void, ptr, i64, i64, i64);
    tpi->after_gen_opc = after_gen_opc;
}