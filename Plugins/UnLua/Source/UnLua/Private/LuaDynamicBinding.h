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

/**
 * UObject动态绑定Lua
 */
struct FLuaDynamicBinding
{
    FLuaDynamicBinding()
        : Class(nullptr), InitializerTableRef(INDEX_NONE)
    {}

    // 检验有效性
    bool IsValid(UClass *InClass) const;

    UClass *Class;
    FString ModuleName;
    // 初始化表,如果有,传入之前,会添加引用
    int32 InitializerTableRef;

    struct FLuaDynamicBindingStackNode
    {
        UClass *Class;
        FString ModuleName;
        int32 InitializerTableRef;
    };

    TArray<FLuaDynamicBindingStackNode> Stack;

    bool Push(UClass *InClass, const TCHAR *InModuleName, int32 InInitializerTableRef);
    int32 Pop();
};

extern FLuaDynamicBinding GLuaDynamicBinding;

struct lua_State;

class FScopedLuaDynamicBinding
{
public:
    FScopedLuaDynamicBinding(lua_State *InL, UClass *Class, const TCHAR *ModuleName, int32 InitializerTableRef);
    ~FScopedLuaDynamicBinding();

private:
    lua_State *L;
    bool bValid;
};
