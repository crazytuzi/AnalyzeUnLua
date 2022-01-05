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

#include "InputCoreTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "UnLuaCompatibility.h"
#include "ReflectionUtils/ReflectionRegistry.h"
#include "UnLuaManager.generated.h"

UCLASS()
class UUnLuaManager : public UObject
{
    GENERATED_BODY()

public:
    UUnLuaManager();
    //~UUnLuaManager();

    // 对UObject绑定Lua Module
    bool Bind(UObjectBaseUtility *Object, UClass *Class, const TCHAR *InModuleName, int32 InitializerTableRef = INDEX_NONE);

    // 当Module热修复后
    bool OnModuleHotfixed(const TCHAR *InModuleName);

    // UObject删除通知
    void NotifyUObjectDeleted(const UObjectBase *Object, bool bClass = false);

    // 清理
    void Cleanup(class UWorld *World, bool bFullCleanup);

    // 清理Class相关
    void CleanUpByClass(UClass *Class);

    void PostCleanup();

    // 获取默认的Axis/Action输入
    void GetDefaultInputs();

    // 清理默认的Axis/Action输入
    void CleanupDefaultInputs();

    // 替换输入
    bool ReplaceInputs(AActor *Actor, class UInputComponent *InputComponent);

    // 释放一个Lua引用的UObject引用
    void ReleaseAttachedObjectLuaRef(UObjectBaseUtility* Object);

    // 当地图加载
    void OnMapLoaded(UWorld *World);

    // 当Actor创建
    void OnActorSpawned(class AActor *Actor);

    // 当Actor销毁
    UFUNCTION()
    void OnActorDestroyed(class AActor *Actor);

    // 当LatentAction完成
    UFUNCTION()
    void OnLatentActionCompleted(int32 LinkID);

    // Action输入
    UFUNCTION(BlueprintImplementableEvent)
    void InputAction(FKey Key);

    // Axis输入
    UFUNCTION(BlueprintImplementableEvent)
    void InputAxis(float AxisValue);

    // Touch输入
    UFUNCTION(BlueprintImplementableEvent)
    void InputTouch(ETouchIndex::Type FingerIndex, const FVector &Location);

    // VectorAxis输入
    UFUNCTION(BlueprintImplementableEvent)
    void InputVectorAxis(const FVector &AxisValue);

    // Gesture输入
    UFUNCTION(BlueprintImplementableEvent)
    void InputGesture(float Value);

    // AnimNotify
    UFUNCTION(BlueprintImplementableEvent)
    void TriggerAnimNotify();

private:
    // 当继承类绑定
    void OnDerivedClassBinded(UClass *DerivedClass, UClass *BaseClass);

    // 获取目标Class
    UClass* GetTargetClass(UClass *Class, UFunction **GetModuleNameFunc = nullptr);

    // 绑定内部实现   
    bool BindInternal(UObjectBaseUtility *Object, UClass *Class, const FString &InModuleName, bool bNewCreated, bool bMultipleLuaBind, FString &Error);
    // 更新Class条件
    bool ConditionalUpdateClass(UClass *Class, const TSet<FName> &LuaFunctions, TMap<FName, UFunction*> &UEFunctions);

    // 覆盖方法
    void OverrideFunctions(const TSet<FName> &LuaFunctions, TMap<FName, UFunction*> &UEFunctions, UClass *OuterClass, bool bCheckFuncNetMode = false);
    // 覆盖方法
    void OverrideFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName);
    // 添加方法
    void AddFunction(UFunction *TemplateFunction, UClass *OuterClass, FName NewFuncName);
    // 替换方法
    void ReplaceFunction(UFunction *TemplateFunction, UClass *OuterClass);

    // 替换Action输入
    void ReplaceActionInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换Key输入
    void ReplaceKeyInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换Axis输入
    void ReplaceAxisInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换Touch输入
    void ReplaceTouchInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换AxisKey输入
    void ReplaceAxisKeyInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换VectorKey输入
    void ReplaceVectorAxisInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);
    // 替换Gesture输入
    void ReplaceGestureInputs(AActor *Actor, UInputComponent *InputComponent, TSet<FName> &LuaFunctions);

    // UObject新增引用
    void AddAttachedObject(UObjectBaseUtility *Object, int32 ObjectRef);

    // 清理复制的UFunction
    void CleanupDuplicatedFunctions();
    // 清理缓存的NativeFunction
    void CleanupCachedNatives();
    // 清理缓存的Script
    void CleanupCachedScripts();

    // 当Class清理
    void OnClassCleanup(UClass *Class);
    // 重置UFunction
    void ResetUFunction(UFunction *Function, FNativeFuncPtr NativeFuncPtr);
    // 移除复制的UFunction
    void RemoveDuplicatedFunctions(UClass *Class, TArray<UFunction*> &Functions);

    TMap<UClass*, FString> ModuleNames;
    TMap<FString, int16> RealModuleNames;
    TMap<FString, UClass*> Classes;
    TMap<UClass*, TMap<FName, UFunction*>> OverridableFunctions;
    TMap<UClass*, TArray<UFunction*>> DuplicatedFunctions;
    TMap<FString, TSet<FName>> ModuleFunctions;
    TMap<UFunction*, FNativeFuncPtr> CachedNatives;
    TMap<UFunction*, TArray<uint8>> CachedScripts;

#if !ENABLE_CALL_OVERRIDDEN_FUNCTION
    TMap<UFunction*, UFunction*> New2TemplateFunctions;
#endif

    TMap<UClass*, TArray<UClass*>> Base2DerivedClasses;
    TMap<UClass*, UClass*> Derived2BaseClasses;

    TSet<FName> DefaultAxisNames;
    TSet<FName> DefaultActionNames;
    TArray<FKey> AllKeys;

    TMap<UObjectBaseUtility*, int32> AttachedObjects;
    TSet<AActor*> AttachedActors;

    UFunction *InputActionFunc;
    UFunction *InputAxisFunc;
    UFunction *InputTouchFunc;
    UFunction *InputVectorAxisFunc;
    UFunction *InputGestureFunc;
    UFunction *AnimNotifyFunc;
};
