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

#include "ClassDesc.h"

/**
 * Field descriptor
 * Field描述
 */
class FFieldDesc
{
    friend class FClassDesc;

public:
    // 是否有效
    FORCEINLINE bool IsValid() const { return FieldIndex != 0; }

    // 是否是属性
    FORCEINLINE bool IsProperty() const { return FieldIndex > 0; }

    // 是否是方法
    FORCEINLINE bool IsFunction() const { return FieldIndex < 0; }

    // 是否继承
    FORCEINLINE bool IsInherited() const { return OuterClass != QueryClass; }

    // 转属性
    FORCEINLINE FPropertyDesc* AsProperty() const { return FieldIndex > 0 ? OuterClass->GetProperty(FieldIndex - 1) : nullptr; }

    // 转方法
    FORCEINLINE FFunctionDesc* AsFunction() const { return FieldIndex < 0 ? OuterClass->GetFunction(-FieldIndex - 1) : nullptr; }

    // 获取Outer名
    FORCEINLINE FString GetOuterName() const { return OuterClass ? OuterClass->GetName() : TEXT(""); }

private:
    FFieldDesc()
        : QueryClass(nullptr), OuterClass(nullptr), FieldIndex(0)
    {}

    ~FFieldDesc() {}

    FClassDesc *QueryClass;
    FClassDesc *OuterClass;
    // field在Properties或Functions数组中的下标，正数表示property，负数表示function， 设计比较巧妙
    int32 FieldIndex;   // index in FClassDesc
};