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

#include "LuaArray.h"

class FLuaSet
{
public:
    enum FScriptSetFlag
    {
        // 被其他持有
        OwnedByOther,   // 'Set' is owned by others
        // 被自己持有,在析构函数中需要释放内存
        OwnedBySelf,    // 'Set' is owned by self, it'll be freed in destructor
    };

    FLuaSet(const FScriptSet *InScriptSet, TSharedPtr<UnLua::ITypeInterface> InElementInterface, FScriptSetFlag Flag = OwnedByOther)
        : Set((FScriptSet*)InScriptSet), SetLayout(FScriptSet::GetScriptLayout(InElementInterface->GetSize(), InElementInterface->GetAlignment()))
        , ElementInterface(InElementInterface), Interface(nullptr), ElementCache(nullptr), ScriptSetFlag(Flag)
    {
        // allocate cache for a single element
        // 为单个元素分配缓存
        ElementCache = FMemory::Malloc(ElementInterface->GetSize(), ElementInterface->GetAlignment());
    }

    FLuaSet(const FScriptSet *InScriptSet, TLuaContainerInterface<FLuaSet> *InSetInterface, FScriptSetFlag Flag = OwnedByOther)
        : Set((FScriptSet*)InScriptSet), Interface(InSetInterface), ElementCache(nullptr), ScriptSetFlag(Flag)
    {
        if (Interface)
        {
            ElementInterface = Interface->GetInnerInterface();
            SetLayout = FScriptSet::GetScriptLayout(ElementInterface->GetSize(), ElementInterface->GetAlignment());

            // allocate cache for a single element
            // 为单个元素分配缓存
            ElementCache = FMemory::Malloc(ElementInterface->GetSize(), ElementInterface->GetAlignment());
        }
    }

    ~FLuaSet()
    {
        DetachInterface();

        // 如果是被自己持有,需要释放内存
        if (ScriptSetFlag == OwnedBySelf)
        {
            Clear();
            delete Set;
        }
        FMemory::Free(ElementCache);
    }

    // 分离接口
    void DetachInterface()
    {
        if (Interface)
        {
            Interface->RemoveContainer(this);
            Interface = nullptr;
        }
    }

    // 获取容器指针
    FORCEINLINE void* GetContainerPtr() const { return Set; }

    /**
     * Get the length of the set
     * 获取Set的长度
     *
     * @return - the length of the array
     */
    FORCEINLINE int32 Num() const
    {
        //return SetHelper.Num();
        return Set->Num();
    }

    /**
     * Add an element to the set
     * 新增元素
     *
     * @param Item - the element
     */
    FORCEINLINE void Add(const void *Item)
    {
        //SetHelper.AddElement(Item);
        const UnLua::ITypeInterface *LocalElementInterface = ElementInterface.Get();
        FScriptSetLayout& LocalSetLayoutForCapture = SetLayout;
        Set->Add(Item, SetLayout,
            [LocalElementInterface](const void* Element) { return LocalElementInterface->GetValueTypeHash(Element); },
            [LocalElementInterface](const void* A, const void* B) { return LocalElementInterface->Identical(A, B); },
            [LocalElementInterface, Item, LocalSetLayoutForCapture](void* NewElement)
            {
                LocalElementInterface->Initialize(NewElement);
                LocalElementInterface->Copy(NewElement, Item);
            },
            [LocalElementInterface](void* Element)
            {
                if (!LocalElementInterface->IsPODType() && !LocalElementInterface->IsTriviallyDestructible())
                {
                    LocalElementInterface->Destruct(Element);
                }
            }
        );
    }

    /**
     * Remove an element from the set
     * 移除元素
     *
     * @param Item - the element
     */
    FORCEINLINE bool Remove(const void *Item)
    {
        //return SetHelper.RemoveElement(Item);
        const UnLua::ITypeInterface *LocalElementInterface = ElementInterface.Get();
        int32 FoundIndex = Set->FindIndex(Item, SetLayout,
            [LocalElementInterface](const void* Element) { return LocalElementInterface->GetValueTypeHash(Element); },
            [LocalElementInterface](const void* A, const void* B) { return LocalElementInterface->Identical(A, B); }
        );
        if (FoundIndex != INDEX_NONE)
        {
            DestructItems(FoundIndex, 1);
            Set->RemoveAt(FoundIndex, SetLayout);
            return true;
        }
        return false;
    }

    /**
     * Check if an element is in the set
     * 检查元素是否在Set中
     *
     * @param Item - the element
     */
    FORCEINLINE bool Contains(const void *Item) const
    {
        //return SetHelper.FindElementIndexFromHash(Item) != INDEX_NONE;
        const UnLua::ITypeInterface *LocalElementInterface = ElementInterface.Get();
        return Set->FindIndex(Item, SetLayout,
            [LocalElementInterface](const void* Element) { return LocalElementInterface->GetValueTypeHash(Element); },
            [LocalElementInterface](const void* A, const void* B) { return LocalElementInterface->Identical(A, B); }
        ) != INDEX_NONE;
    }

    /**
     * Empty the set, and reallocate it for the expected number of elements.
     * 清空Set,同时重新分配给定大小个元素
     *
     * @param Slack (Optional) - the expected usage size after empty operation. Default is 0.
     */
    FORCEINLINE void Clear(int32 Slack = 0)
    {
        //SetHelper.EmptyElements(Slack);
        int32 OldNum = Set->Num();
        if (OldNum)
        {
            DestructItems(0, OldNum);
        }
        if (OldNum || Slack)
        {
            Set->Empty(Slack, SetLayout);
        }
    }

    /**
     * Get address of the i'th element
     * 获取索引处元素
     *
     * @param Index - the index
     * @return - the address of the i'th element
     */
    FORCEINLINE uint8* GetData(int32 Index)
    {
        return (uint8*)Set->GetData(Index, SetLayout);
    }

    FORCEINLINE const uint8* GetData(int32 Index) const
    {
        return (uint8*)Set->GetData(Index, SetLayout);
    }

    /**
     * Adds an uninitialized element to the set. The set needs rehashing to make it valid.
     * 新增一个未初始化的元素,Set需要重新Hash来确保有效
     *
     * @return - the index of the added element.
     */
    FORCEINLINE int32 AddUninitializedValue()
    {
        checkSlow(Num() >= 0);
        return Set->AddUninitialized(SetLayout);
    }

    /**
     * Adds a blank, constructed element to the set. The set needs rehashing to make it valid.
     * 新增一个默认值的元素,Map需要重新Hash来确保有效
     *
     * @return - the index of the first element added.
     **/
    FORCEINLINE int32 AddDefaultValue_Invalid_NeedsRehash()
    {
        checkSlow(Num() >= 0);
        int32 Result = AddUninitializedValue();
        ConstructItem(Result);
        return Result;
    }

    /**
     * Rehash the keys in the set. this function must be called to create a valid set.
     * 对Set进行重新Hash,在创建一个有效的Set的时候必须调用这个方法
     */
    FORCEINLINE void Rehash()
    {
        Set->Rehash(SetLayout, [=](const void* Src) { return ElementInterface->GetValueTypeHash(Src); });
    }

    /**
     * Convert this set to an array
     * 转换Set到Array
     *
     * @param OutArray - the result array
     */
    FORCEINLINE FLuaArray* ToArray(void *OutArray) const
    {
        if (ElementInterface && OutArray)
        {
            FScriptArray *ScriptArray = new FScriptArray;
            //ArrayProperty->InitializeValue(ScriptArray);        // do nothing...
            FLuaArray *LuaArray = new(OutArray) FLuaArray(ScriptArray, ElementInterface, FLuaArray::OwnedBySelf);
            int32 i = -1, Size = Set->Num();
            while (Size > 0)
            {
                if (IsValidIndex(++i))
                {
                    LuaArray->Add(Set->GetData(i, SetLayout));
                    --Size;
                }
            }
            return LuaArray;
        }
        return nullptr;
    }

    FScriptSet *Set;
    FScriptSetLayout SetLayout;
    TSharedPtr<UnLua::ITypeInterface> ElementInterface;
    TLuaContainerInterface<FLuaSet> *Interface;
    //FScriptSetHelper SetHelper;
    void *ElementCache;            // can only hold one element...
    FScriptSetFlag ScriptSetFlag;

private:
    // 从Index处开始销毁Count个元素
    void DestructItems(int32 Index, int32 Count)
    {
        check(Index >= 0 && Count >= 0);

        if (Count == 0)
        {
            return;
        }

        bool bDestroyElements = !ElementInterface->IsPODType() && !ElementInterface->IsTriviallyDestructible();
        if (bDestroyElements)
        {
            uint32 Stride = SetLayout.Size;
            uint8* ElementPtr = (uint8*)Set->GetData(Index, SetLayout);
            for (; Count; ++Index)
            {
                if (IsValidIndex(Index))
                {
                    ElementInterface->Destruct(ElementPtr);
                    --Count;
                }
                ElementPtr += Stride;
            }
        }
    }

    // 检查索引处有效性
    FORCEINLINE bool IsValidIndex(int32 Index) const
    {
        return Set->IsValidIndex(Index);
    }

    // 销毁索引处元素
    FORCEINLINE void ConstructItem(int32 Index)
    {
        check(IsValidIndex(Index));
        uint8* Dest = (uint8*)Set->GetData(Index, SetLayout);
        ElementInterface->Initialize(Dest);
    }
};
