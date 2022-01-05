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

#include "UnLuaPrivate.h"
#include "UnLuaCompatibility.h"
#include "lua.hpp"

struct FScriptContainerDesc
{
    FORCEINLINE int32 GetSize() const { return Size; }
    FORCEINLINE const char* GetName() const { return Name; }

    static const FScriptContainerDesc Array;
    static const FScriptContainerDesc Set;
    static const FScriptContainerDesc Map;

private:
    FORCEINLINE FScriptContainerDesc(int32 InSize, const char *InName)
        : Size(InSize), Name(InName)
    {}

    int32 Size;
    const char *Name;
};

// 从相对路径获取Lua文件完整路径
FString GetFullPathFromRelativePath(const FString& RelativePath);
// 创建命名'UE'空间(一个Lua表)
void CreateNamespaceForUE(lua_State *L);
// 为栈顶的Lua表设置名称
void SetTableForClass(lua_State *L, const char *Name);

/**
 * Set metatable for the userdata/table on the top of the stack
 * 为栈顶的userdata/table设置元表
 */
bool TryToSetMetatable(lua_State *L, const char *MetatableName, UObject* Object = nullptr);
// 获取元表的名字
FString GetMetatableName(const UObjectBaseUtility* Object);

/**
 * Functions to handle Lua userdata
 * 新建一个二次指针的userdata
 */
void* NewUserdataWithTwoLvPtrTag(lua_State* L, int Size, void* Object);
// 对userdata新增二次指针标记
void MarkUserdataTwoLvPtrTag(void* Userdata);
// 计算userdata填充
uint8 CalcUserdataPadding(int32 Alignment);
template <typename T> uint8 CalcUserdataPadding() { return CalcUserdataPadding(alignof(T)); }
// 获取userdata
UNLUA_API void* GetUserdata(lua_State *L, int32 Index, bool *OutTwoLvlPtr = nullptr, bool *OutClassMetatable = nullptr);
// 快速获取userdata
UNLUA_API void* GetUserdataFast(lua_State *L, int32 Index, bool *OutTwoLvlPtr = nullptr);
// 新建内存填充的userdata
UNLUA_API void* NewUserdataWithPadding(lua_State *L, int32 Size, const char *MetatableName, uint8 PaddingSize = 0);
#define NewTypedUserdata(L, Type) NewUserdataWithPadding(L, sizeof(Type), #Type, CalcUserdataPadding<Type>())
// 获取C++实例
UNLUA_API void* GetCppInstance(lua_State *L, int32 Index);
// 快速获取C++实例
UNLUA_API void* GetCppInstanceFast(lua_State *L, int32 Index);

/**
 * Functions to handle script containers
 * 新建Script容器
 */
void* NewScriptContainer(lua_State *L, const FScriptContainerDesc &Desc);
// 缓存Script容器
void* CacheScriptContainer(lua_State *L, void *Key, const FScriptContainerDesc &Desc);
// 获取Script容器
void* GetScriptContainer(lua_State *L, int32 Index);
// 移除缓存Script容器
void RemoveCachedScriptContainer(lua_State *L, void *Key);

/**
 * Functions to push FProperty array
 */
void PushIntegerArray(lua_State *L, FNumericProperty *Property, void *Value);
void PushFloatArray(lua_State *L, FNumericProperty *Property, void *Value);
void PushEnumArray(lua_State *L, FNumericProperty *Property, void *Value);
void PushFNameArray(lua_State *L, FNameProperty *Property, void *Value);
void PushFStringArray(lua_State *L, FStrProperty *Property, void *Value);
void PushFTextArray(lua_State *L, FTextProperty *Property, void *Value);
void PushObjectArray(lua_State *L, FObjectPropertyBase *Property, void *Value);
void PushInterfaceArray(lua_State *L, FInterfaceProperty *Property, void *Value);
void PushDelegateArray(lua_State *L, FDelegateProperty *Property, void *Value);
void PushMCDelegateArray(lua_State *L, FMulticastDelegateProperty *Property, void *Value, const char *MetatableName);
void PushStructArray(lua_State *L, FProperty *Property, void *Value, const char *MetatableName);

/**
 * Push a UObject to Lua stack
 * Push一个UObject到Lua栈
 */
void PushObjectCore(lua_State *L, UObjectBaseUtility *Object);

/**
 * Functions to New/Delete Lua instance for UObjectBaseUtility
 * 新建/释放对应UObjectBaseUtility的Lua实例的方法
 */
int32 NewLuaObject(lua_State *L, UObjectBaseUtility *Object, UClass *Class, const char *ModuleName);
void DeleteLuaObject(lua_State *L, UObjectBaseUtility *Object);
void DeleteUObjectRefs(lua_State* L, UObjectBaseUtility* Object);

/**
 * Get UObject and Lua function address for delegate
 * 获取代理信息
 */
int32 GetDelegateInfo(lua_State *L, int32 Index, UObject* &Object, const void* &Function);

/**
 * Functions to handle Lua functions
 * Lua方法
 */
bool GetFunctionList(lua_State *L, const char *InModuleName, TSet<FName> &FunctionNames);
int32 PushFunction(lua_State *L, UObjectBaseUtility *Object, const char *FunctionName);
bool PushFunction(lua_State *L, UObjectBaseUtility *Object, int32 FunctionRef);
bool CallFunction(lua_State *L, int32 NumArgs, int32 NumResults);

/**
 * Get corresponding Lua instance for a UObjectBaseUtility
 * 获取UObjectBaseUtility对应的Lua实例
 */
UNLUA_API bool GetObjectMapping(lua_State *L, UObjectBaseUtility *Object);

/**
 * Add Lua package path
 * 新增Lua包路径
 */
UNLUA_API void AddPackagePath(lua_State *L, const char *Path);
int LoadFromBuiltinLibs(lua_State *L);
int LoadFromCustomLoader(lua_State *L);
int LoadFromFileSystem(lua_State *L);

/**
 * Functions to handle loaded Lua module
 * Lua模块
 */
// 清理加载的模块
void ClearLoadedModule(lua_State *L, const char *ModuleName);
// 获取加载的模块
int32 GetLoadedModule(lua_State *L, const char *ModuleName);

/**
 * Functions to register collision enums
 * 碰撞Enum
 */
bool RegisterECollisionChannel(lua_State *L);
bool RegisterEObjectTypeQuery(lua_State *L);
bool RegisterETraceTypeQuery(lua_State *L);

// 清理库
void ClearLibrary(lua_State *L, const char *LibrayName);

/**
 * Functions to create weak table
 * 弱表
 */
// 创建弱表(k)
void CreateWeakKeyTable(lua_State *L);
// 创建弱表(v)
void CreateWeakValueTable(lua_State *L);

// 遍历表
int32 TraverseTable(lua_State *L, int32 Index, void *Userdata, bool (*TraverseWorker)(lua_State*, void*));
// 获取表头元素
bool PeekTableElement(lua_State *L, void *Userdata);

/**
 * Functions to register UEnum
 * 注册UEnum
 */
int32 Global_RegisterEnum(lua_State *L);
bool RegisterEnum(lua_State *L, const char *EnumName);
bool RegisterEnum(lua_State *L, UEnum *Enum);

/**
 * Functions to register UClass
 * 注册UClass
 */
int32 Global_UnRegisterClass(lua_State* L);
int32 Global_RegisterClass(lua_State *L);
class FClassDesc* RegisterClass(lua_State *L, const char *ClassName, const char *SuperClassName = nullptr);
class FClassDesc* RegisterClass(lua_State *L, UStruct *Struct, UStruct *SuperStruct = nullptr);

/**
 * Lua global functions
 * Lua全局方法
 */
int32 Global_GetUProperty(lua_State *L);
int32 Global_SetUProperty(lua_State *L);
int32 Global_LoadObject(lua_State *L);
int32 Global_LoadClass(lua_State *L);
int32 Global_NewObject(lua_State *L);
UNLUA_API int32 Global_Print(lua_State *L);
UNLUA_API int32 Global_Require(lua_State *L);
int32 Global_AddToClassWhiteSet(lua_State* L);
int32 Global_RemoveFromClassWhiteSet(lua_State* L);

/**
 * Functions to handle UEnum
 */
int32 Enum_Index(lua_State *L);
int32 Enum_Delete(lua_State *L);
int32 Enum_GetMaxValue(lua_State* L);
int32 Enum_GetNameByValue(lua_State* L);

/**
 * Functions to handle UClass
 */
int32 Class_Index(lua_State* L);
int32 Class_NewIndex(lua_State* L);
int32 Class_CallUFunction(lua_State *L);
int32 Class_CallLatentFunction(lua_State *L);
int32 Class_StaticClass(lua_State *L);
int32 Class_Cast(lua_State* L);

/**
 * Functions to handle UScriptStruct
 */
int32 ScriptStruct_New(lua_State *L);
int32 ScriptStruct_Delete(lua_State *L);
int32 ScriptStruct_Copy(lua_State *L);
int32 ScriptStruct_CopyFrom(lua_State *L);
int32 ScriptStruct_Compare(lua_State *L);

/**
 * Create a type interface
 * 创建一个类型接口
 */
TSharedPtr<UnLua::ITypeInterface> CreateTypeInterface(lua_State *L, int32 Index);