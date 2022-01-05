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

#include "EnumDesc.h"
#include "ClassDesc.h"
#include "FunctionDesc.h"

#define ENABLE_CALL_OVERRIDDEN_FUNCTION 1           // option to call overridden UFunction

/**
 * Descriptor types
 * 描述类型
 */
enum EDescType
{   
	DESC_NONE = 0,
	DESC_CLASS = 1,
	DESC_FUNCTION = 2,
	DESC_PROPERTY = 3,
	DESC_FIELD = 4,
	DESC_ENUM = 5,
};

/**
 * Reflection registry
 * 注册反射
 */
class FReflectionRegistry
{
public:
    ~FReflectionRegistry() { Cleanup(); }

	// 清理
    void Cleanup();

    // all other place should use this to found desc!
	// 获取类描述
    FClassDesc* FindClass(const char* InName);

	// 反注册类描述
    void TryUnRegisterClass(FClassDesc* ClassDesc);
	// 反注册类描述
    bool UnRegisterClass(FClassDesc *ClassDesc);
	// 注册类描述
    FClassDesc* RegisterClass(const char* InName);
	// 注册类描述
    FClassDesc* RegisterClass(UStruct *InStruct);

    // all other place should use this to found desc!
	// 获取Enum描述
    FEnumDesc* FindEnum(const char* InName);

	// 反注册Enum描述
    bool UnRegisterEnum(const FEnumDesc* EnumDesc);
	// 注册Enum描述
    FEnumDesc* RegisterEnum(const char* InName);
	// 注册Enum描述
    FEnumDesc* RegisterEnum(UEnum *InEnum);

	// 注册方法
    FFunctionDesc* RegisterFunction(UFunction *InFunction, int32 InFunctionRef = INDEX_NONE);
	// 反注册方法
    bool UnRegisterFunction(UFunction *InFunction);
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
	// 新增覆盖方法
    bool AddOverriddenFunction(UFunction *NewFunc, UFunction *OverriddenFunc);
	// 移除覆盖方法
    UFunction* RemoveOverriddenFunction(UFunction *NewFunc);
	// 获取覆盖方法
    UFunction* FindOverriddenFunction(UFunction *NewFunc);
#endif

	// 通知UObject删除
    bool NotifyUObjectDeleted(const UObjectBase* InObject);

	// 新增到描述Set
	void AddToDescSet(void* Desc, EDescType type);
	// 从描述Set移除
	void RemoveFromDescSet(void* Desc);
	// 描述是否有效
	bool IsDescValid(void* Desc, EDescType type);
	// 描述是否有效同时检测Object
    bool IsDescValidWithObjectCheck(void* Desc, EDescType type);

	// 新增到GCSet
    void AddToGCSet(const UObject* InObject);
	// 从GCSet移除
    void RemoveFromGCSet(const UObject* InObject);
	// 是否在GCSet
    bool IsInGCSet(const UObject* InObject);

	// 新增到类白名单
    void AddToClassWhiteSet(const FString& ClassName);
	// 从类白名单移除
    void RemoveFromClassWhiteSet(const FString& ClassName);
	// 是否在类白名单
    bool IsInClassWhiteSet(const FString& ClassName);

private:
	// 注册类内部实现
    FClassDesc* RegisterClassInternal(const FString &ClassName, UStruct *Struct, FClassDesc::EType Type);

    TMap<FName, FClassDesc*> Name2Classes;
    TMap<UStruct*, FClassDesc*> Struct2Classes;
    TMap<FName, FEnumDesc*> Enums;
    TMap<UFunction*, FFunctionDesc*> Functions;
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
    TMap<UFunction*, UFunction*> OverriddenFunctions;
#endif

	TMap<void*, EDescType> DescSet;
    TMap<const UObject*, bool> GCSet;
    TMap<const FString, bool> ClassWhiteSet;
};

extern FReflectionRegistry GReflectionRegistry;
