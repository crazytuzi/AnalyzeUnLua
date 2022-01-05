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

#include "FunctionDesc.h"
#include "PropertyDesc.h"
#include "ReflectionRegistry.h"
#include "LuaCore.h"
#include "LuaContext.h"
#include "LuaFunctionInjection.h"
#include "DefaultParamCollection.h"
#include "UnLua.h"
#include "UnLuaLatentAction.h"

/**
 * Function descriptor constructor
 */
FFunctionDesc::FFunctionDesc(UFunction *InFunction, FParameterCollection *InDefaultParams, int32 InFunctionRef)
    : Function(InFunction), DefaultParams(InDefaultParams), ReturnPropertyIndex(INDEX_NONE), LatentPropertyIndex(INDEX_NONE)
    , FunctionRef(InFunctionRef), NumRefProperties(0), NumCalls(0), bStaticFunc(false), bInterfaceFunc(false)
{
	GReflectionRegistry.AddToDescSet(this, DESC_FUNCTION);

    check(InFunction);

    FuncName = InFunction->GetName();

    // 是否是静态方法
    bStaticFunc = InFunction->HasAnyFunctionFlags(FUNC_Static);         // a static function?

    UClass *OuterClass = InFunction->GetOuterUClass();
    if (OuterClass->HasAnyClassFlags(CLASS_Interface) && OuterClass != UInterface::StaticClass())
    {
        // 是否是接口方法4
        bInterfaceFunc = true;                                          // a function in interface?
    }

    bHasDelegateParams = false;
    // create persistent parameter buffer. memory for speed
    // 创建持久化参数缓冲区
#if ENABLE_PERSISTENT_PARAM_BUFFER
    Buffer = nullptr;
    if (InFunction->ParmsSize > 0)
    {
        Buffer = FMemory::Malloc(InFunction->ParmsSize, 16);
#if STATS
        const uint32 Size = FMemory::GetAllocSize(Buffer);
        INC_MEMORY_STAT_BY(STAT_UnLua_PersistentParamBuffer_Memory, Size);
#endif
    }
#endif

    // pre-create OutParmRec. memory for speed
#if !SUPPORTS_RPC_CALL
    OutParmRec = nullptr;
    FOutParmRec *CurrentOutParmRec = nullptr;
#endif

    static const FName NAME_LatentInfo = TEXT("LatentInfo");
    Properties.Reserve(InFunction->NumParms);
    for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
    {
        FProperty *Property = *It;
        FPropertyDesc* PropertyDesc = FPropertyDesc::Create(Property);
        int32 Index = Properties.Add(PropertyDesc);
        // 返回值
        if (PropertyDesc->IsReturnParameter())
        {
            ReturnPropertyIndex = Index;                                // return property
        }
        // Latent方法
        else if (LatentPropertyIndex == INDEX_NONE && Property->GetFName() == NAME_LatentInfo)
        {
            LatentPropertyIndex = Index;                                // 'LatentInfo' property for latent function
        }
        // Out参数和引用参数
        else if (Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
        {
            ++NumRefProperties;

            // pre-create OutParmRec for 'out' property
#if !SUPPORTS_RPC_CALL
            FOutParmRec *Out = (FOutParmRec*)FMemory::Malloc(sizeof(FOutParmRec), alignof(FOutParmRec));
#if STATS
            const uint32 Size = FMemory::GetAllocSize(Out);
            INC_MEMORY_STAT_BY(STAT_UnLua_OutParmRec_Memory, Size);
#endif
            Out->PropAddr = Property->ContainerPtrToValuePtr<uint8>(Buffer);
            Out->Property = Property;
            if (CurrentOutParmRec)
            {
                CurrentOutParmRec->NextOutParm = Out;
                CurrentOutParmRec = Out;
            }
            else
            {
                OutParmRec = Out;
                CurrentOutParmRec = Out;
            }
#endif

            // 非const引用参数
            if (!Property->HasAnyPropertyFlags(CPF_ConstParm))
            {
                OutPropertyIndices.Add(Index);                          // non-const reference property
            }
        }

        // 代理参数
        if (!bHasDelegateParams && !PropertyDesc->IsReturnParameter())
        {
			int8 PropertyType = PropertyDesc->GetPropertyType();
			if (PropertyType == CPT_Delegate
				|| PropertyType == CPT_MulticastDelegate
				|| PropertyType == CPT_MulticastSparseDelegate)
			{
				bHasDelegateParams = true;
			}
        }

    }

#if !SUPPORTS_RPC_CALL
    if (CurrentOutParmRec)
    {
        CurrentOutParmRec->NextOutParm = nullptr;
    }
#endif
}

/**
 * Function descriptor destructor
 */
FFunctionDesc::~FFunctionDesc()
{
#if UNLUA_ENABLE_DEBUG != 0
    UE_LOG(LogUnLua, Log, TEXT("~FFunctionDesc : %s,%p"), *FuncName, this);
#endif

    UnLua::FAutoStack AutoStack;

	GReflectionRegistry.RemoveFromDescSet(this);

    // free persistent parameter buffer
    // 释放持久化参数缓冲区
#if ENABLE_PERSISTENT_PARAM_BUFFER
    if (Buffer)
    {
#if STATS
        const uint32 Size = FMemory::GetAllocSize(Buffer);
        DEC_MEMORY_STAT_BY(STAT_UnLua_PersistentParamBuffer_Memory, Size);
#endif
        FMemory::Free(Buffer);
    }
#endif

    // free pre-created OutParmRec
#if !SUPPORTS_RPC_CALL
    while (OutParmRec)
    {
        FOutParmRec *NextOut = OutParmRec->NextOutParm;
#if STATS
        const uint32 Size = FMemory::GetAllocSize(OutParmRec);
        DEC_MEMORY_STAT_BY(STAT_UnLua_OutParmRec_Memory, Size);
#endif
        FMemory::Free(OutParmRec);
        OutParmRec = NextOut;
    }
#endif

    // release cached property descriptors
    // 释放缓存的参数描述
    for (FPropertyDesc *Property : Properties)
    {  
        delete Property;
    }

    // remove Lua reference for this function
    // 释放方法的Lua引用
    if ((FunctionRef != INDEX_NONE)
        &&(UnLua::GetState()))
    {
        luaL_unref(UnLua::GetState(), LUA_REGISTRYINDEX, FunctionRef);
    }
}

/**
 * Call Lua function that overrides this UFunction
 */
bool FFunctionDesc::CallLua(UObject *Context, FFrame &Stack, void *RetValueAddress, bool bRpcCall, bool bUnpackParams)
{
    // push Lua function to the stack
    bool bSuccess = false;
    lua_State *L = *GLuaCxt;
    if (FunctionRef != INDEX_NONE)
    {
        // 根据存储的lua函数地址FunctionRef，把FunctionRef和objet都push到lua栈中
        bSuccess = PushFunction(L, Context, FunctionRef);
    }
    else
    {   
        // support rpc in standlone mode
        bRpcCall = Function->HasAnyFunctionFlags(FUNC_Net);
        // 如果FunctionRef意外为空，则再去lua里根据Function名称再找一遍lua函数
        FunctionRef = PushFunction(L, Context, bRpcCall ? TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_RPC"), *FuncName)) : TCHAR_TO_UTF8(*FuncName));
        bSuccess = FunctionRef != INDEX_NONE;
    }

    if (bSuccess)
    {
        if (bUnpackParams)
        {
            void* Params = nullptr;
#if ENABLE_PERSISTENT_PARAM_BUFFER
            if (!bHasDelegateParams)
            {
                Params = Buffer;
            }
#endif      
            if (!Params)
            {
                Params = Function->ParmsSize > 0 ? FMemory::Malloc(Function->ParmsSize, 16) : nullptr;
            }

            // 填充参数
            for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm) == CPF_Parm; ++It)
            {
                Stack.Step(Stack.Object, It->ContainerPtrToValuePtr<uint8>(Params));
            }
            check(Stack.PeekCode() == EX_EndFunctionParms);
            Stack.SkipCode(1);          // skip EX_EndFunctionParms

            bSuccess = CallLuaInternal(L, Params, Stack.OutParms, RetValueAddress);             // call Lua function...

            if (Params)
            {
#if ENABLE_PERSISTENT_PARAM_BUFFER
                if (bHasDelegateParams)
#endif
                FMemory::Free(Params);
            }

        }
        else
        {
            // Stack的locals属性存储了函数的参数，是一个uint8类型的数组，因此需要以正确顺序解析它，这个工作就由Properties数组完成，Properties中元素的顺序是能够对应的
            bSuccess = CallLuaInternal(L, Stack.Locals, Stack.OutParms, RetValueAddress);       // call Lua function...
        }
    }

    return bSuccess;
}

/**
 * Call the UFunction
 */
int32 FFunctionDesc::CallUE(lua_State *L, int32 NumParams, void *Userdata)
{
    check(Function);

    // !!!Fix!!!
    // when static function passed an object, it should be ignored auto
    // 当静态方法传递一个对象时，它应该被自动忽略
    int32 FirstParamIndex = 1;
    // 获取这个函数包含的实例UObject
    UObject *Object = nullptr;
    if (bStaticFunc)
    {
        UClass *OuterClass = Function->GetOuterUClass();
        // 获取静态方法的CDO
        Object = OuterClass->GetDefaultObject();                // get CDO for static function
    }
    else
    {
        check(NumParams > 0);
        // 如果不是静态函数的情况，Lua那边的调用会用冒号":"，会把自己传进来，也会获得方法所属的UObject
        Object = UnLua::GetUObject(L, 1);
        ++FirstParamIndex;
        --NumParams;
    }

    // 容错，必须要找到Function所属的UObject
    if (!GLuaCxt->IsUObjectValid(Object))
    {
        UE_LOG(LogUnLua, Warning, TEXT("!!! NULL target object for UFunction '%s'! Check the usage of ':' and '.'!"), *FuncName);
        return 0;
    }

#if SUPPORTS_RPC_CALL
    int32 Callspace = Object->GetFunctionCallspace(Function, nullptr);
    bool bRemote = Callspace & FunctionCallspace::Remote;
    bool bLocal = Callspace & FunctionCallspace::Local;
#else
    bool bRemote = false;
    bool bLocal = true;
#endif

    // 创建一个以Function 参数个数(包含返回类型)为长度的bool数组，用作后续清除标记
    TArray<bool> CleanupFlags;
    CleanupFlags.AddZeroed(Properties.Num());
    // 准备参数值
    void *Params = PreCall(L, NumParams, FirstParamIndex, CleanupFlags, Userdata);      // prepare values of properties

    // 记录调用的Function到FinalFunction中
    UFunction *FinalFunction = Function;
    // 如果Function是接口或有重写的情况，会去找相应更准确的函数赋值给FinalFunction
    if (bInterfaceFunc)
    {
        // get target UFunction if it's a function in Interface
        FName FunctionName = Function->GetFName();
        FinalFunction = Object->GetClass()->FindFunctionByName(FunctionName);
        if (!FinalFunction)
        {
            UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("ERROR! Can't find UFunction '%s' in target object!"), *FuncName);

            if (Params)
            {
#if ENABLE_PERSISTENT_PARAM_BUFFER
                if (NumCalls > 0 || bHasDelegateParams)
#endif	
                    FMemory::Free(Params);
            }

            return 0;
        }
#if UE_BUILD_DEBUG
        else if (FinalFunction != Function)
        {
            // todo: 'FinalFunction' must have the same signature with 'Function', check more parameters here
            check(FinalFunction->NumParms == Function->NumParms && FinalFunction->ParmsSize == Function->ParmsSize && FinalFunction->ReturnValueOffset == Function->ReturnValueOffset);
        }
#endif
    }
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
    {   
        // 被Lua覆盖的方法
        if (IsOverridable(Function) 
            && !Function->HasAnyFunctionFlags(FUNC_Net))
        {
            UFunction *OverriddenFunc = GReflectionRegistry.FindOverriddenFunction(Function);
            if (OverriddenFunc)
            {
                FinalFunction = OverriddenFunc;
            }
        }
    }
#endif

    // call the UFuncton...
#if !SUPPORTS_RPC_CALL
    if (FinalFunction == Function && FinalFunction->HasAnyFunctionFlags(FUNC_Native) && NumCalls == 1)
    {
        //FMemory::Memzero((uint8*)Params + FinalFunction->ParmsSize, FinalFunction->PropertiesSize - FinalFunction->ParmsSize);
        uint8* ReturnValueAddress = FinalFunction->ReturnValueOffset != MAX_uint16 ? (uint8*)Params + FinalFunction->ReturnValueOffset : nullptr;
        FFrame NewStack(Object, FinalFunction, Params, nullptr, GetChildProperties(Function));
        NewStack.OutParms = OutParmRec;
        FinalFunction->Invoke(Object, NewStack, ReturnValueAddress);
    }
    else
#endif
    {   
        // Func_NetMuticast both remote and local
        // local automatic checked remote and local,so local first
        if (bLocal)
        {   
            // 通过虚拟机，直接传入函数的反射类型和参数缓存，实现调用这个函数
            Object->UObject::ProcessEvent(FinalFunction, Params);
        }
        if (bRemote && !bLocal)
        {
            Object->CallRemoteFunction(FinalFunction, Params, nullptr, nullptr);
        }
    }

    // PushOut参数到Lua栈中
    // 从Params中读出返回值，返回值是一个C++类型，然后转换成Lua对象，push Lua对象到Lua栈中，返回返回值个数，Lua那边就收到了最终的返回值
    int32 NumReturnValues = PostCall(L, NumParams, FirstParamIndex, Params, CleanupFlags);      // push 'out' properties to Lua stack
    return NumReturnValues;
}

/**
 * Fire a delegate
 * 提供参数和获取返回值给lua
 */
int32 FFunctionDesc::ExecuteDelegate(lua_State *L, int32 NumParams, int32 FirstParamIndex, FScriptDelegate *ScriptDelegate)
{
    if (!ScriptDelegate || !ScriptDelegate->IsBound())
    {
        return 0;
    }

    TArray<bool> CleanupFlags;
    CleanupFlags.AddZeroed(Properties.Num());
    // 向Params填充参数，参数来自lua栈
    void *Params = PreCall(L, NumParams, FirstParamIndex, CleanupFlags);
    // 执行关联的回调函数
    ScriptDelegate->ProcessDelegate<UObject>(Params);
    // 把返回值填充回lua栈
    int32 NumReturnValues = PostCall(L, NumParams, FirstParamIndex, Params, CleanupFlags);
    return NumReturnValues;
}

/**
 * Fire a multicast delegate
 */
void FFunctionDesc::BroadcastMulticastDelegate(lua_State *L, int32 NumParams, int32 FirstParamIndex, FMulticastScriptDelegate *ScriptDelegate)
{
    if (!ScriptDelegate || !ScriptDelegate->IsBound())
    {
        return;
    }

    TArray<bool> CleanupFlags;
    CleanupFlags.AddZeroed(Properties.Num());
    void *Params = PreCall(L, NumParams, FirstParamIndex, CleanupFlags);
    ScriptDelegate->ProcessMulticastDelegate<UObject>(Params);
    // 多播没有返回值
    PostCall(L, NumParams, FirstParamIndex, Params, CleanupFlags);      // !!! have no return values for multi-cast delegates
}

/**
 * Prepare values of properties for the UFunction
 * 主要是把传入的Lua参数转换成C++对象，放入Params缓存区中，然后返回这个缓存区
 */
void* FFunctionDesc::PreCall(lua_State *L, int32 NumParams, int32 FirstParamIndex, TArray<bool> &CleanupFlags, void *Userdata)
{
    // !!!Fix!!!
    // use simple pool
    // 为Function的参数提前开辟一块内存，用作后续缓存每个参数的值
    void *Params = nullptr;
#if ENABLE_PERSISTENT_PARAM_BUFFER
    if (NumCalls < 1 && !bHasDelegateParams)
    {
        Params = Buffer;
    }
    else
#endif
    Params = Function->ParmsSize > 0 ? FMemory::Malloc(Function->ParmsSize, 16) : nullptr;

    // 记录递归调用次数
    ++NumCalls;

    int32 ParamIndex = 0;
    // 遍历UnLua反射库里，该Function的Property对象
    for (int32 i = 0; i < Properties.Num(); ++i)
    {
        // 获取Function的一个Property
        FPropertyDesc *Property = Properties[i];
        // 初始化此Property在缓存区的位置
        Property->InitializeValue(Params);
        if (i == LatentPropertyIndex)
        {
            const int32 ThreadRef = *((int32*)Userdata);
            if(lua_type(L, FirstParamIndex + ParamIndex) == LUA_TUSERDATA)
            {
                // custom latent action info
                FLatentActionInfo Info = UnLua::Get<FLatentActionInfo>(L, FirstParamIndex + ParamIndex, UnLua::TType<FLatentActionInfo>());
                if(Info.Linkage == UUnLuaLatentAction::MAGIC_LEGACY_LINKAGE)
                    Info.Linkage = ThreadRef;
                Property->CopyValue(Params, &Info);
                continue;
            }

            // bind a callback to the latent function
            // 为Latent绑定回调
            FLatentActionInfo LatentActionInfo(ThreadRef, GetTypeHash(FGuid::NewGuid()), TEXT("OnLatentActionCompleted"), (UObject*)GLuaCxt->GetManager());
            Property->CopyValue(Params, &LatentActionInfo);
            continue;
        }
        // 如果当前property是返回值类型，则先置为true，后续执行Call的时候会填入返回值到缓存区中
        if (i == ReturnPropertyIndex)
        {
            CleanupFlags[i] = ParamIndex < NumParams ? !Property->CopyBack(L, FirstParamIndex + ParamIndex, Params) : true;
            continue;
        }
        // 如果参数还没遍历完，从lua栈中，根据lua对象，在UnLua反射库和元表中，获取的C++对象，放入缓存区中
        if (ParamIndex < NumParams)
        {   
#if ENABLE_TYPE_CHECK == 1
            FString ErrorMsg = "";
            if (!Property->CheckPropertyType(L, FirstParamIndex + ParamIndex, ErrorMsg))
            {
                UNLUA_LOGERROR(L, LogUnLua, Warning, TEXT("Invalid parameter type calling ufunction : %s,parameter : %d, error msg : %s"), *FuncName, ParamIndex, *ErrorMsg);
            }
#endif
            // 通过调用Property->SetValue，通过传入栈index，可以从Lua对象转换成C++对象的值，将C++对象放入缓存区中
            CleanupFlags[i] = Property->SetValue(L, Params, FirstParamIndex + ParamIndex, false);
        }
        else if (!Property->IsOutParameter())
        {
            if (DefaultParams)
            {
                // set value for default parameter
                // 设置默认参数
                IParamValue **DefaultValue = DefaultParams->Parameters.Find(Property->GetProperty()->GetFName());
                if (DefaultValue)
                {
                    const void *ValuePtr = (*DefaultValue)->GetValue();
                    Property->CopyValue(Params, ValuePtr);
                    CleanupFlags[i] = true;
                }
            }
            else
            {
#if ENABLE_TYPE_CHECK == 1
                FString ErrorMsg = "";
                if (!Property->CheckPropertyType(L, FirstParamIndex + ParamIndex, ErrorMsg))
                {
                    UNLUA_LOGERROR(L, LogUnLua, Warning, TEXT("Invalid parameter type calling ufunction : %s,parameter : %d, error msg : %s"), *FuncName, ParamIndex, *ErrorMsg);
                }
#endif
            }
        }
        ++ParamIndex;
    }

    return Params;
}

/**
 * Handling 'out' properties
 */
int32 FFunctionDesc::PostCall(lua_State *L, int32 NumParams, int32 FirstParamIndex, void *Params, const TArray<bool> &CleanupFlags)
{
    int32 NumReturnValues = 0;

    // !!!Fix!!!
    // out parameters always use return format, copyback is better,but some parameters such 
    // as int can not be copy back
    // c++ may has return and out params, we must push it on stack
    // Out参数总是返回格式,复写是更好的,但是部分参数例如int是不能复写的
    // C++可能有返回和输出参数，必须将它压入栈
    for (int32 Index : OutPropertyIndices)
    {
        FPropertyDesc *Property = Properties[Index];
        if (Index >= NumParams || !Property->CopyBack(L, Params, FirstParamIndex + Index))
        {
            Property->GetValue(L, Params, true);
            ++NumReturnValues;
        }
    }

    if (ReturnPropertyIndex > INDEX_NONE)
    {
        FPropertyDesc *Property = Properties[ReturnPropertyIndex];
        if (!CleanupFlags[ReturnPropertyIndex])
        {
            int32 ReturnIndexInStack = FirstParamIndex + ReturnPropertyIndex;
            bool bResult = Property->CopyBack(L, Params, ReturnIndexInStack);
            check(bResult);
            lua_pushvalue(L, ReturnIndexInStack);
        }
        else
        {
            Property->GetValue(L, Params, true);
        }
        ++NumReturnValues;
    }

    for (int32 i = 0; i < Properties.Num(); ++i)
    {
        if (CleanupFlags[i])
        {
            Properties[i]->DestroyValue(Params);
        }
    }

    --NumCalls;

    if (Params)
    {
#if ENABLE_PERSISTENT_PARAM_BUFFER
        if (NumCalls > 0 || bHasDelegateParams)
#endif	
        FMemory::Free(Params);
    }

    return NumReturnValues;
}

/**
 * Get OutParmRec for a non-const reference property
 */
static FOutParmRec* FindOutParmRec(FOutParmRec *OutParam, FProperty *OutProperty)
{
    while (OutParam)
    {
        if (OutParam->Property == OutProperty)
        {
            return OutParam;
        }
        OutParam = OutParam->NextOutParm;
    }
    return nullptr;
}

/**
 * Call Lua function that overrides this UFunction. 
 */
bool FFunctionDesc::CallLuaInternal(lua_State *L, void *InParams, FOutParmRec *OutParams, void *RetValueAddress) const
{
    // prepare parameters for Lua function
    FOutParmRec *OutParam = OutParams;
    for (const FPropertyDesc *Property : Properties)
    {
        if (Property->IsReturnParameter())
        {
            continue;
        }

        // !!!Fix!!!
        // out parameters include return? out/ref and not const
        if (Property->IsOutParameter())
        {
            OutParam = FindOutParmRec(OutParam, Property->GetProperty());
            if (OutParam)
            {
                Property->GetValueInternal(L, OutParam->PropAddr, false);
                OutParam = OutParam->NextOutParm;
                continue;
            }
        }

        // 使用FPropertyDesc::GetValue()方法把函数参数push到lua栈中
        // PropDesc会用之前介绍的GetValueInternal()方法，从locals中获取参数值，然后push到lua中
        Property->GetValue(L, InParams, !Property->IsReferenceParameter());
    }

    // object is also pushed, return is push when return
    int32 NumParams = Properties.Num();
    int32 NumResult = OutPropertyIndices.Num();
    if (ReturnPropertyIndex == INDEX_NONE)
    {
        NumParams++;
    }
    else
    {
        NumResult++;
    }
    // 使用lua_pcall调用函数。既然函数和参数都已具备，就可以进行函数调用了，使用lua_pcall接口即可
    bool bSuccess = CallFunction(L, NumParams, NumResult);      // pcall
    if (!bSuccess)
    {
        return false;
    }

    // out value
    // suppose out param is also pushed on stack? this is assumed done by user... so we can not trust it
    // 获取返回值。lua函数调用完成后会把返回值push到lua栈中，UnLua需要以一定顺序取出这些返回值，并设置到正确的C++属性上
    // lua函数支持多返回值，C++函数只能有一个返回值，但可以使用引用参数实现多返回值效果，因此UnLua支持lua函数直接返回多值给C++
    // UE中，BlueprintEvent函数的返回值存储在两个地方，对于普通意义上的单个返回值，存储在EventParams的ReturnValue属性上，
    // 对于引用传递的参数，会存储为EventParams上普通属性，但UHT自动生成的代码，会把Stack.locals中对应修改后的属性赋值给原先传进来的参数
    int32 NumResultOnStack = lua_gettop(L);
    if (NumResult <= NumResultOnStack)
    {
        int32 OutPropertyIndex = -NumResult;
        OutParam = OutParams;

        for (int32 i = 0; i < OutPropertyIndices.Num(); ++i)
        {
            FPropertyDesc* OutProperty = Properties[OutPropertyIndices[i]];
            if (OutProperty->IsReferenceParameter())
            {
                continue;
            }
            OutParam = FindOutParmRec(OutParam, OutProperty->GetProperty());
            if (!OutParam)
            {
                OutProperty->SetValue(L, InParams, OutPropertyIndex, true);
            }
            else
            {
                // user do push it on stack?
                int32 Type = lua_type(L, OutPropertyIndex);
                if (Type == LUA_TNIL)
                {
                    // so we need copy it back from input parameter
                    OutProperty->CopyBack(OutParam->PropAddr, OutProperty->GetProperty()->ContainerPtrToValuePtr<void>(InParams));   // copy back value to out property
                }
                else
                {   
                    // copy it from stack
                    // 调用PropDesc的SetValueInternal方法，把引用参数写回Stack.locals，即Params结构体
                    OutProperty->SetValueInternal(L, OutParam->PropAddr, OutPropertyIndex, true);       // set value for out property
                }
                OutParam = OutParam->NextOutParm;
            }
            ++OutPropertyIndex;
        }
    }
    
    // return value
    if (ReturnPropertyIndex > INDEX_NONE)
    {   
        if (NumResultOnStack < 1)
        {
            UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("FuncName %s has return value, but no value found on stack!"),*FuncName);
        }
        else
        {
            check(RetValueAddress);
            // 判断函数是否有返回值，如果有就把Params中ReturnValue部分也写上值
            // 观察SetValueInternal函数的栈元素索引就能发现，引用参数按顺序被压入栈中，而返回值位于栈顶
            Properties[ReturnPropertyIndex]->SetValueInternal(L, RetValueAddress, -1, true);        // set value for return property
        }
    }

    lua_pop(L, NumResult);
    return true;
}
