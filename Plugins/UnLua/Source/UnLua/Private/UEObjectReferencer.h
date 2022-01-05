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

#include "Containers/Set.h"
#include "UObject/GCObject.h"

/**
* UE4使用GC管理UObject，lua也有GC管理l对象，因此UE和lua进行数据传递时，UnLua需要准确的加引用和减引用操作
* 这样，UE在GC时知道哪些UObject正在被lua runtime使用，相应的，lua在gc时也能知道哪些对象正在被UE4 runtime使用，从而进行正确的GC行为
* 但是也要记住，UE可以显式调用Destroy()销毁Actor，也可以使用MarkPendingKill()来销毁UObject，即使还有引用存在
*/

/**
* UE使用GC管理UObject，当UObject被传递到lua后，lua会对其添加引用，这样UE的GC就能看到这个引用了
*/

/**
* UnLua使用全局的GObjectReferencer对象管理引用添加和删除，它继承了FGCObject，并且实现了AddReferencedObjects接口，每当UE进行标记时，都会调用到该方法
*/

/**
* GObjectReferencer内部维护了ReferencedObjects容器，记录当前在lua中被使用的UObject，每当AddReferencedObjects被调用时都会对这些UObject添加引用
*/

class FObjectReferencer : public FGCObject
{
public:
    static FObjectReferencer& Instance()
    {
        static FObjectReferencer Referencer;
        return Referencer;
    }

    void AddObjectRef(UObject *Object)
    {
        ReferencedObjects.Add(Object);
    }

    void RemoveObjectRef(UObject *Object)
    {
        ReferencedObjects.Remove(Object);
    }

    void Cleanup()
    {
        return ReferencedObjects.Empty();
    }

#if UE_BUILD_DEBUG
    void Debug()
    {
        check(true);
    }
#endif

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObjects(ReferencedObjects);
    }

    virtual FString GetReferencerName() const
    {
        return "UnLua_GObjectReferencer";
    }

private:
    FObjectReferencer() {}

    TSet<UObject*> ReferencedObjects;
};

#define GObjectReferencer FObjectReferencer::Instance()
