// #include "callframe_tracking.h"


#include <stdio.h>
#include "tcg-plugin.h"
#include "vector_callframes.h"
#include <capstone/capstone.h>

// returns size (in bytes) of memory affected by operation
static uint32_t memory_op_size(TCGMemOp memflags)
{
    switch (memflags & MO_SIZE) {
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

// #ifdef TARGET_ARM
static bool return_pending;
static cs_insn last_insn;
static callframes *cfs;
static target_ulong prev_write_addr = 0;
static target_ulong prev_write_size = 0;
static target_ulong prev_write_frame = 0;

static target_ulong before_pc;

#define CF_TRACKING_DEBUG 0


static target_ulong find_frame_by_address(target_ulong addr){
    if ( !GetSize_callframes(cfs) )
        return 0;


    if (addr > cfs->cf)
        return 0;

    callframes *c = cfs;
    for(; c != NULL; c = c->next) {
        if (addr > c->cf)
            return c->cf;
    }
    return 0;
}


// int after_block_exec_callframe_cb(CPUState *cpu, TranslationBlock *tb){
static int after_block_exec_callframe_cb(const TCGPluginInterface *tpi){
    // CPUArchState *env = (CPUArchState *) cpu->env_ptr;
    CPUArchState *env = tpi_current_cpu_arch(tpi);
    uint32_t pc = env->regs[15];
#if CF_TRACKING_DEBUG
    printf("PC after: %x\n", pc);
#endif
    if (return_pending)
    {
        return_pending = false;
        if (before_pc == pc)
            return 0;
        cfs = PopBack_callframes(cfs);
        return_pending = false;
#if CF_TRACKING_DEBUG
        // CPUArchState *env = (CPUArchState *) cpu->env_ptr;
        CPUArchState *env = tpi_current_cpu_arch(tpi);
        printf("Ret  at %x - sp == %x\n", env->regs[15], env->regs[13]);
#endif
    }
    return 0;
}

// （物理）内存写前调用，如果检测到tb中有写操作==>tb执行前调用
// int phys_mem_write_callframe_cb(CPUState *cpu, target_ulong pc, target_ulong addr, target_ulong size, void *buf){
static int phys_mem_write_callframe_cb(CPUArchState *env, target_ulong pc, target_ulong addr, target_ulong size){
    // CPUArchState *env = (CPUArchState *) cpu->env_ptr;
    target_ulong env_pc = env->regs[15];
    target_ulong cur_frame = find_frame_by_address(addr);
    //Ensure that we already have a callframe stack and 
    //that both current and previous write are onto the stack
    if (GetSize_callframes(cfs) > 1 && cur_frame && prev_write_frame){
        if ( prev_write_addr + prev_write_size == addr){
            if ( cur_frame != prev_write_frame){
                printf("[!] Detected stack-corruputing memory write at 0x%08x!\n", env_pc);
                printf(" |  Previous_memory_access_address: 0x%08x\n", prev_write_addr);
                printf(" |  Current_memory_access_address: 0x%08x\n", addr);
                printf(" |  Previous_write_stack_frame: 0x%08x\n", prev_write_frame);
                printf(" |  Current_write_stack_frame: 0x%08x\n", cur_frame);
                return -1;
            }
        }
        prev_write_addr = 0;
        prev_write_size = 0;
        prev_write_frame = 0;
    }
    prev_write_addr = addr;
    prev_write_size = size;
    prev_write_frame = cur_frame;
    return 0;
}

// 对应(* tpi_after_gen_opc_t)(const TCGPluginInterface *tpi, const TPIOpCode *opcode)
// int before_block_exec_callframe_cb(CPUState *cpu, TranslationBlock *tb){
static void before_block_exec_callframe_cb(const TCGPluginInterface *tpi, const TPIOpCode *opcode){
    // CPUArchState *env = (CPUArchState *) cpu->env_ptr;
    CPUArchState *env = tpi_current_cpu_arch(tpi);
    const TCGOpcode opc = opcode->opcode->opc;
    const TCGMemOpIdx flags = opcode->opargs[2];
    const TCGMemOp memflags = get_memop(flags);
    uint32_t memory_size = memory_op_size(memflags);

    TCGv_ptr t_env = tcg_const_ptr(env);
    TCGv_i64 t_pc = tcg_const_i64(pc);
    TCGArg addr = opcode->opargs[1];
    TCGv_i32 t_size = tcg_const_i32(memory_size);

    TCGTemp *args[] = {tcgv_ptr_temp(t_env), tcgv_i64_temp(t_pc),
                    arg_temp(addr), tcgv_i32_temp(t_size)};
    switch (opc)
    {
        // 写物理host内存
        case INDEX_op_qemu_ld_i32:
        case INDEX_op_qemu_ld_i64: // qemu_ld/st_i32/i64 t0, t1, flags, memidx
        // Load data (at the guest address t1) into t0, or store data (in t0) at guest address t1
            tcg_gen_callN(phys_mem_write_callframe_cb, TCG_CALL_DUMMY_ARG,
                            sizeof(args) / sizeof(args[0]), args);
            tcg_temp_free_ptr(t_env);
            tcg_temp_free_i64(t_pc);
            tcg_temp_free_i32(t_size);
            break;
        default:
            break;
    }

    uint32_t pc = env->regs[15];
    cs_insn insn;
    before_pc = pc;
    tb = tpi->tb;
    std::vector<cs_insn> insns_vec = tb_insns_map[tb->pc];

    insn = insns_vec.at(insns_vec.size()-1); 
    if (insn.id == ARM_INS_LDMDB || insn.id == ARM_INS_POP ||  
       (insn.id == ARM_INS_MOV && insn.detail->arm.operands[0].reg == ARM_REG_PC)  || 
       (insn.id == ARM_INS_BX  && insn.detail->arm.operands[0].reg == ARM_REG_LR))
    {
#if CF_TRACKING_DEBUG
        printf("Ret-insn 0x%" PRIx64 ":\t%s\t\t%s\n", insn.address, insn.mnemonic, insn.op_str);
#endif
        last_insn = insn;
        return_pending = true;
    }
#if CF_TRACKING_DEBUG
    printf("PC before: %x\n", pc);
#endif
    TCGTemp *args[1];
    TCGv_ptr t_tpi = tcg_const_ptr(tpi);
    args[0] = tcgv_ptr_temp(t_tpi);
    TCGv_i64 tcgv_ret = tcg_temp_new_i64();
    tcg_gen_callN(after_block_exec_callframe_cb, tcgv_i64_temp(tcgv_ret), 1, args);
    tcg_temp_free_i64(tcgv_ret);
    tcg_temp_free_ptr(t_tpi);
}

// TODO
// void on_call_callframe_cb(CPUState *cpu, target_ulong pc){

//     if (before_pc == pc)
//         printf("this shouldnt happen");
        
//     CPUArchState *env = (CPUArchState *) cpu->env_ptr;
//     uint32_t sp = env->regs[13];
//     PushBack_callframes(sp, cfs);
// #if CF_TRACKING_DEBUG
//     printf("Call at %x - sp == %x\n", pc, sp);
//     callframes* c = cfs;
//     for(; c != NULL; c = c->next) 
//         printf("\t%x\n", c->cf);
// #endif
// }

// bool enable_callframe_tracking(void* self, panda_cb pcb){
//     panda_require("callstack_instr");
//     if (!init_callstack_instr_api())
//         return false;

//     pcb.phys_mem_before_write = phys_mem_write_callframe_cb;
//     panda_register_callback(self, PANDA_CB_PHYS_MEM_BEFORE_WRITE, pcb);

//     pcb.after_block_exec = after_block_exec_callframe_cb;
//     panda_register_callback(self, PANDA_CB_AFTER_BLOCK_EXEC, pcb);

//     pcb.before_block_exec = before_block_exec_callframe_cb;
//     panda_register_callback(self, PANDA_CB_BEFORE_BLOCK_EXEC, pcb);


//     PPP_REG_CB("callstack_instr", on_call, on_call_callframe_cb);
//     return true;
// }


void tpi_init(TCGPluginInterface *tpi){
    TPI_INIT_VERSION(tpi);

    TPI_DECL_FUNC_1(tpi, after_block_exec_callframe_cb, i64, ptr);
    TPI_DECL_FUNC_4(tpi, phys_mem_write_callframe_cb, void, ptr, i64, i64, i64);
    tpi->after_gen_opc = before_block_exec_callframe_cb;
}


// #endif
