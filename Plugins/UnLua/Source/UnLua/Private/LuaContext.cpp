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

#include "LuaContext.h"
#include "LuaCore.h"
#include "LuaDynamicBinding.h"
#include "UnLuaEx.h"
#include "UnLuaManager.h"
#include "UnLuaInterface.h"
#include "UnLuaDelegates.h"
#include "UnLuaDebugBase.h"
#include "UEObjectReferencer.h"
#include "CollisionHelper.h"
#include "DelegateHelper.h"
#include "ReflectionUtils/PropertyCreator.h"
#include "DefaultParamCollection.h"
#include "ReflectionUtils/ReflectionRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "DelegateHelper.h"

#if WITH_EDITOR
#include "Editor.h"
#include "GameDelegates.h"
#endif


/**
 * Statically exported callback for 'Hotfix'
 * 热修复
 */
bool OnModuleHotfixed(const char* ModuleName)
{
    if (!GLuaCxt->IsEnable() || !ModuleName)
    {
        UE_LOG(LogUnLua, Log, TEXT("%s: Invalid module name!"), ANSI_TO_TCHAR(__FUNCTION__));
        return false;
    }

    bool bSuccess = GLuaCxt->GetUnLuaManager()->OnModuleHotfixed(UTF8_TO_TCHAR(ModuleName));
#if !UE_BUILD_SHIPPING
    if (!bSuccess)
    {
        UE_LOG(LogUnLua, Log, TEXT("%s: Failed to update module!"), ANSI_TO_TCHAR(__FUNCTION__));
    }
#endif
    return bSuccess;
}

EXPORT_FUNCTION(bool, OnModuleHotfixed, const char*)


FLuaContext* GLuaCxt = nullptr;

/**
 * Create GLuaCxt
 * 创建GLuaCxt
 */
FLuaContext* FLuaContext::Create()
{
    if (!GLuaCxt)
    {
        static FLuaContext Context;
        GLuaCxt = &Context;
    }
    return GLuaCxt;
}

/**
 * Register different engine delegates
 */
void FLuaContext::RegisterDelegates()
{
#if SUPPORTS_COMMANDLET == 0
    if (IsRunningCommandlet())
    {
        return;
    }
#endif

    FWorldDelegates::OnWorldCleanup.AddRaw(this, &FLuaContext::OnWorldCleanup);
    FCoreDelegates::OnPostEngineInit.AddRaw(this, &FLuaContext::OnPostEngineInit);   // called before FCoreDelegates::OnFEngineLoopInitComplete.Broadcast(), after GEngine->Init(...)
    FCoreDelegates::OnPreExit.AddRaw(this, &FLuaContext::OnPreExit);                 // called before StaticExit()
    FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &FLuaContext::OnAsyncLoadingFlushUpdate);
    FCoreDelegates::OnHandleSystemError.AddRaw(this, &FLuaContext::OnCrash);
    FCoreDelegates::OnHandleSystemEnsure.AddRaw(this, &FLuaContext::OnCrash);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FLuaContext::PostLoadMapWithWorld);
    //FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FLuaContext::OnPreGarbageCollect);

#if WITH_EDITOR
    FEditorDelegates::PreBeginPIE.AddRaw(this, &FLuaContext::PreBeginPIE);
    FEditorDelegates::PostPIEStarted.AddRaw(this, &FLuaContext::PostPIEStarted);
    FEditorDelegates::PrePIEEnded.AddRaw(this, &FLuaContext::PrePIEEnded);
#endif

    GUObjectArray.AddUObjectCreateListener(this);    // add listener for creating UObject
    GUObjectArray.AddUObjectDeleteListener(this);    // add listener for deleting UObject
}

/**
 * Create Lua state (main thread) and register/create base libs/tables/classes
 */
void FLuaContext::CreateState()
{
#if SUPPORTS_COMMANDLET == 0
    if (IsRunningCommandlet())
    {
        return;
    }
#endif

    if (!L)
    {

        // 创建Lua主线程
        L = lua_newstate(FLuaContext::LuaAllocator, nullptr);       // create main Lua thread
        check(L);
        // 打开所有的Lua标准库
        luaL_openlibs(L);                                           // open all standard Lua libraries

        // 自定义加载器
        AddSearcher(LoadFromCustomLoader, 2);
        // 文件系统
        AddSearcher(LoadFromFileSystem, 3);
        // 编译的库
        AddSearcher(LoadFromBuiltinLibs, 4);

        // 创建ObjectMap(弱表v)
        lua_pushstring(L, "ObjectMap");                             // create weak table 'ObjectMap'
        CreateWeakValueTable(L);
        lua_rawset(L, LUA_REGISTRYINDEX);

        // 创建StructMap(弱表v)
        lua_pushstring(L, "StructMap");                             // create weak table 'StructMap'
        CreateWeakValueTable(L);
        lua_rawset(L, LUA_REGISTRYINDEX);

        // 创建ScriptContainerMap(弱表v)
        lua_pushstring(L, "ScriptContainerMap");                    // create weak table 'ScriptContainerMap'
        CreateWeakValueTable(L);
        lua_rawset(L, LUA_REGISTRYINDEX);

        // 创建ArrayMap(弱表v)
        lua_pushstring(L, "ArrayMap");                              // create weak table 'ArrayMap'
        CreateWeakValueTable(L);
        lua_rawset(L, LUA_REGISTRYINDEX);

        // 创建命名'UE'空间
        CreateNamespaceForUE(L);                                    // create 'UE' namespace (table)

        // register global Lua functions
        // lua_register的功能：将一个C function放入Lua状态机的全局表中
        // 创建全局Lua方法
        // 注册Enum
        lua_register(L, "RegisterEnum", Global_RegisterEnum);
        // 注册Class
        lua_register(L, "RegisterClass", Global_RegisterClass);
        // 获取UProperty
        lua_register(L, "GetUProperty", Global_GetUProperty);
        // 设置UProperty
        lua_register(L, "SetUProperty", Global_SetUProperty);
        // 加载Object
        lua_register(L, "LoadObject", Global_LoadObject);
        // 加载Class
        lua_register(L, "LoadClass", Global_LoadClass);
        // 创建Object
        lua_register(L, "NewObject", Global_NewObject);
        // 新增Class白名单
        lua_register(L, "UnLua_AddToClassWhiteSet", Global_AddToClassWhiteSet);
        // 移除Class白名单
        lua_register(L, "UnLua_RemoveFromClassWhiteSet", Global_RemoveFromClassWhiteSet);
        // 反注册Class
        lua_register(L, "UnLua_UnRegisterClass", Global_UnRegisterClass);

        // UE打印
        lua_register(L, "UEPrint", Global_Print);

        // register collision related enums
        // 注册碰撞Enum
        FCollisionHelper::Initialize();     // initialize collision helper stuff
        RegisterECollisionChannel(L);
        RegisterEObjectTypeQuery(L);
        RegisterETraceTypeQuery(L);

        if (FUnLuaDelegates::ConfigureLuaGC.IsBound())
        {
            FUnLuaDelegates::ConfigureLuaGC.Execute(L);
        }
        else
        {
            // UnLua默认使用的Lua5.4.2,区别SLua默认使用的是5.3.4
#if 504 == LUA_VERSION_NUM
            // 分代 gc
            lua_gc(L, LUA_GCGEN);
#else
            // default Lua GC config in UnLua
            lua_gc(L, LUA_GCSETPAUSE, 100);
            lua_gc(L, LUA_GCSETSTEPMUL, 5000);
#endif
        }

        // add new package path
        // 新增包路径
        FString LuaSrcPath = GLuaSrcFullPath + TEXT("?.lua");
        AddPackagePath(L, TCHAR_TO_UTF8(*LuaSrcPath));

        FUnLuaDelegates::OnPreStaticallyExport.Broadcast();

        // 提前注册好UObject和UClass
        RegisterClass(L, "UClass", "UObject");                      // register base class

        // register statically exported classes
        // 注册静态导出的UClass
        for (TMap<FName, UnLua::IExportedClass*>::TIterator It(ExportedNonReflectedClasses); It; ++It)
        {
            It.Value()->Register(L);
        }

        // register statically exported global functions
        // 注册静态导出的全局方法
        for (UnLua::IExportedFunction* Function : ExportedFunctions)
        {
            Function->Register(L);
        }

        // register statically exported enums
        // 注册静态导出的Enum
        for (UnLua::IExportedEnum* Enum : ExportedEnums)
        {
            Enum->Register(L);
        }

        FUnLuaDelegates::OnLuaStateCreated.Broadcast(L);
    }
}

/**
 * Enable UnLua
 */
void FLuaContext::SetEnable(bool InEnable)
{
    if (InEnable)
    {
        Initialize();
    }
    else
    {
        Cleanup(true);
    }
}

/**
 * Is UnLua enabled?
 */
bool FLuaContext::IsEnable() const
{
    return L && bEnable;
}

/**
 * Statically export a global functions
 */
bool FLuaContext::ExportFunction(UnLua::IExportedFunction* Function)
{
    if (Function)
    {
        ExportedFunctions.Add(Function);
        return true;
    }
    return false;
}

/**
 * Statically export an enum
 */
bool FLuaContext::ExportEnum(UnLua::IExportedEnum* Enum)
{
    if (Enum)
    {
        ExportedEnums.Add(Enum);
        return true;
    }
    return false;
}

/**
 * Statically export a class
 */
bool FLuaContext::ExportClass(UnLua::IExportedClass* Class)
{
    if (Class)
    {
        TMap<FName, UnLua::IExportedClass*>& ExportedClasses = Class->IsReflected() ? ExportedReflectedClasses : ExportedNonReflectedClasses;
        ExportedClasses.Add(Class->GetName(), Class);
        return true;
    }
    return false;
}

/**
 * Find a statically exported class
 */
UnLua::IExportedClass* FLuaContext::FindExportedClass(FName Name)
{
    UnLua::IExportedClass** Class = ExportedReflectedClasses.Find(Name);
    if (Class)
    {
        return *Class;
    }
    Class = ExportedNonReflectedClasses.Find(Name);
    return Class ? *Class : nullptr;
}

/**
 * Find a statically exported reflected class
 */
UnLua::IExportedClass* FLuaContext::FindExportedReflectedClass(FName Name)
{
    UnLua::IExportedClass** Class = ExportedReflectedClasses.Find(Name);
    return Class ? *Class : nullptr;
}

/**
 * Find a statically exported non reflected class
 */
UnLua::IExportedClass* FLuaContext::FindExportedNonReflectedClass(FName Name)
{
    UnLua::IExportedClass** Class = ExportedNonReflectedClasses.Find(Name);
    return Class ? *Class : nullptr;
}

/**
 * Add a type interface
 */
bool FLuaContext::AddTypeInterface(FName Name, TSharedPtr<UnLua::ITypeInterface> TypeInterface)
{
    if (Name == NAME_None || !TypeInterface)
    {
        return false;
    }

    TSharedPtr<UnLua::ITypeInterface>* TypeInterfacePtr = TypeInterfaces.Find(Name);
    if (!TypeInterfacePtr)
    {
        TypeInterfaces.Add(Name, TypeInterface);
    }
    return true;
}

/**
 * Find a type interface
 */
TSharedPtr<UnLua::ITypeInterface> FLuaContext::FindTypeInterface(FName Name)
{
    TSharedPtr<UnLua::ITypeInterface>* TypeInterfacePtr = TypeInterfaces.Find(Name);
    return TypeInterfacePtr ? *TypeInterfacePtr : TSharedPtr<UnLua::ITypeInterface>();
}

/**
* Delay Bind RF_NeedPostLoad Object
**/
void FLuaContext::OnDelayBindObject(UObject* Object)
{
    if (GLuaCxt->IsUObjectValid(Object))
    {
        if (!FUObjectThreadContext::Get().IsRoutingPostLoad && !Object->HasAllFlags(RF_NeedPostLoad | RF_NeedInitialization))
        {
            UE_LOG(LogUnLua, Log, TEXT("%s[%llu]: Delay bind object %s,%p"), ANSI_TO_TCHAR(__FUNCTION__), GFrameCounter, *Object->GetName(), Object);
            TryToBindLua(Object);
        }
        else
        {
            AsyncTask(ENamedThreads::GameThread, [this, Object]()
                {
                    this->OnDelayBindObject(Object);
                });
        }
    }
}

/**
 * Try to bind Lua module for a UObject
 * 主要是对UObject做一些检查，把满足条件的UObject交给UnLuaManager去执行Bind
 */
bool FLuaContext::TryToBindLua(UObjectBaseUtility* Object)
{
    // 容错判断，bind要在UnLua初始化之后进行
    if (!bEnable || !IsUObjectValid(Object))
    {
        return false;
    }

    // 过滤掉CDO对象和Archetype对象，不对他们进行绑定
    const bool bIsCDO = Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject); 
    if (bIsCDO)
    {
        // filter out class default and template objects
        return false;
    }
    
    UClass* Class = Object->GetClass();
    // UPackage和UClass跳过
    if (Class->IsChildOf<UPackage>() || Class->IsChildOf<UClass>() || Class->HasAnyClassFlags(CLASS_NewerVersionExists))
    {
        // filter out UPackage and UClass and recompiled objects
        return false;
    }

    // 检查有没有实现UnLuaInterface接口
    static UClass* InterfaceClass = UUnLuaInterface::StaticClass();

    //all bind operation should be in game thread, include dynamic bind
    // 所有绑定操作都应该在GameThread中，包括动态绑定
    // 静态绑定
    if (Class->ImplementsInterface(InterfaceClass))                             // static binding
    {
        // fliter some object in bp nest case
        // RF_WasLoaded & RF_NeedPostLoad?
        UObject* Outer = Object->GetOuter();
        // WidgetTree下的UI控件跳过
        if ((Outer)
            && (Outer->GetFName().IsEqual("WidgetTree")) && Object->HasAllFlags(RF_NeedInitialization | RF_NeedPostLoad | RF_NeedPostLoadSubobjects))
        {
            return false;
        }

        if (GWorld)
        {
            FString ObjectName;
            Object->GetFullName(GWorld, ObjectName);
            // WidgetArchetype和WidgetTree跳过
            if (ObjectName.Contains(".WidgetArchetype:") || ObjectName.Contains(":WidgetTree."))
            {
                UE_LOG(LogUnLua, Warning, TEXT("Filter UObject of %s in WidgetArchetype"), *ObjectName);
                return false;
            }
        }

        // 获取UnLuaInterface接口里实现的方法
        // 必须实现GetModuleName接口函数
        UFunction* Func = Class->FindFunctionByName(FName("GetModuleName"));    // find UFunction 'GetModuleName'. hard coded!!!
        if (Func)
        {
            // native func may not be bind in level bp
            if (!Func->GetNativeFunc())
            {
                Func->Bind();
                if (!Func->GetNativeFunc())
                {
                    UE_LOG(LogUnLua, Warning, TEXT("TryToBindLua: bind native function failed for GetModuleName in object %s"), *Object->GetName());
                    return false;
                }
            }

            if (IsInGameThread())
            {
                FString ModuleName;
                UObject* DefaultObject = Class->GetDefaultObject();             // get CDO
                // 强制调用UObject::ProcessEvent
                DefaultObject->UObject::ProcessEvent(Func, &ModuleName);        // force to invoke UObject::ProcessEvent(...)
                if (ModuleName.Len() < 1)
                {
                    return false;
                }

                // 延迟绑定
                if (Object->HasAllFlags(RF_NeedPostLoad | RF_NeedInitialization))
                {
                    OnDelayBindObject((UObject*)Object);
                    return false;
                }

#if !UE_BUILD_SHIPPING
                if (GLuaDynamicBinding.IsValid(Class) && GLuaDynamicBinding.ModuleName != ModuleName)
                {
                    UE_LOG(LogUnLua, Warning, TEXT("Dynamic binding '%s' ignored as it conflicts static binding '%s'."), *GLuaDynamicBinding.ModuleName, *ModuleName);
                }
#endif

                // 绑定
                return Manager->Bind(Object, Class, *ModuleName, GLuaDynamicBinding.InitializerTableRef);   // bind!!!
            }
            
            // 是否正在异步加载
            if (IsAsyncLoading())
            {
                // check FAsyncLoadingThread::IsMultithreaded()?
                FScopeLock Lock(&Async2MainCS);
                // 将UObject加入候选
                Candidates.Add((UObject*)Object);                           // mark the UObject as a candidate
            }
        }
        return false;
    }

    // 动态绑定
    // 如果在lua中使用"NewObject"和"SpawnActor"，我们可以选择指定提供ModuleName，这样UnLua可以在运行时把一个UObject和ModuleName关联起来，因此称为“动态”
    if (GLuaDynamicBinding.IsValid(Class))                                 // dynamic binding
    {
        return Manager->Bind(Object, Class, *GLuaDynamicBinding.ModuleName, GLuaDynamicBinding.InitializerTableRef);
    }

    return false;
}

void FLuaContext::AddSearcher(int (*Searcher)(lua_State *), int Index)
{
    // if #package.searchers 
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");
    lua_remove(L, -2);
    if(!lua_istable(L, -1))
    {
        UE_LOG(LogUnLua, Warning, TEXT("Invalid package.serachers!"));
        return;
    }

    const uint32 Len = lua_rawlen(L, -1);
    Index = Index < 0 ? (int)(Len + Index + 2) : Index;
    // 前移
    for (int e = (int)Len + 1; e > Index; e--)
    {
        lua_rawgeti(L, -1, e - 1);
        lua_rawseti(L, -2, e);
    }
    
    lua_pushcfunction(L, Searcher);
    lua_rawseti(L, -2, Index);
    lua_pop(L, 1);
}

/**
 * Callback for FWorldDelegates::OnWorldTickStart
 */
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
void FLuaContext::OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaTime)
#else
void FLuaContext::OnWorldTickStart(ELevelTick TickType, float DeltaTime)
#endif
{
    if (!Manager)
    {
        return;
    }

    for (UInputComponent* InputComponent : CandidateInputComponents)
    {
        if (!InputComponent->IsRegistered() || InputComponent->IsPendingKill())
        {
            continue;
        }

        AActor* Actor = Cast<AActor>(InputComponent->GetOuter());
        // 替换输入事件
        Manager->ReplaceInputs(Actor, InputComponent);                              // try to replace/override input events
    }

    CandidateInputComponents.Empty();
    FWorldDelegates::OnWorldTickStart.Remove(OnWorldTickStartHandle);
}

/**
 * Callback for FWorldDelegates::OnWorldCleanup
 */
void FLuaContext::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    if (!World || !bEnable)
    {
        return;
    }

    World->RemoveOnActorSpawnedHandler(OnActorSpawnedHandle);

#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
    Cleanup(IsEngineExitRequested(), World);                    // clean up
#else
    Cleanup(GIsRequestingExit, World);                          // clean up
#endif
}

/**
 * Callback for FCoreDelegates::OnPostEngineInit
 */
void FLuaContext::OnPostEngineInit()
{
#if AUTO_UNLUA_STARTUP && !WITH_EDITOR
    SetEnable(true);
#endif

    // 创建UFunction的默认参数数据
    CreateDefaultParamCollection();                 // create data for default parameters of UFunctions

#if WITH_EDITOR
    UGameViewportClient* GameViewportClient = GEngine->GameViewport;
    if (GameViewportClient)
    {
        GameViewportClient->OnGameViewportInputKey().BindRaw(this, &FLuaContext::OnGameViewportInputKey);   // bind a default input event
    }
#endif
}

/**
 * Callback for FCoreDelegates::OnPreExit
 */
void FLuaContext::OnPreExit()
{
    Cleanup(true);                                  // full clean up
}

/**
 * Callback for FCoreDelegates::OnAsyncLoadingFlushUpdate
 */
void FLuaContext::OnAsyncLoadingFlushUpdate()
{
    if (!Manager)
    {
        return;
    }

    static UClass* InterfaceClass = UUnLuaInterface::StaticClass();

    {
        TArray<UObject*> LocalCandidates;

        {
            FScopeLock Lock(&Async2MainCS);

            //!!!Fix!!!
            // check object is load completed?
            // copy fully loaded object to local cache for bind
            for (int32 i = Candidates.Num() - 1; i >= 0; --i)
            {
                UObject* Object = Candidates[i];
                if ((GLuaCxt->IsUObjectValid(Object))
                    && (!Object->HasAnyFlags(RF_NeedPostLoad))
                    && (!Object->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
                    && (!Object->GetClass()->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading)))
                {
                    LocalCandidates.Add(Object);
                    Candidates.RemoveAt(i);
                }
            }
        }

        for (int32 i = 0; i < LocalCandidates.Num(); ++i)
        {
            UObject* Object = LocalCandidates[i];
            if (Object)
            {
                UFunction* Func = Object->FindFunction(FName("GetModuleName"));
                if (!Func || !Func->GetNativeFunc())
                {
                    continue;
                }
                FString ModuleName;
                Object->UObject::ProcessEvent(Func, &ModuleName);    // force to invoke UObject::ProcessEvent(...)
                if (ModuleName.Len() < 1)
                {
                    continue;
                }
                Manager->Bind(Object, Object->GetClass(), *ModuleName);
            }
        }
    }
}

/**
 * Callback for FCoreDelegates::OnHandleSystemError and FCoreDelegates::OnHandleSystemEnsure
 */
void FLuaContext::OnCrash()
{
    if (!IsInGameThread())
        return;

    const FString LogStr = UnLua::GetLuaCallStack(L);

    if (LogStr.IsEmpty())
    {
        UE_LOG(LogUnLua, Warning, TEXT("Lua state has not been created yet."));
    }
    else
    {
        UE_LOG(LogUnLua, Error, TEXT("%s"), *LogStr);
    }

    GLog->Flush();
}


/**
 * Callback for FCoreUObjectDelegates::PostLoadMapWithWorld
 */
void FLuaContext::PostLoadMapWithWorld(UWorld* World)
{
    if (!World || !bEnable)
    {
        return;
    }

    // !!!Fix!!!
    // gameinstance delay bind, muti lua state support
    UGameInstance* GameInstance = World->GetGameInstance();
    if (GameInstance
        && (!GameInstances.Contains(GameInstance)))
    {
        TryToBindLua(GameInstance);                     // try to bind Lua module for GameInstance
        GameInstances.Add(GameInstance);
    }

    Manager->OnMapLoaded(World);

    // !!!Fix!!!
    // when world is cleanup, this need to remove
    // register callback for spawning an actor
    OnActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(Manager, &UUnLuaManager::OnActorSpawned));
}

#if WITH_EDITOR
/**
 * Callback for FEditorDelegates::PreBeginPIE
 */
void FLuaContext::PreBeginPIE(bool bIsSimulating)
{
#if AUTO_UNLUA_STARTUP
    SetEnable(true);
#endif

    UGameViewportClient* GameViewportClient = GEngine->GameViewport;
    if (GameViewportClient)
    {
        GameViewportClient->OnGameViewportInputKey().BindRaw(this, &FLuaContext::OnGameViewportInputKey);   // bind a default input event
    }
}

/**
 * Callback for FEditorDelegates::PostPIEStarted
 */
void FLuaContext::PostPIEStarted(bool bIsSimulating)
{
    UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
    if (EditorEngine)
    {
        PostLoadMapWithWorld(EditorEngine->PlayWorld);
    }
}

/**
 * Callback for FEditorDelegates::PrePIEEnded
 */
void FLuaContext::PrePIEEnded(bool bIsSimulating)
{
    // close lua env alwaylls
    SetEnable(false);
}

#endif

/**
 * Add a Lua coroutine and its reference in Lua registry
 */
void FLuaContext::AddThread(lua_State* Thread, int32 ThreadRef)
{
    ThreadToRef.Add(Thread, ThreadRef);
    RefToThread.Add(ThreadRef, Thread);
}

/**
 * Starts and resumes a Lua coroutine
 */
void FLuaContext::ResumeThread(int32 ThreadRef)
{
    lua_State** ThreadPtr = RefToThread.Find(ThreadRef);
    if (ThreadPtr)
    {
        lua_State* Thread = *ThreadPtr;
#if 504 == LUA_VERSION_NUM
        int NResults = 0;
        int32 State = lua_resume(Thread, L, 0, &NResults);
#else
        int32 State = lua_resume(Thread, L, 0);
#endif
        if (State == LUA_OK)
        {
            ThreadToRef.Remove(Thread);
            RefToThread.Remove(ThreadRef);
            // 如果协程完成其执行，则删除引用
            luaL_unref(L, LUA_REGISTRYINDEX, ThreadRef);    // remove the reference if the coroutine finishes its execution
        }
    }
}

/**
 * Clean up all Lua coroutines
 */
void FLuaContext::CleanupThreads()
{
    ThreadToRef.Empty();
    RefToThread.Empty();
}

/**
 * Find a Lua coroutine
 */
int32 FLuaContext::FindThread(lua_State* Thread)
{
    int32* ThreadRefPtr = ThreadToRef.Find(Thread);
    return ThreadRefPtr ? *ThreadRefPtr : LUA_REFNIL;
}

/**
 * Callback when a UObjectBase (not full UObject) is created
 */
void FLuaContext::NotifyUObjectCreated(const UObjectBase* InObject, int32 Index)
{
    {
        FScopeLock Lock(&Async2MainCS);
        // 把所有新创建的UObject添加到一个列表中，用于后续判断Object的有效性
        UObjPtr2Idx.Add(const_cast<UObjectBase*>(InObject), Index);
#if UNLUA_ENABLE_DEBUG != 0
        UObjPtr2Name.Add(const_cast<UObjectBase*>(InObject), InObject->GetFName().ToString());
#endif
    }

    if (!bEnable)
    {
        return;
    }

#if WITH_EDITOR
    // Don't bind during cook
    if (GIsCookerLoadingPackage)
    {
        return;
    }
#endif

    // try to bind a Lua module for the object
    // 类型转换
    UObjectBaseUtility* Object = (UObjectBaseUtility*)InObject;
    TryToBindLua(Object);

    // special handling for UInputComponent
    if (!Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Object->IsA<UInputComponent>())
    {
        AActor* Actor = Cast<APlayerController>(Object->GetOuter());
        if (!Actor)
        {
            Actor = Cast<APawn>(Object->GetOuter());
        }
        if (Actor && Actor->GetLocalRole() >= ROLE_AutonomousProxy)
        {
            //!!!Fix!!!
            // when tick start processing, inputcomponent may be invald or changeing
            CandidateInputComponents.AddUnique((UInputComponent*)InObject);
            if (OnWorldTickStartHandle.IsValid())
            {
                FWorldDelegates::OnWorldTickStart.Remove(OnWorldTickStartHandle);
            }
            OnWorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddRaw(this, &FLuaContext::OnWorldTickStart);
        }
    }
}

/**
 * Callback when a UObjectBase (not full UObject) is deleted
 * 每当有UObjectBase被UE删除时，都会进行响应
 * 收到事件后，会把该对象从GObjectReferencer的ReferencedObjects数组中将其移除
 */
void FLuaContext::NotifyUObjectDeleted(const UObjectBase* InObject, int32 Index)
{
    if (!bEnable)
    {
        FScopeLock Lock(&Async2MainCS);
        UObjPtr2Idx.Remove(InObject);

#if UNLUA_ENABLE_DEBUG != 0
        UObjPtr2Name.Remove(InObject);
#endif

        return;
    }

#if UNLUA_ENABLE_DEBUG != 0
    UE_LOG(LogUnLua, Log, TEXT("NotifyUObjectDeleted : %s,%p"), *UObjPtr2Name[InObject], InObject);
#endif

    bool bClass = GReflectionRegistry.NotifyUObjectDeleted(InObject);
    Manager->NotifyUObjectDeleted(InObject, bClass);
    FDelegateHelper::NotifyUObjectDeleted((UObject*)InObject);

    if (CandidateInputComponents.Num() > 0)
    {
        int32 NumRemoved = CandidateInputComponents.Remove((UInputComponent*)InObject);
        if (NumRemoved > 0 && CandidateInputComponents.Num() < 1)
        {
            FWorldDelegates::OnWorldTickStart.Remove(OnWorldTickStartHandle);
        }
    }

    FScopeLock Lock(&Async2MainCS);
    UObjPtr2Idx.Remove(InObject);
    UObjPtr2Name.Remove(InObject);
}


/**
 * Callback when a GUObjectArray is deleted
 */
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 22)
void FLuaContext::OnUObjectArrayShutdown()
{
    bool bEngineExit = false;
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
    bEngineExit = IsEngineExitRequested();
#else
    bEngineExit = GIsRequestingExit;
#endif

    if (bEngineExit)
    {
        // when exiting, remove listeners for creating/deleting UObject
        GUObjectArray.RemoveUObjectCreateListener(GLuaCxt);
        GUObjectArray.RemoveUObjectDeleteListener(GLuaCxt);
    }
}
#endif

/**
 * Robust method to verify uobject
 */
bool FLuaContext::IsUObjectValid(UObjectBase* UObjPtr)
{
    if (!UObjPtr)
    {
        return false;
    }

    int32 UObjIdx = -1;
    {
        FScopeLock Lock(&Async2MainCS);
        if (UObjPtr2Idx.Contains(UObjPtr))
        {
            UObjIdx = UObjPtr2Idx[UObjPtr];
        }
    }

    if (-1 != UObjIdx)
    {
        FUObjectItem* UObjectItem = GUObjectArray.IndexToObject(UObjIdx);
        if (!UObjectItem)
        {
            return false;
        }
        else
        {
            return (UObjPtr == UObjectItem->Object) && ((UObjPtr->GetFlags() & (RF_BeginDestroyed | RF_FinishDestroyed)) == 0)
                    && !UObjectItem->IsUnreachable();
        }
    }
    else
    {
        //!!!Fix!!!
        //all should be false here?
        return false;
    }
}

UUnLuaManager* FLuaContext::GetUnLuaManager()
{
    return Manager;
}


FLuaContext::FLuaContext()
    : L(nullptr), Manager(nullptr), bEnable(false)
{
#if WITH_EDITOR
    LuaHandle = nullptr;
#endif
}

FLuaContext::~FLuaContext()
{
    Cleanup(true);

    if (Manager)
    {
        Manager->RemoveFromRoot();
        Manager = nullptr;
    }

    if (L)
    {
        L = NULL;
    }

#if ENGINE_MAJOR_VERSION <= 4 && ENGINE_MINOR_VERSION < 23
    // when exiting, remove listeners for creating/deleting UObject
    GUObjectArray.RemoveUObjectCreateListener(GLuaCxt);
    GUObjectArray.RemoveUObjectDeleteListener(GLuaCxt);
#endif

    FScopeLock Lock(&Async2MainCS);
    UObjPtr2Idx.Empty();

#if UNLUA_ENABLE_DEBUG != 0
    UObjPtr2Name.Empty();
#endif
}

/**
 * Allocator for Lua VM
 * LuaVM的内存分配器
 */
void* FLuaContext::LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize)
{
    if (nsize == 0)
    {
#if STATS
        const uint32 Size = FMemory::GetAllocSize(ptr);
        DEC_MEMORY_STAT_BY(STAT_UnLua_Lua_Memory, Size);
#endif
        FMemory::Free(ptr);
        return nullptr;
    }

    void* Buffer = nullptr;
    if (!ptr)
    {
        Buffer = FMemory::Malloc(nsize);
#if STATS
        const uint32 Size = FMemory::GetAllocSize(Buffer);
        INC_MEMORY_STAT_BY(STAT_UnLua_Lua_Memory, Size);
#endif
    }
    else
    {
#if STATS
        const uint32 OldSize = FMemory::GetAllocSize(ptr);
#endif
        Buffer = FMemory::Realloc(ptr, nsize);
#if STATS
        const uint32 NewSize = FMemory::GetAllocSize(Buffer);
        if (NewSize > OldSize)
        {
            INC_MEMORY_STAT_BY(STAT_UnLua_Lua_Memory, NewSize - OldSize);
        }
        else
        {
            DEC_MEMORY_STAT_BY(STAT_UnLua_Lua_Memory, OldSize - NewSize);
        }
#endif
    }
    return Buffer;
}

/**
 * Initialize UnLua
 */
void FLuaContext::Initialize()
{
    if (!bEnable)
    {
        // 创建Lua主线程
        CreateState();  // create Lua main thread

        // create UnLuaManager and add it to root
        // 创建UUnLuaManager,同时AddToRoot
        Manager = NewObject<UUnLuaManager>();
        Manager->AddToRoot();

        if (L)
        {
            GPropertyCreator.Cleanup();
            bEnable = true;
            FUnLuaDelegates::OnLuaContextInitialized.Broadcast();
        }
    }
}

/**
 * Clean up UnLua
 */
void FLuaContext::Cleanup(bool bFullCleanup, UWorld* World)
{
    if (!bEnable)
    {
        return;
    }

    if (L)
    {
        FUnLuaDelegates::OnPreLuaContextCleanup.Broadcast(bFullCleanup);

        if (!bFullCleanup)
        {
            // force full lua gc
            // 强制Lua全量GC
            lua_gc(L, LUA_GCCOLLECT, 0);
            lua_gc(L, LUA_GCCOLLECT, 0);

            //!!!Fix!!!
            // do some check work here
        }
        else
        {
            bEnable = false;

            // close lua state first
            // 先关闭Lua虚拟机
            lua_close(L);
            L = nullptr;

            // clean ue side modules,es static data structes
            FCollisionHelper::Cleanup();                        // clean up collision helper stuff

            GObjectReferencer.Cleanup();                        // clean up object referencer

            //!!!Fix!!!
            //thread need refine
            CleanupThreads();                                   // lua thread

            LibraryNames.Empty();                               // metatables and lua module
            ModuleNames.Empty();

            FDelegateHelper::Cleanup(bFullCleanup);                 // clean up delegates

            Manager->Cleanup(NULL, bFullCleanup);                  // clean up UnLuaManager

            GPropertyCreator.Cleanup();                             // clean up dynamically created UProperties

            GReflectionRegistry.Cleanup();                      // clean up reflection registry

            GameInstances.Empty();
            CandidateInputComponents.Empty();
            FCoreUObjectDelegates::GetPostGarbageCollect().Remove(OnPostGarbageCollectHandle);
            FWorldDelegates::OnWorldTickStart.Remove(OnWorldTickStartHandle);

            // old manager
            if (Manager)
            {
                Manager->RemoveFromRoot();
                Manager = nullptr;
            }

#if WITH_EDITOR
            if (LuaHandle)
            {
                FPlatformProcess::FreeDllHandle(LuaHandle);     // unload Lua dynamic lib
                LuaHandle = nullptr;
            }
#endif

        }

        FUnLuaDelegates::OnPostLuaContextCleanup.Broadcast(bFullCleanup);
    }
}

/**
 * Build-in input event for 'Hotfix'
 */
bool FLuaContext::OnGameViewportInputKey(FKey InKey, FModifierKeysState ModifierKeyState, EInputEvent EventType)
{
    if (!bEnable)
    {
        return false;
    }
    if (InKey == EKeys::L && ModifierKeyState.IsControlDown() && EventType == IE_Released)
    {
        return HotfixLua();
    }
    return false;
}
