// kernel_calluser.h
// C++14 Header-Only
// 内核调用用户态函数的工具库(仅支持Win10 x64 later, 且原本就是从应用层Call上来的情况下)
#pragma once
#include <ntifs.h>
#include "process.h"
#include "memory_search.h"

namespace kernel_calluser
{
    // ============================================================================
    // 常量定义
    // ============================================================================

    constexpr ULONG USER_RETURN_ADDRESS_OFFSET = 0x48;
    constexpr ULONG USER_STACK_RESERVE = 0x98;
    constexpr ULONG USER_SHADOW_SPACE = 0x20;

    // ============================================================================
    // 结构定义
    // ============================================================================

    struct kthread_dummy {
        _DISPATCHER_HEADER header;                                      // 0x0000
        void* slist_fault_address;                                      // 0x0018
        UINT64 quantum_target;                                          // 0x0020
        void* initial_stack;                                            // 0x0028
        void* volatile stack_limit;                                     // 0x0030
        void* stack_base;                                               // 0x0038
        UCHAR padding_0[0x50];                                          // 0x0040
        _KTRAP_FRAME* trap_frame;                                       // 0x0090
        UCHAR padding_1[0x14C];                                         // 0x0098
        USHORT kernel_apc_disable;                                      // 0x01E4
        USHORT special_apc_disable;                                     // 0x01E6
        UCHAR padding_2[0x62];                                          // 0x01E8
        UCHAR apc_state_index;                                          // 0x024A
    };

    struct kernel_stack_segment {
        ULONG_PTR stack_base, stack_limit, kernel_stack, initial_stack, actual_limit;
    };

    struct kernel_stack_control {
        kernel_stack_segment current, previous;
    };

    struct callout_stack {
        UCHAR reserved[4];
        BOOLEAN force_delete_flag;
        UCHAR stack_count;
        UCHAR reserved_2[58];
        PVOID stack_pointers[1];
    };

    struct apc_state {
        UCHAR state_index;
        USHORT special_apc_disable, kernel_apc_disable;
    };

    // ============================================================================
    // 外部声明
    // ============================================================================

    EXTERN_C NTKERNELAPI callout_stack* KeAllocateCalloutStack(BOOLEAN large_stack, UCHAR processor);
    EXTERN_C NTKERNELAPI VOID KeFreeCalloutStack(callout_stack* stack);
    EXTERN_C void __KiCallUserMode2(ULONG_PTR* out_var_ptr, ULONG_PTR call_ctx, ULONG_PTR kstack_control);
    EXTERN_C ULONG g_kvas_enabled, g_kvashadow_offset;

    // ============================================================================
    // 全局变量
    // ============================================================================

    template<typename T = void>
    struct globals {
        static ULONG_PTR precall_addr, postcall_addr;
    };

    template<typename T> ULONG_PTR globals<T>::precall_addr = 0;
    template<typename T> ULONG_PTR globals<T>::postcall_addr = 0;

    // ============================================================================
    // 辅助函数
    // ============================================================================

    template<typename T>
    inline T* offset_ptr(ULONG_PTR base, LONG offset) {
        return reinterpret_cast<T*>(base + offset);
    }

    inline apc_state save_apc_state(kthread_dummy* thread) {
        return { thread->apc_state_index, thread->special_apc_disable, thread->kernel_apc_disable };
    }

    inline void clear_apc_state(kthread_dummy* thread) {
        thread->apc_state_index = 0;
        thread->special_apc_disable = thread->kernel_apc_disable = 0;
    }

    inline void restore_apc_state(kthread_dummy* thread, const apc_state& state) {
        thread->apc_state_index = state.state_index;
        thread->special_apc_disable = state.special_apc_disable;
        thread->kernel_apc_disable = state.kernel_apc_disable;
    }

    inline ULONG_PTR get_user_stack_ptr() {
        return reinterpret_cast<kthread_dummy*>(KeGetCurrentThread())->trap_frame->Rsp;
    }

    inline kernel_stack_control* setup_kernel_stack_control(ULONG_PTR kernel_stack, kthread_dummy* thread) {
        auto control = offset_ptr<kernel_stack_control>(kernel_stack, -static_cast<LONG>(sizeof(kernel_stack_control)));

        control->current = {
            kernel_stack,
            kernel_stack - KERNEL_STACK_SIZE,
            reinterpret_cast<ULONG_PTR>(thread->stack_base),
            reinterpret_cast<ULONG_PTR>(thread->stack_limit),
            0
        };

        control->previous = { reinterpret_cast<ULONG_PTR>(thread->initial_stack), 0, 0, 0, 0 };
        return control;
    }

    // ============================================================================
    // 核心调用函数
    // ============================================================================

    inline ULONG_PTR call_usermode_internal(ULONG_PTR func_ptr, ULONG_PTR user_rsp, void** call_data) {
        ULONG_PTR ret_val_ptr = 0;

        auto stack = KeAllocateCalloutStack(FALSE, 1);
        if (!stack || !stack->stack_count) return 0;

        auto thread = reinterpret_cast<kthread_dummy*>(PsGetCurrentThread());
        auto kernel_stack = reinterpret_cast<ULONG_PTR>(stack->stack_pointers[0]);

        if (kernel_stack && thread) {
            auto stack_control = setup_kernel_stack_control(kernel_stack, thread);

            *offset_ptr<ULONG_PTR>(user_rsp, USER_RETURN_ADDRESS_OFFSET) = globals<>::postcall_addr;
            call_data[0] = reinterpret_cast<void*>(globals<>::precall_addr);
            call_data[1] = reinterpret_cast<void*>(user_rsp);
            call_data[2] = reinterpret_cast<void*>(func_ptr);

            auto saved_state = save_apc_state(thread);
            clear_apc_state(thread);
            __KiCallUserMode2(&ret_val_ptr, reinterpret_cast<ULONG_PTR>(call_data), reinterpret_cast<ULONG_PTR>(stack_control));
            restore_apc_state(thread, saved_state);
        }

        KeFreeCalloutStack(stack);
        return ret_val_ptr ? *reinterpret_cast<ULONG_PTR*>(ret_val_ptr) : 0;
    }

    // ============================================================================
    // 公共接口 可自行扩展支持更多参数
    // ============================================================================

    __declspec(noinline)
        ULONG_PTR call_usermode_function(ULONG_PTR func_ptr, ULONG_PTR arg1 = 0, ULONG_PTR arg2 = 0, ULONG_PTR arg3 = 0, ULONG_PTR arg4 = 0)
    {
        void* call_data[9] = {};
        auto new_user_rsp = (get_user_stack_ptr() - USER_STACK_RESERVE) & 0xFFFFFFFFFFFFFFF0;

        *offset_ptr<ULONG_PTR>(new_user_rsp, USER_SHADOW_SPACE + (0 * 8)) = arg1;
        *offset_ptr<ULONG_PTR>(new_user_rsp, USER_SHADOW_SPACE + (1 * 8)) = arg2;
        *offset_ptr<ULONG_PTR>(new_user_rsp, USER_SHADOW_SPACE + (2 * 8)) = arg3;
        *offset_ptr<ULONG_PTR>(new_user_rsp, USER_SHADOW_SPACE + (3 * 8)) = arg4;

        return call_usermode_internal(func_ptr, new_user_rsp, call_data);
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    inline bool initialize() {
        auto user32 = process::module_list::find64(L"user32.dll");
        if (user32.base_address) {
            globals<>::postcall_addr = reinterpret_cast<ULONG_PTR>(
                memsearch::find_pattern_in_section(user32.base_address, ".text",
                    "45 33 C0 48 89 44 24 20 48 8D 4C 24 20")
                );
        }

        auto ntdll = process::module_list::find64(L"ntdll.dll");
        if (ntdll.base_address) {
            globals<>::precall_addr = reinterpret_cast<ULONG_PTR>(
                memsearch::find_pattern_in_section(ntdll.base_address, ".text",
                    "48 8B 4C 24 ?? 48 8B 54 24 ?? 4C")
                );
        }

        ULONG_PTR syscall_entry = __readmsr(0xC0000082);
        ULONG offset = *reinterpret_cast<ULONG*>(syscall_entry + 8) & 0xFF00;
        g_kvas_enabled = offset ? 1 : 0;
        g_kvashadow_offset = offset + 8;

        return globals<>::precall_addr && globals<>::postcall_addr;
    }

    // ============================================================================
    // 测试函数
    // ============================================================================

    inline void test_call() {
        if (!initialize()) {
            DbgPrintEx(77, 0, "[kernel_calluser] Initialization failed\n");
            return;
        }

        auto kernel32 = process::module_list::find64(L"kernel32.dll");
        if (!kernel32.base_address) return;

        auto func = kernelfunc::FindExport(kernel32.base_address, "IsBadReadPtr");
        if (func) {
            auto result = call_usermode_function(reinterpret_cast<ULONG_PTR>(func), 111, 222, 333, 444);
            DbgPrintEx(77, 0, "[kernel_calluser] Result: %p\n", reinterpret_cast<void*>(result));
        }
    }
}
