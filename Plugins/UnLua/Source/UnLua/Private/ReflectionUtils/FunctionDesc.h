// Tencent is pleased to support the open source community by making UnLua available.
// 
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the MIT License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at
//
// http://opensource.org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#pragma once

#include "UnLuaBase.h"
#include "LuaContext.h"

#define ENABLE_PERSISTENT_PARAM_BUFFER 1            // option to allocate persistent buffer for UFunction's parameters

struct lua_State;
struct FParameterCollection;
class FPropertyDesc;

/**
 * Function descriptor
 * 方法描述
 */
class FFunctionDesc
{
    friend class FReflectionRegistry;

public:
    FFunctionDesc(UFunction *InFunction, FParameterCollection *InDefaultParams, int32 InFunctionRef = INDEX_NONE);
    ~FFunctionDesc();

    /**
     * Check the validity of this function
     * 检查这个方法的有效性
     *
     * @return - true if the function is valid, false otherwise
     */
    FORCEINLINE bool IsValid() const { return Function && GLuaCxt->IsUObjectValid(Function); }

    /**
     * Test if this function has return property
     * 测试这个函数是否有返回值
     *
     * @return - true if the function has return property, false otherwise
     */
    FORCEINLINE bool HasReturnProperty() const { return ReturnPropertyIndex > INDEX_NONE; }

    /**
     * Test if this function is a latent function
     * 测试这个函数是否是Latent方法
     *
     * @return - true if the function is a latent function, false otherwise
     */
    FORCEINLINE bool IsLatentFunction() const { return LatentPropertyIndex > INDEX_NONE; }

    /**
     * Get the number of properties (includes both in and out properties)
     * 获取参数的数量(包括输入参数和Out参数)
     *
     * @return - the number of properties
     */
    FORCEINLINE uint8 GetNumProperties() const { return Function->NumParms; }

    /**
     * Get the number of out properties
     * 获取Out参数的数量
     *
     * @return - the number of out properties. out properties means return property or non-const reference properties
     */
    FORCEINLINE uint8 GetNumOutProperties() const { return ReturnPropertyIndex > INDEX_NONE ? OutPropertyIndices.Num() + 1 : OutPropertyIndices.Num(); }

    /**
     * Get the number of reference properties
     * 获取引用参数的数量
     *
     * @return - the number of reference properties.
     */
    FORCEINLINE uint8 GetNumRefProperties() const { return NumRefProperties; }

    /**
     * Get the number of non-const reference properties
     * 获取非const引用参数的数量
     *
     * @return - the number of non-const reference properties.
     */
    FORCEINLINE uint8 GetNumNoConstRefProperties() const { return OutPropertyIndices.Num(); }

    /**
     * Get the 'true' function
     * 获取真正的方法
     *
     * @return - the UFunction
     */
    FORCEINLINE UFunction* GetFunction() const { return Function; }

    /**
     * Call Lua function that overrides this UFunction
     * 调用覆盖了UFunction的Lua方法
     *
     * @param Stack - script execution stack
     * @param RetValueAddress - address of return value
     * @param bRpcCall - whether this function is a RPC function
     * @param bUnpackParams - whether to unpack parameters from the stack
     * @return - true if the Lua function executes successfully, false otherwise
     */
    bool CallLua(UObject *Context, FFrame &Stack, void *RetValueAddress, bool bRpcCall, bool bUnpackParams);

    /**
     * Call this UFunction
     * 调用UFunction
     *
     * @param NumParams - the number of parameters
     * @param Userdata - user data, now it's only used for latent function and it must be a 'int32'
     * @return - the number of return values pushed on the stack
     */
    int32 CallUE(lua_State *L, int32 NumParams, void *Userdata = nullptr);

    /**
     * Fire the delegate
     * 执行单播
     *
     * @param NumParams - the number of parameters
     * @param FirstParamIndex - Lua index of the first parameter
     * @param ScriptDelegate - the delegate
     * @return - the number of return values pushed on the stack
     */
    int32 ExecuteDelegate(lua_State *L, int32 NumParams, int32 FirstParamIndex, FScriptDelegate *ScriptDelegate);

    /**
     * Fire the multicast delegate
     * 执行多播
     *
     * @param NumParams - the number of parameters
     * @param FirstParamIndex - Lua index of the first parameter
     * @param ScriptDelegate - the multicast delegate
     */
    void BroadcastMulticastDelegate(lua_State *L, int32 NumParams, int32 FirstParamIndex, FMulticastScriptDelegate *ScriptDelegate);

private:
    // 为调用UFunction准备参数
    void* PreCall(lua_State *L, int32 NumParams, int32 FirstParamIndex, TArray<bool> &CleanupFlags, void *Userdata = nullptr);
    // 实际调用
    int32 PostCall(lua_State *L, int32 NumParams, int32 FirstParamIndex, void *Params, const TArray<bool> &CleanupFlags);

    // 调用Lua内部实现
    bool CallLuaInternal(lua_State *L, void *InParams, FOutParmRec *OutParams, void *RetValueAddress) const;

    // 对应UFunction信息
    UFunction *Function;
    FString FuncName;
#if ENABLE_PERSISTENT_PARAM_BUFFER
    void *Buffer;
#endif
#if !SUPPORTS_RPC_CALL
    FOutParmRec *OutParmRec;
#endif
    // 函数的参数描述列表
    TArray<FPropertyDesc*> Properties;
    // 记录哪些属性是引用传递变量
    TArray<int32> OutPropertyIndices;
    // 默认参数信息
    FParameterCollection *DefaultParams;
    int32 ReturnPropertyIndex;
    int32 LatentPropertyIndex;
    // lua中对应函数地址
    int32 FunctionRef;
    uint8 NumRefProperties;
    uint8 NumCalls;                 // RECURSE_LIMIT is 120 or 250 which is less than 256, so use a byte...
    uint8 bStaticFunc : 1;
    uint8 bInterfaceFunc : 1;
    uint8 bHasDelegateParams : 1;
};
