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

#include "UObject/UObjectArray.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericApplication.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UnLuaBase.h"

class FLuaContext : public FUObjectArray::FUObjectCreateListener, public FUObjectArray::FUObjectDeleteListener
{
public:
    UNLUA_API static FLuaContext* Create();

    // 注册不同的Engine代理
    void RegisterDelegates();

    // 创建Lua状态机(主线程)和注册创建基础库/表/类
    void CreateState();
    // 启动或者关闭UnLua
    void SetEnable(bool InEnable);
    // 查询是否启动UnLua
    bool IsEnable() const;

    // 导出方法
    bool ExportFunction(UnLua::IExportedFunction *Function);
    // 导出Enum
    bool ExportEnum(UnLua::IExportedEnum *Enum);
    // 导出Class
    bool ExportClass(UnLua::IExportedClass *Class);
    // 查找导出Class
    UnLua::IExportedClass* FindExportedClass(FName Name);
    // 查找导出反射Class
    UnLua::IExportedClass* FindExportedReflectedClass(FName Name);
    // 查找导出非反射Class
    UnLua::IExportedClass* FindExportedNonReflectedClass(FName Name);

    // 新增类型接口
    bool AddTypeInterface(FName Name, TSharedPtr<UnLua::ITypeInterface> TypeInterface);
    // 查找类型接口
    TSharedPtr<UnLua::ITypeInterface> FindTypeInterface(FName Name);

    // 尝试绑定Lua
    bool TryToBindLua(UObjectBaseUtility *Object);

    // 新增库名
    void AddLibraryName(const TCHAR *LibraryName) { LibraryNames.Add(LibraryName); }
    // 新增模块名
    void AddModuleName(const TCHAR *ModuleName) { ModuleNames.AddUnique(ModuleName); }
    // 新增查找器
    void AddSearcher(int (*Searcher)(lua_State *), int Index);
    // 新增编译的库加载器
    void AddBuiltinLoader(const TCHAR *Name, int (*Loader)(lua_State *)) { BuiltinLoaders.Add(Name, Loader); }

#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
    // 当WorldTick开始
    void OnWorldTickStart(UWorld *World, ELevelTick TickType, float DeltaTime);
#else
    void OnWorldTickStart(ELevelTick TickType, float DeltaTime);
#endif
    // 当World清理
    void OnWorldCleanup(UWorld *World, bool bSessionEnded, bool bCleanupResources);
    // 当Engine初始化
    void OnPostEngineInit();
    // 预退出
    void OnPreExit();
    // 异步加载更新
    void OnAsyncLoadingFlushUpdate();
    // 当崩溃时
    void OnCrash();
    // 当加载Map
    void PostLoadMapWithWorld(UWorld *World);
    void OnPostGarbageCollect();
    // 延迟绑定Object
    void OnDelayBindObject(UObject* Object);

#if WITH_EDITOR
    // 预开始PIE
    void PreBeginPIE(bool bIsSimulating);
    // PIE开始
    void PostPIEStarted(bool bIsSimulating);
    // PIE结束
    void PrePIEEnded(bool bIsSimulating);
#endif

    // 获取导出反射Class
    const TMap<FName, UnLua::IExportedClass*>& GetExportedReflectedClasses() const { return ExportedReflectedClasses; }
    // 获取导出非反射Class
    const TMap<FName, UnLua::IExportedClass*>& GetExportedNonReflectedClasses() const { return ExportedNonReflectedClasses; }
    // 获取导出Enum
    const TArray<UnLua::IExportedEnum*>& GetExportedEnums() const { return ExportedEnums; }
    // 获取导出方法
    const TArray<UnLua::IExportedFunction*>& GetExportedFunctions() const { return ExportedFunctions; }
    // 获取导出编译的库
    const TMap<const TCHAR *, int (*)(lua_State *)>& GetBuiltinLoaders() const { return BuiltinLoaders; } 

    // 添加Thread
    void AddThread(lua_State *Thread, int32 ThreadRef);
    // (中断后)继续Thread
    void ResumeThread(int32 ThreadRef);
    // 清理Thread
    void CleanupThreads();
    // 查找Thread
    int32 FindThread(lua_State *Thread);

    // 获取Manage
    FORCEINLINE class UUnLuaManager* GetManager() const { return Manager; }

    // 操作符重载
    FORCEINLINE operator lua_State*() const { return L; }

    // interfaces of FUObjectArray::FUObjectCreateListener and FUObjectArray::FUObjectDeleteListener
    // UObject创建通知
    virtual void NotifyUObjectCreated(const class UObjectBase *InObject, int32 Index) override;
    // UObject销毁通知
    virtual void NotifyUObjectDeleted(const class UObjectBase *InObject, int32 Index) override;
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 22)
    // UObjectArray销毁通知
	virtual void OnUObjectArrayShutdown() override;
#endif

    // UObject是否有效
	bool IsUObjectValid(UObjectBase* UObjPtr);

    // 获取Manage
    UUnLuaManager* GetUnLuaManager();

private:
    FLuaContext();
    ~FLuaContext();

    // allocator used for 'lua_newstate'
    // 内存分配器
    static void* LuaAllocator(void *ud, void *ptr, size_t osize, size_t nsize);

    // 初始化UnLua
    void Initialize();
    // 清理UnLua
    void Cleanup(bool bFullCleanup = false, UWorld *World = nullptr);

    // 当GameViewport输入
    bool OnGameViewportInputKey(FKey InKey, FModifierKeysState ModifierKeyState, EInputEvent EventType);


    lua_State *L;

    UUnLuaManager *Manager;

    FDelegateHandle OnActorSpawnedHandle;
    FDelegateHandle OnWorldTickStartHandle;
    FDelegateHandle OnPostGarbageCollectHandle;

    // Classes/Enums的元表
    TArray<FString> LibraryNames;       // metatables for classes/enums
    // 加载的Lua模块
    TArray<FString> ModuleNames;        // required Lua modules

    // 候选UObject
    TArray<UObject*> Candidates;        // binding candidates during async loading

    TArray<UnLua::IExportedFunction*> ExportedFunctions;                // statically exported global functions
    TArray<UnLua::IExportedEnum*> ExportedEnums;                        // statically exported enums
    TMap<FName, UnLua::IExportedClass*> ExportedReflectedClasses;       // statically exported reflected classes
    TMap<FName, UnLua::IExportedClass*> ExportedNonReflectedClasses;    // statically exported non-reflected classes

    TMap<FName, TSharedPtr<UnLua::ITypeInterface>> TypeInterfaces;      // registered type interfaces

    TMap<const TCHAR *, int (*)(lua_State *)> BuiltinLoaders;

    //!!!Fix!!!
    //thread need refine
    TMap<lua_State*, int32> ThreadToRef;                                // coroutine -> ref
    TMap<int32, lua_State*> RefToThread;                                // ref -> coroutine
	TMap<UObjectBase *, int32> UObjPtr2Idx;                             // UObject pointer -> index in GUObjectArray
    TMap<UObjectBase*, FString> UObjPtr2Name;                           // UObject pointer -> Name for debug purpose
    FCriticalSection Async2MainCS;                                      // async loading thread and main thread sync lock

#if WITH_EDITOR
    void *LuaHandle;
#endif

    TArray<class UInputComponent*> CandidateInputComponents;
    TArray<UGameInstance*> GameInstances;

    bool bEnable;
};

extern class FLuaContext *GLuaCxt;
