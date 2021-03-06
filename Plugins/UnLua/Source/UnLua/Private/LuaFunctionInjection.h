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

#include "CoreUObject.h"
#include "UnLuaCompatibility.h"

// opcode
enum
{
    EX_CallLua = EX_Max - 1
};

class FLuaInvoker
{
public:
    // This macro is used to declare a thunk function in autogenerated boilerplate code
    // #define DECLARE_FUNCTION(func) static void func( UObject* Context, FFrame& Stack, RESULT_DECL )
    DECLARE_FUNCTION(execCallLua);
};

// 是否可覆盖
bool IsOverridable(UFunction *Function);
// 获取覆盖方法
void GetOverridableFunctions(UClass *Class, TMap<FName, UFunction*> &Functions);
// 拷贝UFunction
UFunction* DuplicateUFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName);
// 移除UFunction
void RemoveUFunction(UFunction *Function, UClass *OuterClass);
// 覆盖UFunction
void OverrideUFunction(UFunction *Function, FNativeFuncPtr NativeFunc, void *Userdata, bool bInsertOpcodes = true);
