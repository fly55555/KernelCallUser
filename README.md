# Kernel CallUser

Windows 内核态调用用户态函数的 C++14 Header-Only 工具库。

## 功能特性

- 从内核态安全调用用户态函数
- Header-Only 设计，无需额外编译，无stl依赖，process.h功能需要自己实现!
- 使用未文档化的KeAllocateCalloutStack&KeFreeCalloutStack替代未导出的MmCreateKernelStack&MmDeleteKernelStack
- 支持最多个参数传递
- 自动管理 APC 状态和内核栈
- 符合 x64 calling convention

## 系统要求

- Windows 10 及以上
- 必须在从用户态进入内核态的线程上下文中调用
- 需要 WDK 开发环境
- C++14 或更高版本

## 使用方法

### 基本用法

```cpp
#include "kernel_calluser.h"

// 初始化（在进程上下文空间调用一次）
if (!kernel_calluser::initialize()) {
    // 初始化失败处理
    return STATUS_UNSUCCESSFUL;
}

// 调用用户态函数 可自行扩展更多参数
auto result = kernel_calluser::call_usermode_function(
    user_function_address,
    arg1,  // 参数1
    arg2,  // 参数2
    arg3,  // 参数3
    arg4   // 参数4
);

## 鸣谢
DoubleCall项目提供的思路以及KernelDwm项目提供的Kva兼容支持:
https://github.com/wbaby/DoubleCallBack
https://github.com/cs1ime/KernelDwm
