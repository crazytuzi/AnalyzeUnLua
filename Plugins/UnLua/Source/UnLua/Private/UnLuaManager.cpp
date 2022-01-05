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

#include "UnLuaManager.h"
#include "UnLua.h"
#include "UnLuaInterface.h"
#include "LuaCore.h"
#include "LuaContext.h"
#include "LuaFunctionInjection.h"
#include "DelegateHelper.h"
#include "UEObjectReferencer.h"
#include "GameFramework/InputSettings.h"
#include "Components/InputComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/LevelScriptActor.h"

static const TCHAR* SReadableInputEvent[] = { TEXT("Pressed"), TEXT("Released"), TEXT("Repeat"), TEXT("DoubleClick"), TEXT("Axis"), TEXT("Max") };

UUnLuaManager::UUnLuaManager()
    : InputActionFunc(nullptr), InputAxisFunc(nullptr), InputTouchFunc(nullptr), InputVectorAxisFunc(nullptr), InputGestureFunc(nullptr), AnimNotifyFunc(nullptr)
{
    // COD跳过
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    GetDefaultInputs();             // get all Axis/Action inputs
    EKeys::GetAllKeys(AllKeys);     // get all key inputs

    // get template input UFunctions for InputAction/InputAxis/InputTouch/InputVectorAxis/InputGesture/AnimNotify
    UClass *Class = GetClass();
    InputActionFunc = Class->FindFunctionByName(FName("InputAction"));
    InputAxisFunc = Class->FindFunctionByName(FName("InputAxis"));
    InputTouchFunc = Class->FindFunctionByName(FName("InputTouch"));
    InputVectorAxisFunc = Class->FindFunctionByName(FName("InputVectorAxis"));
    InputGestureFunc = Class->FindFunctionByName(FName("InputGesture"));
    AnimNotifyFunc = Class->FindFunctionByName(FName("TriggerAnimNotify"));
}

//UUnLuaManager::~UUnLuaManager()
//{
//    if (HasAnyFlags(RF_ClassDefaultObject))
//    {
//        return;
//    }
//    CleanupDefaultInputs();
//}


/**
 * Bind a Lua module for a UObject
 *（1）Manager->Bind绑定的是：新创建的UObject + Lua 对象（根据新创建的UObject找到lua路径后创建的）
 *（2）为新UObject 注册（RegisterClass）元表
 *（3）根据Lua模块，重写新UObject中可被重写的UFunction的反射信息，注意这里是直接在UFunction上改写
 *（4）创建Lua对象，并做设元表、设Object等变量、放入全局引用等初始化，
 * 并把Lua对象和lightuserdata（UObject指针）的映射放入ObjectMap中，ObjectMap是Lua中的对象
 *（5）使新UObject被全局持有，并把Lua对象索引和UObject映射放入AttachedObjects中，AttachedObjects是C++中的对象，
 * 和Lua中的ObjectMap对象相似
 */
bool UUnLuaManager::Bind(UObjectBaseUtility *Object, UClass *Class, const TCHAR *InModuleName, int32 InitializerTableRef)
{   
    if (!Object || !Class)
    {
        UE_LOG(LogUnLua, Warning, TEXT("Invalid target object!"));
        return false;
    }

#if UNLUA_ENABLE_DEBUG != 0
    UE_LOG(LogUnLua, Log, TEXT("UUnLuaManager::Bind : %p,%s,%s"), Object, *Object->GetName(),InModuleName);
#endif
    
    bool bSuccess = true;
    lua_State *L = *GLuaCxt;

    bool bMultipleLuaBind = false;
    UClass** BindedClass = Classes.Find(InModuleName);
    if ((BindedClass)
        &&(*BindedClass != Object->GetClass()))
    {
        bMultipleLuaBind = true;
    }

    // （1）记录Class的类型反射信息
    // （2）创建一个以Class_C为名字的元表，Class_C是其类型GetName的结果，然后设置相应的元方法，并放入_G表中
    // 如果RegisterClass失败则退出Bind
    if (!RegisterClass(L, Class))              // register class first
    {
        return false;
    }

    // try bind lua if not bind or use a copyed table
    // 调用lua里的require方法，获取lua的模块代码，即路径对应的lua代码，require之后，UnLua会将该lua模块缓存到package.loaded中，通过名字就可以获取该lua模块
    UnLua::FLuaRetValues RetValues = UnLua::Call(L, "require", TCHAR_TO_UTF8(InModuleName));    // require Lua module
    FString Error;
    if (!RetValues.IsValid() || RetValues.Num() == 0)
    {
        Error = "invalid return value of require()";
        bSuccess = false;
    }
    else if (RetValues[0].GetType() != LUA_TTABLE)
    {
        Error = FString("table needed but got ");
        if(RetValues[0].GetType() == LUA_TSTRING)
            Error += UTF8_TO_TCHAR(RetValues[0].Value<const char*>());
        else
            Error += UTF8_TO_TCHAR(lua_typename(L, RetValues[0].GetType()));
        bSuccess = false;
    }
    else
    {
        // 如果require成功，则进一步进行绑定
        bSuccess = BindInternal(Object, Class, InModuleName, true, bMultipleLuaBind, Error);                             // bind!!!
    }

    if (bSuccess)
    {   
        bool bDerivedClassBinded = false;
        // 继承类型
        if (Object->GetClass() != Class)
        {
            bDerivedClassBinded = true;
            OnDerivedClassBinded(Object->GetClass(), Class);
        }

        FString RealModuleName = *ModuleNames.Find(Class);

        // 把Lua模块路径缓存到一个列表里，便于后续统一清理这些已Require的Lua模块
        GLuaCxt->AddModuleName(*RealModuleName);                                       // record this required module

        // create a Lua instance for this UObject
        // 根据新生成的UObject和对应的Lua模块路径，新创建一个Lua对象
        int32 ObjectRef = NewLuaObject(L, Object, bDerivedClassBinded ? Class : nullptr, TCHAR_TO_UTF8(*RealModuleName));

        // 将该UObject放入UE4全局引用中，这样该UObject之后不会被GC掉，并将UObject和Lua对象在Registry表里的索引作为key、value放入AttachedObjects中，这个Lua对象和UObject进行了绑定
        AddAttachedObject(Object, ObjectRef);                                       // record this binded UObject

        // try call user first user function handler
        // 检查lua中是否有Initialize函数，lua中可以实现该函数做一些初始化工作，如果有就会调用
        bool bResult = false;
        int32 FunctionRef = PushFunction(L, Object, "Initialize");                  // push hard coded Lua function 'Initialize'
        if (FunctionRef != INDEX_NONE)
        {
            if (InitializerTableRef != INDEX_NONE)
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, InitializerTableRef);             // push a initializer table if necessary
            }
            else
            {
                lua_pushnil(L);
            }
            bResult = ::CallFunction(L, 2, 0);                                 // call 'Initialize'
            if (!bResult)
            {
                UE_LOG(LogUnLua, Warning, TEXT("Failed to call 'Initialize' function!"));
            }
            luaL_unref(L, LUA_REGISTRYINDEX, FunctionRef);
        }
    }
    else
    {
        UE_LOG(LogUnLua, Warning, TEXT("Failed to attach %s module for object %s,%p!\n%s"), InModuleName, *Object->GetName(), Object, *Error);
    }

    return bSuccess;
}

/**
 * Callback for 'Hotfix'
 */
bool UUnLuaManager::OnModuleHotfixed(const TCHAR *InModuleName)
{
    TArray<FString> _ModuleNames;
    _ModuleNames.Add(InModuleName);
    int16* NameIdx = RealModuleNames.Find(InModuleName);
    for (int16 i = 1; i < *NameIdx; ++i)
    {
        _ModuleNames.Add(FString::Printf(TEXT("%s_#%d"),InModuleName,i));
    }

    for (int i = 0; i < _ModuleNames.Num(); ++i)
    {
        UClass** ClassPtr = Classes.Find(_ModuleNames[i]);
        if (ClassPtr)
        {
            lua_State* L = *GLuaCxt;
            TStringConversion<TStringConvert<TCHAR, ANSICHAR>> ModuleName(InModuleName);

            TSet<FName> LuaFunctions;
            // 获取Lua模块/表中的所有方法
            bool bSuccess = GetFunctionList(L, ModuleName.Get(), LuaFunctions);                 // get all functions in this Lua module/table
            if (!bSuccess)
            {
                continue;
            }

            TSet<FName>* LuaFunctionsPtr = ModuleFunctions.Find(InModuleName);
            check(LuaFunctionsPtr);
            // 获取新增Lua方法
            TSet<FName> NewFunctions = LuaFunctions.Difference(*LuaFunctionsPtr);               // get new added Lua functions
            if (NewFunctions.Num() > 0)
            {
                UClass* Class = *ClassPtr;
                TMap<FName, UFunction*>* UEFunctionsPtr = OverridableFunctions.Find(Class);     // get all overridable UFunctions
                check(UEFunctionsPtr);
                for (const FName& LuaFuncName : NewFunctions)
                {
                    UFunction** Func = UEFunctionsPtr->Find(LuaFuncName);
                    if (Func)
                    {
                        OverrideFunction(*Func, Class, LuaFuncName);                            // override the UFunction
                    }
                }

                bSuccess = ConditionalUpdateClass(Class, NewFunctions, *UEFunctionsPtr);        // update class conditionally
            }
        }
    }
        
    return true;
}

/**
 * Remove binded UObjects
 */
void UUnLuaManager::NotifyUObjectDeleted(const UObjectBase *Object, bool bClass)
{
    if (bClass)
    {
        //OnClassCleanup((UClass*)Object);
    }
    else
    {
        // 删除Lua表
        DeleteLuaObject(*GLuaCxt, (UObjectBaseUtility*)Object);        // delete the Lua instance (table)
    }
}

/**
 * Clean up...
 */
void UUnLuaManager::Cleanup(UWorld *InWorld, bool bFullCleanup)
{
    AttachedObjects.Empty();
    AttachedActors.Empty();

    ModuleNames.Empty();
    Classes.Empty();
    OverridableFunctions.Empty();
    ModuleFunctions.Empty();

    CleanupDuplicatedFunctions();       // clean up duplicated UFunctions
    CleanupCachedNatives();             // restore cached thunk functions
    CleanupCachedScripts();             // restore cached scripts

#if !ENABLE_CALL_OVERRIDDEN_FUNCTION
    New2TemplateFunctions.Empty();
#endif
}

/**
 * Clean up everything linked to the target UClass
 */
void UUnLuaManager::CleanUpByClass(UClass *Class)
{
    if (!Class)
    {
        return;
    }

    const FString *ModuleNamePtr = ModuleNames.Find(Class);
    if (ModuleNamePtr)
    {
        FString ModuleName = *ModuleNamePtr;

        Classes.Remove(ModuleName);
        ModuleFunctions.Remove(ModuleName);

        TMap<FName, UFunction*> FunctionMap;
        OverridableFunctions.RemoveAndCopyValue(Class, FunctionMap);
        for (TMap<FName, UFunction*>::TIterator It(FunctionMap); It; ++It)
        {
            UFunction *Function = It.Value();
            FNativeFuncPtr NativeFuncPtr = nullptr;
            if (CachedNatives.RemoveAndCopyValue(Function, NativeFuncPtr))
            {
                ResetUFunction(Function, NativeFuncPtr);
            }
        }

        TArray<UFunction*> Functions;
        if (DuplicatedFunctions.RemoveAndCopyValue(Class, Functions))
        {
            RemoveDuplicatedFunctions(Class, Functions);
        }

        OnClassCleanup(Class);

        FDelegateHelper::CleanUpByClass(Class);

        ClearLoadedModule(*GLuaCxt, TCHAR_TO_UTF8(*ModuleName));

        ModuleNames.Remove(Class);
    }
}

/**
 * Clean duplicated UFunctions
 */
void UUnLuaManager::CleanupDuplicatedFunctions()
{
    for (TMap<UClass*, TArray<UFunction*>>::TIterator It(DuplicatedFunctions); It; ++It)
    {
        OnClassCleanup(It.Key());
        RemoveDuplicatedFunctions(It.Key(), It.Value());
    }
    DuplicatedFunctions.Empty();
    Base2DerivedClasses.Empty();
    Derived2BaseClasses.Empty();
}

/**
 * Restore cached thunk functions
 */
void UUnLuaManager::CleanupCachedNatives()
{
    for (TMap<UFunction*, FNativeFuncPtr>::TIterator It(CachedNatives); It; ++It)
    {   
        if (GLuaCxt->IsUObjectValid(It.Key()))
        {
            ResetUFunction(It.Key(), It.Value());
        }
    }
    CachedNatives.Empty();
}

/**
 * Restore cached scripts
 */
void UUnLuaManager::CleanupCachedScripts()
{
    for (TMap<UFunction*, TArray<uint8>>::TIterator It(CachedScripts); It; ++It)
    {
        UFunction *Func = It.Key();
        Func->Script = It.Value();
    }
    CachedScripts.Empty();
}

/**
 * Cleanup intermediate data linked to a UClass
 */
void UUnLuaManager::OnClassCleanup(UClass *Class)
{
    UClass *BaseClass = nullptr;
    if (Derived2BaseClasses.RemoveAndCopyValue(Class, BaseClass))
    {
        TArray<UClass*> *DerivedClasses = Base2DerivedClasses.Find(BaseClass);
        if (DerivedClasses)
        {
            DerivedClasses->Remove(Class);
        }
    }

    TArray<UClass*> DerivedClasses;
    if (Base2DerivedClasses.RemoveAndCopyValue(Class, DerivedClasses))
    {
        for (UClass *DerivedClass : DerivedClasses)
        {
            // 清除基类的UFunction缓存
            DerivedClass->ClearFunctionMapsCaches();            // clean up cached UFunctions of super class
        }
    }
}

/**
 * Reset a UFunction
 */
void UUnLuaManager::ResetUFunction(UFunction *Function, FNativeFuncPtr NativeFuncPtr)
{
    if (GLuaCxt->IsUObjectValid(Function))
    {
        Function->SetNativeFunc(NativeFuncPtr);

        if (Function->Script.Num() > 0 && Function->Script[0] == EX_CallLua)
        {
            Function->Script.Empty();
        }

        TArray<uint8> Script;
        if (CachedScripts.RemoveAndCopyValue(Function, Script))
        {
            Function->Script = Script;
        }
    }
    else
    {
        CachedScripts.Remove(Function);
    }

    GReflectionRegistry.UnRegisterFunction(Function);

#if ENABLE_CALL_OVERRIDDEN_FUNCTION
    UFunction *OverriddenFunc = GReflectionRegistry.RemoveOverriddenFunction(Function);
    if (GLuaCxt->IsUObjectValid(OverriddenFunc))
    {   
        RemoveUFunction(OverriddenFunc, OverriddenFunc->GetOuterUClass());
    }
#endif
}

/**
 * Remove duplicated UFunctions
 */
void UUnLuaManager::RemoveDuplicatedFunctions(UClass *Class, TArray<UFunction*> &Functions)
{
    for (UFunction *Function : Functions)
    {
        RemoveUFunction(Function, Class);                       // clean up duplicated UFunction
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
        GReflectionRegistry.RemoveOverriddenFunction(Function);
#endif
    }
}

/**
 * Post process for cleaning up
 */
void UUnLuaManager::PostCleanup()
{

}

/**
 * Get all default Axis/Action inputs
 */
void UUnLuaManager::GetDefaultInputs()
{
    UInputSettings *DefaultIS = UInputSettings::StaticClass()->GetDefaultObject<UInputSettings>();
    TArray<FName> AxisNames, ActionNames;
    DefaultIS->GetAxisNames(AxisNames);
    DefaultIS->GetActionNames(ActionNames);
    for (auto AxisName : AxisNames)
    {
        DefaultAxisNames.Add(AxisName);
    }
    for (auto ActionName : ActionNames)
    {
        DefaultActionNames.Add(ActionName);
    }
}

/**
 * Clean up all default Axis/Action inputs
 */
void UUnLuaManager::CleanupDefaultInputs()
{
    DefaultAxisNames.Empty();
    DefaultActionNames.Empty();
}

/**
 * Replace inputs
 */
bool UUnLuaManager::ReplaceInputs(AActor *Actor, UInputComponent *InputComponent)
{
    if (!Actor || !InputComponent || !AttachedObjects.Find(Actor))
    {
        return false;
    }

    UClass *Class = Actor->GetClass();
    FString *ModuleNamePtr = ModuleNames.Find(Class);
    if (!ModuleNamePtr)
    {
        UClass **SuperClassPtr = Derived2BaseClasses.Find(Class);
        if (!SuperClassPtr || !(*SuperClassPtr))
        {
            return false;
        }
        ModuleNamePtr = ModuleNames.Find(*SuperClassPtr);
    }
    check(ModuleNamePtr);
    TSet<FName> *LuaFunctionsPtr = ModuleFunctions.Find(*ModuleNamePtr);
    check(LuaFunctionsPtr);

    ReplaceActionInputs(Actor, InputComponent, *LuaFunctionsPtr);       // replace action inputs
    ReplaceKeyInputs(Actor, InputComponent, *LuaFunctionsPtr);          // replace key inputs
    ReplaceAxisInputs(Actor, InputComponent, *LuaFunctionsPtr);         // replace axis inputs
    ReplaceTouchInputs(Actor, InputComponent, *LuaFunctionsPtr);        // replace touch inputs
    ReplaceAxisKeyInputs(Actor, InputComponent, *LuaFunctionsPtr);      // replace AxisKey inputs
    ReplaceVectorAxisInputs(Actor, InputComponent, *LuaFunctionsPtr);   // replace VectorAxis inputs
    ReplaceGestureInputs(Actor, InputComponent, *LuaFunctionsPtr);      // replace gesture inputs

    return true;
}

/**
 * Callback when a map is loaded
 */
void UUnLuaManager::OnMapLoaded(UWorld *World)
{
    /*for (AActor *Actor : AttachedActors)
    {	
		if (GLuaCxt->IsUObjectValid(Actor))
		{
			if (!Actor->OnDestroyed.IsAlreadyBound(this, &UUnLuaManager::OnActorDestroyed))
			{
				Actor->OnDestroyed.AddDynamic(this, &UUnLuaManager::OnActorDestroyed);      // bind a callback for destroying actor
			}
		}
    }*/

    ENetMode NetMode = World->GetNetMode();
    if (NetMode == NM_DedicatedServer)
    {
        return;
    }

    const TArray<ULevel*> &Levels = World->GetLevels();
    for (ULevel *Level : Levels)
    {
        // replace input defined in ALevelScriptActor::InputComponent if necessary
        // 如果有必要,替换定义在ALevelScriptActor::InputComponent中的输入
        ALevelScriptActor *LSA = Level->GetLevelScriptActor();
        if (LSA && LSA->InputEnabled() && LSA->InputComponent)
        {
            ReplaceInputs(LSA, LSA->InputComponent);
        }
    }
}

/**
 * Callback for spawning an actor
 */
void UUnLuaManager::OnActorSpawned(AActor *Actor)
{
    if (!GLuaCxt->IsEnable())
    {
        return;
    }

    // 绑定Actor销毁回调
    Actor->OnDestroyed.AddDynamic(this, &UUnLuaManager::OnActorDestroyed);      // bind a callback for destroying actor
}

/**
 * Callback for destroying an actor
 */
void UUnLuaManager::OnActorDestroyed(AActor *Actor)
{
    if (!GLuaCxt->IsEnable())
    {
        return;
    }

    int32 Num = AttachedActors.Remove(Actor);
    if (Num > 0)
    {   
        // 移除Actor记录
        DeleteUObjectRefs(UnLua::GetState(),Actor);   // remove record of this actor
    }
}

/**
 * Callback for completing a latent function
 */
void UUnLuaManager::OnLatentActionCompleted(int32 LinkID)
{
    // 继续协程
    GLuaCxt->ResumeThread(LinkID);              // resume a coroutine
}

/**
 * Notify that a derived class is binded to its base class
 */
void UUnLuaManager::OnDerivedClassBinded(UClass *DerivedClass, UClass *BaseClass)
{
    TArray<UClass*> &DerivedClasses = Base2DerivedClasses.FindOrAdd(BaseClass);
    do
    {
        if (DerivedClasses.Find(DerivedClass) != INDEX_NONE)
        {
            break;
        }
        Derived2BaseClasses.Add(DerivedClass, BaseClass);
        DerivedClasses.Add(DerivedClass);
        DerivedClass = DerivedClass->GetSuperClass();
    } while (DerivedClass != BaseClass);
}

/**
 * Get target UCLASS for Lua binding
 */
UClass* UUnLuaManager::GetTargetClass(UClass *Class, UFunction **GetModuleNameFunc)
{
    static UClass *InterfaceClass = UUnLuaInterface::StaticClass();
    if (!Class || !Class->ImplementsInterface(InterfaceClass))
    {
        return nullptr;
    }
    UFunction *Func = Class->FindFunctionByName(FName("GetModuleName"));
    if (Func && Func->GetNativeFunc())
    {
        if (GetModuleNameFunc)
        {
            *GetModuleNameFunc = Func;
        }

        UClass *OuterClass = Func->GetOuterUClass();
        return OuterClass == InterfaceClass ? Class : OuterClass;
    }
    return nullptr;
}

/**
 * Bind a Lua module for a UObject
 * 绑定了Lua模块和它对应的C++对象的反射类型，做了两件事情：
 * （1）将Lua模块名和C++对象的反射类型记录在ModuleNames、Classes表中
 * （2）如果Lua模块的方法中，有和C++函数同名的方法，则直接拿这个C++函数的反射信息进行改写，改写为执行Lua方法
 */
bool UUnLuaManager::BindInternal(UObjectBaseUtility* Object, UClass* Class, const FString& InModuleName, bool bNewCreated, bool bMultipleLuaBind, FString& Error)
{
    if (!Object || !Class)
    {
        return false;
    }

    // module may be already loaded for other class,etc muti bp bind to same lua
    // 模块可能已经加载
    FString RealModuleName = InModuleName;
    if (bMultipleLuaBind)
    {
        lua_State* L = UnLua::GetState();
        const int32 Type = GetLoadedModule(L, TCHAR_TO_UTF8(*InModuleName));
        if (Type != LUA_TTABLE) 
        {
            Error = FString::Printf(TEXT("table needed got %s"), UTF8_TO_TCHAR(lua_typename(L, Type)));
            return false;
        }

        // generate new module for this module
        // 生成新的Module
        int16* NameIdx = RealModuleNames.Find(InModuleName);
        if (!NameIdx)
        {
            RealModuleNames.Add(InModuleName, 1);
            RealModuleName = FString::Printf(TEXT("%s_#%d"), *InModuleName, 1);
        }
        else
        {
            *NameIdx = *NameIdx + 1;
            RealModuleName = FString::Printf(TEXT("%s_#%d"), *InModuleName, *NameIdx);
        }

        // make a copy of lua module
        // 深拷贝
        lua_newtable(L);
        lua_pushnil(L);
        while (lua_next(L, -3) != 0)
        {
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_settable(L, -4);
        }

        lua_getglobal(L, "package");
        lua_getfield(L, -1, "loaded");
        lua_pushvalue(L, -3);
        lua_setfield(L, -2, TCHAR_TO_UTF8(*RealModuleName));
        lua_pop(L, 3);
    }


    // Lua Module路径名和C++类型建立映射关系，记录在ModuleNames和Classes表中
    ModuleNames.Add(Class, RealModuleName);
    Classes.Add(RealModuleName, Class);

    TSet<FName> &LuaFunctions = ModuleFunctions.Add(RealModuleName);
    // 遍历也会包括Module的所有父类
    GetFunctionList(UnLua::GetState(), TCHAR_TO_UTF8(*RealModuleName), LuaFunctions);                         // get all functions defined in the Lua module
    // 获取Class所有可重写的函数反射信息，记录在OverridableFunctions数据结构中，目前包括"BlueprintEvent"和"RepNotifyFunc"，也就是说，蓝图中无法覆写的RepNotify函数，在UnLua中可以直接覆写
    TMap<FName, UFunction*> &UEFunctions = OverridableFunctions.Add(Class);
    GetOverridableFunctions(Class, UEFunctions);                                // get all overridable UFunctions

    // 判断Lua Module中的方法名，有没有和Class函数名重名的
    // 如果有，则将原Function的调用重定向为Lua方法的调用，将原Function的执行代码保存在一个新的函数中，新的函数名为函数原名+“Copy”
    OverrideFunctions(LuaFunctions, UEFunctions, Class, bNewCreated);           // try to override UFunctions

    return ConditionalUpdateClass(Class, LuaFunctions, UEFunctions);
}


/**
 * Override special UFunctions
 */
bool UUnLuaManager::ConditionalUpdateClass(UClass *Class, const TSet<FName> &LuaFunctions, TMap<FName, UFunction*> &UEFunctions)
{
    check(Class);

    if (LuaFunctions.Num() < 1 || UEFunctions.Num() < 1)
    {
        return true;
    }

    if (Class->IsChildOf<UAnimInstance>())
    {
        for (const FName &FunctionName : LuaFunctions)
        {
            if (!UEFunctions.Find(FunctionName) && FunctionName.ToString().StartsWith(TEXT("AnimNotify_")))
            {
                AddFunction(AnimNotifyFunc, Class, FunctionName);           // override AnimNotify
            }
        }
    }

    return true;
}

/**
 * Override candidate UFunctions
 */
void UUnLuaManager::OverrideFunctions(const TSet<FName> &LuaFunctions, TMap<FName, UFunction*> &UEFunctions, UClass *OuterClass, bool bCheckFuncNetMode)
{
    for (const FName &LuaFuncName : LuaFunctions)
    {
        UFunction **Func = UEFunctions.Find(LuaFuncName);
        if (Func)
        {
            UFunction *Function = *Func;
            OverrideFunction(Function, OuterClass, LuaFuncName);
        }
    }
}

/**
 * Override a UFunction
 */
void UUnLuaManager::OverrideFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
    // 需要判断这个UFunction是这个UClass的还是它父类的，是UClass的则替换UFunction，是父类的则新增UFunction
    if (TemplateFunction->GetOuter() != OuterClass)
    {
//#if UE_BUILD_SHIPPING || UE_BUILD_TEST
        if (TemplateFunction->Script.Num() > 0 && TemplateFunction->Script[0] == EX_CallLua)
        {
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
            TemplateFunction = GReflectionRegistry.FindOverriddenFunction(TemplateFunction);
#else
            TemplateFunction = New2TemplateFunctions.FindChecked(TemplateFunction);
#endif
        }
//#endif
        AddFunction(TemplateFunction, OuterClass, NewFuncName);     // add a duplicated UFunction to child UClass
    }
    else
    {
        // 如果lua要覆盖的UFunction就在子类中，则需要替换该UFunction的逻辑，不能再创建同名函数了
        ReplaceFunction(TemplateFunction, OuterClass);              // replace thunk function and insert opcodes
    }
}

/**
 * Add a duplicated UFunction to UClass
 */
void UUnLuaManager::AddFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName)
{
    UFunction *Func = OuterClass->FindFunctionByName(NewFuncName, EIncludeSuperFlag::ExcludeSuper);
    if (!Func)
    {
        if (TemplateFunction->HasAnyFunctionFlags(FUNC_Native))
        {
            // call this before duplicate UFunction that has FUNC_Native to eliminate "Failed to bind native function" warnings.
            OuterClass->AddNativeFunction(*NewFuncName.ToString(), (FNativeFuncPtr)&FLuaInvoker::execCallLua);
        }

        // 先把要覆写的UFunction作为TemplateFunction，新建了NewFunction
        // 新建NewFunction通过DuplicateUFunction函数完成，会把TemplateFunction的Property逐个复制过去，然后Class把NewFunction添加到自己的FuncMap中，以后就能访问了
        UFunction *NewFunc = DuplicateUFunction(TemplateFunction, OuterClass, NewFuncName); // duplicate a UFunction
        if (!NewFunc->HasAnyFunctionFlags(FUNC_Native) && NewFunc->Script.Num() > 0)
        {
            // 接下来会把NewFunc的字节码清空，这就意味之后该TemplateFunction对应的蓝图逻辑执行不到了
            NewFunc->Script.Empty(3);                               // insert opcodes for non-native UFunction only
        }
        // 类似UClass，UnLua也会对UFunction进行注册，并创建FFunctionDesc作为描述数据
        // FFunctionDesc数据结构也很重要，它可以作为UFunction和LuaFunction之间的桥梁
        // Function指向UFunction，FunctionRef指向lua中的函数，还存有函数名，函数默认参数等信息
        OverrideUFunction(NewFunc, (FNativeFuncPtr)&FLuaInvoker::execCallLua, GReflectionRegistry.RegisterFunction(NewFunc));   // replace thunk function and insert opcodes
        TArray<UFunction*> &DuplicatedFuncs = DuplicatedFunctions.FindOrAdd(OuterClass);
        DuplicatedFuncs.AddUnique(NewFunc);
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
        GReflectionRegistry.AddOverriddenFunction(NewFunc, TemplateFunction);
#else
        New2TemplateFunctions.Add(NewFunc, TemplateFunction);
#endif
    }
}

/**
 * Replace thunk function and insert opcodes
 */
void UUnLuaManager::ReplaceFunction(UFunction *TemplateFunction, UClass *OuterClass)
{
    if (TemplateFunction->GetNativeFunc() == FLuaInvoker::execCallLua)
        return;

    FNativeFuncPtr *NativePtr = CachedNatives.Find(TemplateFunction);
    if (!NativePtr)
    {
#if ENABLE_CALL_OVERRIDDEN_FUNCTION
        // 首先会拷贝一个名称加上"Copy"后缀的NewFunc，NewFunc作为原UFunction的备份
        FName NewFuncName(*FString::Printf(TEXT("%s%s"), *TemplateFunction->GetName(), TEXT("Copy")));
        if (TemplateFunction->HasAnyFunctionFlags(FUNC_Native))
        {
            // call this before duplicate UFunction that has FUNC_Native to eliminate "Failed to bind native function" warnings.
            OuterClass->AddNativeFunction(*NewFuncName.ToString(), TemplateFunction->GetNativeFunc());
        }
        UFunction *NewFunc = DuplicateUFunction(TemplateFunction, OuterClass, NewFuncName);
        GReflectionRegistry.AddOverriddenFunction(TemplateFunction, NewFunc);
#endif
        // 如果原UFunction有NativeFunc指针和字节码，就把它保存到CachedNatives容器和CachedScripts中做记录，用于以后恢复UFunction
        // 毕竟在直接修改UFunction实例，在Editor中PIE结束不恢复，会导致UFunction内存坏掉
        CachedNatives.Add(TemplateFunction, TemplateFunction->GetNativeFunc());
        if (!TemplateFunction->HasAnyFunctionFlags(FUNC_Native) && TemplateFunction->Script.Num() > 0)
        {
            CachedScripts.Add(TemplateFunction, TemplateFunction->Script);
            TemplateFunction->Script.Empty(3);
        }
        // 保存信息后，就可以使用和上面相同的UFunction逻辑覆盖步骤，修改NativeFunc指针和字节码了，只不过这次直接操作的原UFunction
        OverrideUFunction(TemplateFunction, (FNativeFuncPtr)&FLuaInvoker::execCallLua, GReflectionRegistry.RegisterFunction(TemplateFunction));
    }
}

/**
 * Replace action inputs
 */
void UUnLuaManager::ReplaceActionInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();

    TSet<FName> ActionNames;
    int32 NumActionBindings = InputComponent->GetNumActionBindings();
    for (int32 i = 0; i < NumActionBindings; ++i)
    {
        FInputActionBinding &IAB = InputComponent->GetActionBinding(i);
        FName Name = GET_INPUT_ACTION_NAME(IAB);
        FString ActionName = Name.ToString();
        ActionNames.Add(Name);

        FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *ActionName, SReadableInputEvent[IAB.KeyEvent]));
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputActionFunc, Class, FuncName);
            IAB.ActionDelegate.BindDelegate(Actor, FuncName);
        }

        if (!IS_INPUT_ACTION_PAIRED(IAB))
        {
            EInputEvent IE = IAB.KeyEvent == IE_Pressed ? IE_Released : IE_Pressed;
            FuncName = FName(*FString::Printf(TEXT("%s_%s"), *ActionName, SReadableInputEvent[IE]));
            if (LuaFunctions.Find(FuncName))
            {
                AddFunction(InputActionFunc, Class, FuncName);
                FInputActionBinding AB(Name, IE);
                AB.ActionDelegate.BindDelegate(Actor, FuncName);
                InputComponent->AddActionBinding(AB);
            }
        }
    }

    EInputEvent IEs[] = { IE_Pressed, IE_Released };
    TSet<FName> DiffActionNames = DefaultActionNames.Difference(ActionNames);
    for (TSet<FName>::TConstIterator It(DiffActionNames); It; ++It)
    {
        FName ActionName = *It;
        for (int32 i = 0; i < 2; ++i)
        {
            FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *ActionName.ToString(), SReadableInputEvent[IEs[i]]));
            if (LuaFunctions.Find(FuncName))
            {
                AddFunction(InputActionFunc, Class, FuncName);
                FInputActionBinding AB(ActionName, IEs[i]);
                AB.ActionDelegate.BindDelegate(Actor, FuncName);
                InputComponent->AddActionBinding(AB);
            }
        }
    }
}

/**
 * Replace key inputs
 */
void UUnLuaManager::ReplaceKeyInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();

    TArray<FKey> Keys;
    TArray<bool> PairedKeys;
    TArray<EInputEvent> InputEvents;
    for (FInputKeyBinding &IKB : InputComponent->KeyBindings)
    {
        int32 Index = Keys.Find(IKB.Chord.Key);
        if (Index == INDEX_NONE)
        {
            Keys.Add(IKB.Chord.Key);
            PairedKeys.Add(false);
            InputEvents.Add(IKB.KeyEvent);
        }
        else
        {
            PairedKeys[Index] = true;
        }

        FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *IKB.Chord.Key.ToString(), SReadableInputEvent[IKB.KeyEvent]));
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputActionFunc, Class, FuncName);
            IKB.KeyDelegate.BindDelegate(Actor, FuncName);
        }
    }

    for (int32 i = 0; i< Keys.Num(); ++i)
    {
        if (!PairedKeys[i])
        {
            EInputEvent IE = InputEvents[i] == IE_Pressed ? IE_Released : IE_Pressed;
            FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *Keys[i].ToString(), SReadableInputEvent[IE]));
            if (LuaFunctions.Find(FuncName))
            {
                AddFunction(InputActionFunc, Class, FuncName);
                FInputKeyBinding IKB(FInputChord(Keys[i]), IE);
                IKB.KeyDelegate.BindDelegate(Actor, FuncName);
                InputComponent->KeyBindings.Add(IKB);
            }
        }
    }

    EInputEvent IEs[] = { IE_Pressed, IE_Released };
    for (const FKey &Key : AllKeys)
    {
        if (Keys.Find(Key) != INDEX_NONE)
        {
            continue;
        }
        for (int32 i = 0; i < 2; ++i)
        {
            FName FuncName = FName(*FString::Printf(TEXT("%s_%s"), *Key.ToString(), SReadableInputEvent[IEs[i]]));
            if (LuaFunctions.Find(FuncName))
            {
                AddFunction(InputActionFunc, Class, FuncName);
                FInputKeyBinding IKB(FInputChord(Key), IEs[i]);
                IKB.KeyDelegate.BindDelegate(Actor, FuncName);
                InputComponent->KeyBindings.Add(IKB);
            }
        }
    }
}

/**
 * Replace axis inputs
 */
void UUnLuaManager::ReplaceAxisInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();

    TSet<FName> AxisNames;
    for (FInputAxisBinding &IAB : InputComponent->AxisBindings)
    {
        AxisNames.Add(IAB.AxisName);
        if (LuaFunctions.Find(IAB.AxisName))
        {
            AddFunction(InputAxisFunc, Class, IAB.AxisName);
            IAB.AxisDelegate.BindDelegate(Actor, IAB.AxisName);
        }
    }

    TSet<FName> DiffAxisNames = DefaultAxisNames.Difference(AxisNames);
    for (TSet<FName>::TConstIterator It(DiffAxisNames); It; ++It)
    {
        if (LuaFunctions.Find(*It))
        {
            AddFunction(InputAxisFunc, Class, *It);
            FInputAxisBinding &IAB = InputComponent->BindAxis(*It);
            IAB.AxisDelegate.BindDelegate(Actor, *It);
        }
    }
}

/**
 * Replace touch inputs
 */
void UUnLuaManager::ReplaceTouchInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();

    TArray<EInputEvent> InputEvents = { IE_Pressed, IE_Released, IE_Repeat };        // IE_DoubleClick?
    for (FInputTouchBinding &ITB : InputComponent->TouchBindings)
    {
        InputEvents.Remove(ITB.KeyEvent);
        FName FuncName = FName(*FString::Printf(TEXT("Touch_%s"), SReadableInputEvent[ITB.KeyEvent]));
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputTouchFunc, Class, FuncName);
            ITB.TouchDelegate.BindDelegate(Actor, FuncName);
        }
    }

    for (EInputEvent IE : InputEvents)
    {
        FName FuncName = FName(*FString::Printf(TEXT("Touch_%s"), SReadableInputEvent[IE]));
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputTouchFunc, Class, FuncName);
            FInputTouchBinding ITB(IE);
            ITB.TouchDelegate.BindDelegate(Actor, FuncName);
            InputComponent->TouchBindings.Add(ITB);
        }
    }
}

/**
 * Replace axis key inputs
 */
void UUnLuaManager::ReplaceAxisKeyInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();
    for (FInputAxisKeyBinding &IAKB : InputComponent->AxisKeyBindings)
    {
        FName FuncName = IAKB.AxisKey.GetFName();
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputAxisFunc, Class, FuncName);
            IAKB.AxisDelegate.BindDelegate(Actor, FuncName);
        }
    }
}

/**
 * Replace vector axis inputs
 */
void UUnLuaManager::ReplaceVectorAxisInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();
    for (FInputVectorAxisBinding &IVAB : InputComponent->VectorAxisBindings)
    {
        FName FuncName = IVAB.AxisKey.GetFName();
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputVectorAxisFunc, Class, FuncName);
            IVAB.AxisDelegate.BindDelegate(Actor, FuncName);
        }
    }
}

/**
 * Replace gesture inputs
 */
void UUnLuaManager::ReplaceGestureInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions)
{
    UClass *Class = Actor->GetClass();
    for (FInputGestureBinding &IGB : InputComponent->GestureBindings)
    {
        FName FuncName = IGB.GestureKey.GetFName();
        if (LuaFunctions.Find(FuncName))
        {
            AddFunction(InputGestureFunc, Class, FuncName);
            IGB.GestureDelegate.BindDelegate(Actor, FuncName);
        }
    }
}

/**
 * Record a binded UObject
 */
void UUnLuaManager::AddAttachedObject(UObjectBaseUtility *Object, int32 ObjectRef)
{
    check(Object);

    GObjectReferencer.AddObjectRef((UObject*)Object);

    AttachedObjects.Add(Object, ObjectRef);

    if (Object->IsA<AActor>())
    {
        AttachedActors.Add((AActor*)Object);
    }
}

/**
 * Get lua ref of a recorded binded UObject
 */
void UUnLuaManager::ReleaseAttachedObjectLuaRef(UObjectBaseUtility* Object)
{   
    int32* ObjectLuaRef = AttachedObjects.Find(Object);
    if ((ObjectLuaRef)
        &&(*ObjectLuaRef != LUA_REFNIL))
    {   
#if UNLUA_ENABLE_DEBUG != 0
        UE_LOG(LogUnLua, Log, TEXT("ReleaseAttachedObjectLuaRef : %s,%p,%d"), *Object->GetName(), Object, *ObjectLuaRef);
#endif
        luaL_unref(UnLua::GetState(), LUA_REGISTRYINDEX, *ObjectLuaRef);
        AttachedObjects.Remove(Object);
    }
}
