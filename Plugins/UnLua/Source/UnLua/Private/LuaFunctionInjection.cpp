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

#include "LuaFunctionInjection.h"
#include "ReflectionUtils/ReflectionRegistry.h"
#include "Misc/MemStack.h"
#include "GameFramework/Actor.h"

/**
 * Custom thunk function to call Lua function
 * 自定义指令方法来调用Lua方法
 */
DEFINE_FUNCTION(FLuaInvoker::execCallLua)
{
    bool bUnpackParams = false;
    // 会获取到当前正在被调用的UFunction，即变量Func
    UFunction *Func = Stack.Node;
    FFunctionDesc *FuncDesc = nullptr;
    if (Stack.CurrentNativeFunction)
    {
        if (Func != Stack.CurrentNativeFunction)
        {
            Func = Stack.CurrentNativeFunction;
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
            FMemory::Memcpy(&FuncDesc, &Stack.CurrentNativeFunction->Script[1], sizeof(FuncDesc));
#endif
            bUnpackParams = true;
        }
        else
        {
            if (Func->GetNativeFunc() == (FNativeFuncPtr)&FLuaInvoker::execCallLua)
            {
                check(*Stack.Code == EX_CallLua);
                // 仅当NativeFunc时跳过EX_CallLua
                Stack.SkipCode(1);      // skip EX_CallLua only when called from native func
            }
        }
    }

    //!!!Fix!!!
    //find desc from classdesc
    // 根据Func寻找已注册的FuncDesc，如果是SHIPPING版本，就直接从字节码里面获取FunDesc指针，如果非SHIPPING，就去Map里面查找，或现场注册一个
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    if (!FuncDesc)
    {
        FMemory::Memcpy(&FuncDesc, Stack.Code, sizeof(FuncDesc));
        // 跳过FFunctionDesc指针
        Stack.SkipCode(sizeof(FuncDesc));       // skip 'FFunctionDesc' pointer
    }
#else
    FuncDesc = GReflectionRegistry.RegisterFunction(Func);
#endif

    bool bRpcCall = false;
#if SUPPORTS_RPC_CALL
    AActor *Actor = Cast<AActor>(Stack.Object);
    if (!Actor)
    {
        UActorComponent *ActorComponent = Cast<UActorComponent>(Stack.Object);
        if (ActorComponent)
        {
            Actor = ActorComponent->GetOwner();
        }
    }
    if (Actor)
    {
        /*ENetMode NetMode = Actor->GetNetMode();
        if ((Func->HasAnyFunctionFlags(FUNC_NetClient | FUNC_NetMulticast) && NetMode == NM_Client) || (Func->HasAnyFunctionFlags(FUNC_NetServer | FUNC_NetMulticast) && (NetMode == NM_DedicatedServer || NetMode == NM_ListenServer)))
        {
            bRpcCall = true;
        }*/

        int32 Callspace = Actor->GetFunctionCallspace(Func, nullptr);
        bRpcCall = Callspace & FunctionCallspace::Remote;
    }
#endif

    // Stack可以理解为蓝图虚拟机，是比较重要的数据结构，用于支撑蓝图调用功能
    // 里面存储了蓝图字节码和数据，在调用一个BlueprintEvent函数时，都会创建一个Stack
    // Stack会贯穿调用lua函数的整个过程，包括函数参数传递，函数返回值传递等
    bool bSuccess = FuncDesc->CallLua(Context, Stack, (void*)RESULT_PARAM, bRpcCall, bUnpackParams);
    if (!bSuccess && bUnpackParams)
    {
        FMemMark Mark(FMemStack::Get());
        void *Params = New<uint8>(FMemStack::Get(), Func->ParmsSize, 16);
        for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm) == CPF_Parm; ++It)
        {
            Stack.Step(Stack.Object, It->ContainerPtrToValuePtr<uint8>(Params));
        }
        Stack.SkipCode(1);          // skip EX_EndFunctionParms
    }
}

/**
 * Register thunk function for new opcode
 * 对新的opcode注册指令方法
 */
extern uint8 GRegisterNative(int32 NativeBytecodeIndex, const FNativeFuncPtr& Func);
// 添加了一个字节码EX_CallLua，execCallLua则被声明为实现该字节码的函数，同时也能作为普通C++函数使用，可谓一举两得。execCallLua函数功能就和名字一样，用于调用覆写的lua函数
static FNativeFunctionRegistrar CallLuaRegistrar(UObject::StaticClass(), "execCallLua", (FNativeFuncPtr)&FLuaInvoker::execCallLua);
static uint8 CallLuaBytecode = GRegisterNative(EX_CallLua, (FNativeFuncPtr)&FLuaInvoker::execCallLua);


/**
 * Whether the UFunction is overridable
 */
bool IsOverridable(UFunction *Function)
{
    check(Function);

    static const uint32 FlagMask = FUNC_Native | FUNC_Event | FUNC_Net;
    static const uint32 FlagResult = FUNC_Native | FUNC_Event;
    return Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) || (Function->FunctionFlags & FlagMask) == FlagResult;
}

/**
 * Get all UFUNCTIONs that can be overrode
 */
void GetOverridableFunctions(UClass *Class, TMap<FName, UFunction*> &Functions)
{
    if (!Class)
    {
        return;
    }

    // all 'BlueprintEvent'
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); It; ++It)
    {
        UFunction *Function = *It;
        if (IsOverridable(Function))
        {
            FName FuncName = Function->GetFName();
            UFunction **FuncPtr = Functions.Find(FuncName);
            if (!FuncPtr)
            {
                Functions.Add(FuncName, Function);
            }
        }
    }

    // all 'RepNotifyFunc'
    for (int32 i = 0; i < Class->ClassReps.Num(); ++i)
    {
        FProperty *Property = Class->ClassReps[i].Property;
        if (Property->HasAnyPropertyFlags(CPF_RepNotify))
        {
            UFunction *Function = Class->FindFunctionByName(Property->RepNotifyFunc);
            if (Function)
            {
                UFunction **FuncPtr = Functions.Find(Property->RepNotifyFunc);
                if (!FuncPtr)
                {
                    Functions.Add(Property->RepNotifyFunc, Function);
                }
            }
        }
    }
}

/**
 * Only used to get offset of 'Offset_Internal'
 * 只是用来获取Offset_Internal的偏移
 */
#if ENGINE_MAJOR_VERSION <= 4 && ENGINE_MINOR_VERSION < 25
struct FFakeProperty : public UField
#else
struct FFakeProperty : public FField
#endif
{
    int32       ArrayDim;
    int32       ElementSize;
    uint64      PropertyFlags;
    uint16      RepIndex;
    TEnumAsByte<ELifetimeCondition> BlueprintReplicationCondition;
    int32       Offset_Internal;
};

/**
 * 1. Duplicate template UFUNCTION
 * 2. Add duplicated UFUNCTION to class' function map
 * 3. Register 'FFunctionDesc'
 * 4. Add to root if necessary
 * 1. 复制临时UFunction
 * 2. 新增复制出来的UFunction到Class的FunctionMap中
 * 3. 注册FFunctionDesc
 * 4. 如果有必要新增到Root
 */
UFunction* DuplicateUFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
    static int32 Offset = offsetof(FFakeProperty, Offset_Internal);
    static FArchive Ar;         // dummy archive used for FProperty::Link()

    FObjectDuplicationParameters DuplicationParams(TemplateFunction, OuterClass);
    DuplicationParams.DestName = NewFuncName;
    DuplicationParams.InternalFlagMask &= ~EInternalObjectFlags::Native;
    UFunction *NewFunc = Cast<UFunction>(StaticDuplicateObjectEx(DuplicationParams));
    
    if (!FPlatformProperties::RequiresCookedData())
    {
        UMetaData::CopyMetadata(TemplateFunction, NewFunc);
    }
    NewFunc->Bind();
    NewFunc->StaticLink(true);

    OuterClass->AddFunctionToFunctionMap(NewFunc, NewFuncName);
    GReflectionRegistry.RegisterFunction(NewFunc);
    NewFunc->ClearInternalFlags(EInternalObjectFlags::Native);

    if (OuterClass->IsRooted() || GUObjectArray.IsDisregardForGC(OuterClass))
    {
        NewFunc->AddToRoot();
    }
    else
    {
        NewFunc->Next = OuterClass->Children;
        OuterClass->Children = NewFunc;
    }

    return NewFunc;
}

/**
 * 1. Remove duplicated UFUNCTION from class' function map
 * 2. Unregister 'FFunctionDesc'
 * 3. Remove from root if necessary
 * 4. Clear 'Native' flag if necessary
 * 1. 将复制出来的UFunction从Class的FunctionMap中移除
 * 2. 反注册FFunctionDesc
 * 3. 如果有必要,从Root上移除UFunction
 * 4. 如果有必要,清除Native标记
 */
void RemoveUFunction(UFunction* Function, UClass* OuterClass)
{
    UE_LOG(UnLuaDelegate, Verbose, TEXT("Clean %s"), *Function->GetName());

    if (OuterClass->IsValidLowLevel())
    {
#if UNLUA_ENABLE_DEBUG != 0
        const FString Result = OuterClass->FindFunctionByName(*Function->GetName()) ? "OK" : "Not Exists";
        UE_LOG(LogUnLua, Log, TEXT("RemoveUFunction: [%p], [%s] From Class : [%p], [%s] Result=%s"), Function, *Function->GetName(), OuterClass, *OuterClass->GetFullName(), *Result);
#endif
        OuterClass->RemoveFunctionFromFunctionMap(Function);

        if(OuterClass->Children == Function)
        {
            OuterClass->Children = Function->Next;
        }
        else
        {
            UField* Previous = OuterClass->Children;
            while(Previous && Previous->Next != Function)
                Previous = Previous->Next;
            if(Previous)
                Previous->Next = Function->Next;
        }
    }

    GReflectionRegistry.UnRegisterFunction(Function);
}

/**
 * 1. Replace thunk function
 * 2. Insert special opcodes if necessary
 * 1. 替换指令方法
 * 2. 如果有必要,插入特殊opcode
 */
void OverrideUFunction(UFunction *Function, FNativeFuncPtr NativeFunc, void *Userdata, bool bInsertOpcodes)
{
    if (!Function->HasAnyFunctionFlags(FUNC_Net) || Function->HasAnyFunctionFlags(FUNC_Native))
    {
        Function->SetNativeFunc(NativeFunc);
    }

    if (Function->Script.Num() < 1)
    {
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
        if (bInsertOpcodes)
        {
            // 把UFunction的C++函数指针和蓝图字节码调用函数都指向FLuaInvoker::execCallLua函数
            // 这样不管调用纯C++的RepNotify，还是BluePrintEvent，都能调用到execCallLua函数
            Function->Script.Add(EX_CallLua);
            int32 Index = Function->Script.AddZeroed(sizeof(Userdata));
            // 把FFunctionDesc直接拷贝到字节码中作为数据，这样会省一次map查找操作，有助于提升效率，属于用空间换时间
            FMemory::Memcpy(Function->Script.GetData() + Index, &Userdata, sizeof(Userdata));
            Function->Script.Add(EX_Return);
            Function->Script.Add(EX_Nothing);
        }
        else
        {
            int32 Index = Function->Script.AddZeroed(sizeof(Userdata));
            FMemory::Memcpy(Function->Script.GetData() + Index, &Userdata, sizeof(Userdata));
        }
#else
        // 需要在execCallLua中根据UFunction去GReflectionRegistry.RegisterFunction找FFunctionDesc
        Function->Script.Add(EX_CallLua);
        Function->Script.Add(EX_Return);
        Function->Script.Add(EX_Nothing);
#endif
    }
}
