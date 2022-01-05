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

#include "UnLuaEx.h"
#include "LuaCore.h"
#include "DelegateHelper.h"

/**
 * Bind a callback for the delegate. Parameters must be a UObject and a Lua function
 */
static int32 FScriptDelegate_Bind(lua_State *L)
{
    // 三个参数，FScriptDelegate，UObject和luafunc，其中FScriptDelegate是TScriptDelegate的别名
    int32 NumParams = lua_gettop(L);
    if (NumParams != 3)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid parameters!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    FScriptDelegate *Delegate = (FScriptDelegate*)GetCppInstanceFast(L, 1);
    if (!Delegate)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid dynamic delegate!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    UObject *Object = UnLua::GetUObject(L, 2);
    if (!Object)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid object!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    const void *CallbackFunction = lua_topointer(L, 3);
    if (!CallbackFunction)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid function!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    // FCallbackDesc可以看作一个回调函数的存根，可以用UClass和lua回调函数唯一标识一个回调函数
    FCallbackDesc Callback(Object->GetClass(), CallbackFunction, Object);
    // 尝试根据Callback获取对应的UFunction函数名，如果没有对应的UFunction就返回NAME_None
    FName FuncName = FDelegateHelper::GetBindedFunctionName(Callback);
    if (FuncName == NAME_None)
    {
        lua_pushvalue(L, 3);
        // 先对luafunc添加引用，这会对函数中的upvalue也添加引用，需要注意
        int32 CallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
        FDelegateHelper::Bind(Delegate, Object, Callback, CallbackRef);
    }
    else
    {
        Delegate->BindUFunction(Object, FuncName);
    }
    return 0;
}

/**
 * Unbind the callback for the delegate
 * 做一些清理工作，比如各种map容器，创建的FCallbackDesc
 */
static int32 FScriptDelegate_Unbind(lua_State *L)
{
    int32 NumParams = lua_gettop(L);
    if (NumParams != 1)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid parameters!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    FScriptDelegate *Delegate = (FScriptDelegate*)GetCppInstanceFast(L, 1);
    if (Delegate)
    {
        FDelegateHelper::Unbind(Delegate);
    }
    else
    {
        UObject *Object = nullptr;
        const void *CallbackFunction = nullptr;
        int32 FuncIdx = GetDelegateInfo(L, 1, Object, CallbackFunction);     // get target UObject and Lua function
        if (FuncIdx != INDEX_NONE)
        {
            FDelegateHelper::Unbind(FCallbackDesc(Object->GetClass(), CallbackFunction, Object));
        }
    }

    return 0;
}

/**
 * Call the callback bound to the delegate
 */
static int32 FScriptDelegate_Execute(lua_State *L)
{
    int32 NumParams = lua_gettop(L);
    if (NumParams < 1)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid parameters!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    FScriptDelegate *Delegate = (FScriptDelegate*)GetCppInstanceFast(L, 1);
    if (!Delegate)
    {
        UNLUA_LOGERROR(L, LogUnLua, Error, TEXT("%s: Invalid dynamic delegate!"), ANSI_TO_TCHAR(__FUNCTION__));
        return 0;
    }

    int32 NumReturnValues = FDelegateHelper::Execute(L, Delegate, NumParams - 1, 2);
    return NumReturnValues;
}

/**
 * 单播Delegate的传递由FDelegatePropertyDesc类完成
 * 传递到lua中也由UserData表示，类型为FScriptDelegate
 */
static const luaL_Reg FScriptDelegateLib[] =
{
    { "Bind", FScriptDelegate_Bind },
    { "Unbind", FScriptDelegate_Unbind },
    { "Execute", FScriptDelegate_Execute },
    { nullptr, nullptr }
};

EXPORT_UNTYPED_CLASS(FScriptDelegate, false, FScriptDelegateLib)
IMPLEMENT_EXPORTED_CLASS(FScriptDelegate)
