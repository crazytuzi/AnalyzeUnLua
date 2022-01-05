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

#include "ClassDesc.h"
#include "FieldDesc.h"
#include "PropertyDesc.h"
#include "FunctionDesc.h"
#include "ReflectionRegistry.h"
#include "LuaCore.h"
#include "LuaContext.h"
#include "DefaultParamCollection.h"
#include "UnLuaManager.h"
#include "UnLua.h"

/**
 * Class descriptor constructor
 */
FClassDesc::FClassDesc(UStruct *InStruct, const FString &InName, EType InType)
    : Struct(InStruct), ClassName(InName), Type(InType), UserdataPadding(0), Size(0), RefCount(0), Locked(false),FunctionCollection(nullptr)
{   
	GReflectionRegistry.AddToDescSet(this, DESC_CLASS);

    if (InType == EType::CLASS)
    {
        Size = Struct->GetStructureSize();

        // register implemented interfaces
        // 注册实现的接口
        for (FImplementedInterface &Interface : Class->Interfaces)
        {
            FClassDesc *InterfaceClass = GReflectionRegistry.RegisterClass(Interface.Class);
            RegisterClass(*GLuaCxt, Interface.Class);
        }

        FunctionCollection = GDefaultParamCollection.Find(*ClassName);
    }
    else if (InType == EType::SCRIPTSTRUCT)
    {
        UScriptStruct::ICppStructOps *CppStructOps = ScriptStruct->GetCppStructOps();
        int32 Alignment = CppStructOps ? CppStructOps->GetAlignment() : ScriptStruct->GetMinAlignment();
        Size = CppStructOps ? CppStructOps->GetSize() : ScriptStruct->GetStructureSize();
        // 为userdata计算内存对齐
        UserdataPadding = CalcUserdataPadding(Alignment);       // calculate padding size for userdata
    }
}

/**
 * Class descriptor destructor
 */
FClassDesc::~FClassDesc()
{   
#if UNLUA_ENABLE_DEBUG != 0
    UE_LOG(LogUnLua, Log, TEXT("~FClassDesc : %s,%p,%d"), *GetName(), this, RefCount);
#endif
    
    // 确保Lua栈正确清理
    UnLua::FAutoStack  AutoStack;              // make sure lua stack is cleaned

	GReflectionRegistry.RemoveFromDescSet(this);

    // remove refs to class,etc ufunction/delegate
    // if (GLuaCxt->IsUObjectValid(Class))
    {
        UUnLuaManager* UnLuaManager = GLuaCxt->GetManager();
        if (UnLuaManager)
        {
            UnLuaManager->CleanUpByClass(Class);
        }
    }

    // remove lua side class tables
    // 移除Lua侧表
    FTCHARToUTF8 Utf8ClassName(*ClassName);
    // 清理关联的Lua元表
    ClearLibrary(*GLuaCxt, Utf8ClassName.Get());            // clean up related Lua meta table
    // 清理加载的Lua模块
    ClearLoadedModule(*GLuaCxt, Utf8ClassName.Get());       // clean up required Lua module

    // remove descs within classdesc,etc property/function
    for (TMap<FName, FFieldDesc*>::TIterator It(Fields); It; ++It)
    {
        delete It.Value();
    }
    for (FPropertyDesc *Property : Properties)
    {   
        if (GReflectionRegistry.IsDescValid(Property,DESC_PROPERTY))
        {
            delete Property;
        }
    }
    for (FFunctionDesc *Function : Functions)
    {
        if (GReflectionRegistry.IsDescValid(Function,DESC_FUNCTION))
        {   
            delete Function;
        }
    }

    Fields.Empty();
    Properties.Empty();
    Functions.Empty();
}

void FClassDesc::AddRef()
{   
    TArray<FClassDesc*> DescChain;
    GetInheritanceChain(DescChain);

    // 新增自身
    DescChain.Insert(this, 0);   // add self
    for (int i = 0; i < DescChain.Num(); ++i)
    {
        DescChain[i]->RefCount++;
    }
}

void FClassDesc::SubRef()
{ 
    TArray<FClassDesc*> DescChain;
    GetInheritanceChain(DescChain);

    // 新增自身
    DescChain.Insert(this, 0);   // add self
    for (int i = 0; i < DescChain.Num(); ++i)
    {
        DescChain[i]->RefCount--;
    }
}


void FClassDesc::AddLock()
{
    TArray<FClassDesc*> DescChain;
    GetInheritanceChain(DescChain);

    // 新增自身
    DescChain.Insert(this, 0);   // add self
    for (int i = 0; i < DescChain.Num(); ++i)
    {
        DescChain[i]->Locked = true;
    }
}

void FClassDesc::ReleaseLock()
{
    TArray<FClassDesc*> DescChain;
    GetInheritanceChain(DescChain);

    // 新增自身
    DescChain.Insert(this, 0);   // add self
    for (int i = 0; i < DescChain.Num(); ++i)
    {
        DescChain[i]->Locked = false;
    }
}

bool FClassDesc::IsLocked()
{
    return Locked;
}

FFieldDesc* FClassDesc::FindField(const char* FieldName)
{
    FFieldDesc** FieldDescPtr = Fields.Find(FieldName);
    return FieldDescPtr ? *FieldDescPtr : nullptr;
}

/**
 * Register a field of this class
 * 根据FieldName获取对应Field的反射信息
 * 借助UE4的反射机制，根据Field的字符串名字找到了对应了FProperty或UFunction，并缓存在Unlua自己的数据结构中，供后续取用
 */
FFieldDesc* FClassDesc::RegisterField(FName FieldName, FClassDesc *QueryClass)
{
    if (!Struct)
    {
        return nullptr;
    }

    FFieldDesc *FieldDesc = nullptr;
    FFieldDesc **FieldDescPtr = Fields.Find(FieldName);
    // 先去UnLua反射信息FClassDesc的缓存里去找是不是已有，有就直接返回了，没有就进到else里创建
    if (FieldDescPtr)
    {
        FieldDesc = *FieldDescPtr;
    }
    else
    {
        // a property or a function ?
        // 通过UE4的反射接口，根据名字尝试去获取FProperty，注意到，这里的Struct是保存在FClassDesc里的，是最开始GReflectionRegistry.RegisterClass的时候存进去的
        FProperty *Property = Struct->FindPropertyByName(FieldName);
        // 如果根据名字获取不到FProperty，则尝试去获取UFunction
        UFunction *Function = (!Property && Type == EType::CLASS) ? Class->FindFunctionByName(FieldName) : nullptr;
        bool bValid = Property || Function;
        // 如果要找的Field既不是FProperty，又不是UFunction，而且是结构体类型，且不是Native的，则进一步继续遍历这个结构体，找到匹配的名字赋值给Property
        if (!bValid && Type == EType::SCRIPTSTRUCT && !Struct->IsNative())
        {
            FString FieldNameStr = FieldName.ToString();
            const int32 GuidStrLen = 32;
            const int32 MinimalPostfixlen = GuidStrLen + 3;
            for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); PropertyIt; ++PropertyIt)
            {
                FString DisplayName = (*PropertyIt)->GetName();
                if (DisplayName.Len() > MinimalPostfixlen)
                {
                    DisplayName = DisplayName.LeftChop(GuidStrLen + 1);
                    int32 FirstCharToRemove = INDEX_NONE;
                    if (DisplayName.FindLastChar(TCHAR('_'), FirstCharToRemove))
                    {
                        DisplayName = DisplayName.Mid(0, FirstCharToRemove);
                    }
                }

                if (DisplayName == FieldNameStr)
                {
                    Property = *PropertyIt;
                    break;
                }
            }

            bValid = Property != nullptr;
        }
        // 如果这三种情况尝试过了，还没找到，就退出
        if (!bValid)
        {
            return nullptr;
        }

        // 获取Field的来源，如果有则继续
        UStruct *OuterStruct = Property ? Cast<UStruct>(GetPropertyOuter(Property)) : Cast<UStruct>(Function->GetOuter());
        if (OuterStruct)
        {
            // 如果属性或方法来源不是自己，说明这个Field来自于基类，则把它的基类再注册一遍，返回它的基类的FieldDesc
            if (OuterStruct != Struct)
            {   
                FClassDesc *OuterClass = (FClassDesc*)GReflectionRegistry.RegisterClass(OuterStruct);
                check(OuterClass);
                return OuterClass->RegisterField(FieldName, QueryClass);
            }

            // create new Field descriptor
            // 如果Field的来源是自己，则创建一个FFieldDesc，和FClassDesc建立映射
            FieldDesc = new FFieldDesc;
            FieldDesc->QueryClass = QueryClass;
            FieldDesc->OuterClass = this;
            // 将反射结果存入Properties或Functions中，并在FieldDesc中记住FieldIndex
            // 正数FieldIndex表示是Property，负数FieldIndex表示是Function（从lua栈index来的灵感？）
            // 之所以要++一下，应该是为了避免FieldIndex为0区分不出正负了
            Fields.Add(FieldName, FieldDesc);
            if (Property)
            {
                FieldDesc->FieldIndex = Properties.Add(FPropertyDesc::Create(Property));        // index of property descriptor
                ++FieldDesc->FieldIndex;
            }
            else
            {
                check(Function);
                FParameterCollection *DefaultParams = FunctionCollection ? FunctionCollection->Functions.Find(FieldName) : nullptr;
                FieldDesc->FieldIndex = Functions.Add(new FFunctionDesc(Function, DefaultParams, INDEX_NONE));  // index of function descriptor
                ++FieldDesc->FieldIndex;
                FieldDesc->FieldIndex = -FieldDesc->FieldIndex;
            }
        }
    }
    return FieldDesc;
}

/**
 * Get class inheritance chain
 */
void FClassDesc::GetInheritanceChain(TArray<FString> &InNameChain, TArray<UStruct*> &InStructChain)
{
    check(Type != EType::UNKNOWN);

    InNameChain.Empty();
    InStructChain.Empty();

    if (GLuaCxt->IsUObjectValid(Struct))
    {   
        if (NameChain.Num() <= 0)
        {
            UStruct* SuperStruct = Struct->GetInheritanceSuper();
            while (SuperStruct)
            {
                FString Name = FString::Printf(TEXT("%s%s"), SuperStruct->GetPrefixCPP(), *SuperStruct->GetName());
                NameChain.Add(Name);
                StructChain.Add(SuperStruct);
                SuperStruct = SuperStruct->GetInheritanceSuper();
            }
        }

        InNameChain = NameChain;
        InStructChain = StructChain;
    }
}

void FClassDesc::GetInheritanceChain(TArray<FClassDesc*>& DescChain)
{
    TArray<FString> InNameChain;
    TArray<UStruct*> InStructChain;
    GetInheritanceChain(InNameChain, InStructChain);

    for (int i = 0; i < NameChain.Num(); ++i)
    {
        FClassDesc* ClassDesc = GReflectionRegistry.FindClass(TCHAR_TO_UTF8(*NameChain[i]));
        if (ClassDesc)
        {
            DescChain.Add(ClassDesc);
        }
        else
        {
            UE_LOG(LogUnLua,Warning,TEXT("GetInheritanceChain : ClassDesc %s in inheritance chain %s not found"), *NameChain[i],*GetName());
        }
    }
}

FClassDesc::EType FClassDesc::GetType(UStruct* InStruct)
{
    EType Type = EType::UNKNOWN;
    UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InStruct);
    if (ScriptStruct)
    {
        Type = EType::SCRIPTSTRUCT;
    }
    else
    {
        UClass* Class = Cast<UClass>(InStruct);
        if (Class)
        {
            Type = EType::CLASS;
        }
    }
    return Type;
}
