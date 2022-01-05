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

#include "UnLuaBase.h"
#include "UnLuaCompatibility.h"

/**
 * new FProperty types
 * 新增属性类型
 */
enum
{
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 22)
    CPT_MulticastSparseDelegate = CPT_Unused_Index_19,
#endif
    CPT_Enum = CPT_Unused_Index_21,
    CPT_Array = CPT_Unused_Index_22,
};

struct lua_State;

/**
 * Property descriptor
 * 属性描述
 */
class FPropertyDesc : public UnLua::ITypeInterface
{
public:
	// 新增
    static FPropertyDesc* Create(FProperty *InProperty);

	virtual ~FPropertyDesc();

    /**
     * Check the validity of this property
     * 检查属性的有效性
     *
     * @return - true if the property is valid, false otherwise
     */
    bool IsValid() const;

    /**
     * Test if this property is a const reference parameter.
     * 是否是const引用参数
     *
     * @return - true if the property is a const reference parameter, false otherwise
     */
    FORCEINLINE bool IsConstOutParameter() const { return Property->HasAllPropertyFlags(CPF_OutParm | CPF_ConstParm); }

    /**
     * Test if this property is a non-const reference parameter.
     * 是否是非const引用参数
     *
     * @return - true if the property is a non-const reference parameter, false otherwise
     */
    FORCEINLINE bool IsNonConstOutParameter() const { return Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm); }

    /**
     * Test if this property is an out parameter. out parameter means return parameter or non-const reference parameter
     * 是否是Out参数,Out参数意味着返回值或者非const引用
     *
     * @return - true if the property is an out parameter, false otherwise
     */
    FORCEINLINE bool IsOutParameter() const { return Property->HasAnyPropertyFlags(CPF_ReturnParm) || (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm)); }

    /**
     * Test if this property is the return parameter
     * 是否是返回值
     *
     * @return - true if the property is the return parameter, false otherwise
     */
    FORCEINLINE bool IsReturnParameter() const { return Property->HasAnyPropertyFlags(CPF_ReturnParm); }

    /**
     * Test if this property is the reference parameter
     * 是否是引用参数
     *
     * @return - true if the property is the return parameter, false otherwise
     */
    FORCEINLINE bool IsReferenceParameter() const { return Property->HasAnyPropertyFlags(CPF_ReferenceParm); }

    /**
     * Get the 'true' property
     * 获取真正的属性
     *
     * @return - the FProperty
     */
    FORCEINLINE FProperty* GetProperty() const { return Property; }

    /**
     * @see FProperty::InitializeValue_InContainer(...)
     */
    FORCEINLINE void InitializeValue(void *ContainerPtr) const { Property->InitializeValue_InContainer(ContainerPtr); }

    /**
     * @see FProperty::DestroyValue_InContainer(...)
     */
    FORCEINLINE void DestroyValue(void *ContainerPtr) const { Property->DestroyValue_InContainer(ContainerPtr); }

    /**
     * @see FProperty::CopySingleValue(...)
     */
    FORCEINLINE void CopyValue(void *ContainerPtr, const void *Src) const { Property->CopySingleValue(Property->ContainerPtrToValuePtr<void>(ContainerPtr), Src); }

    /**
     * Get the value of this property
     * 获取属性的值
     *
     * @param ContainerPtr - the address of the container for this property
     * @param bCreateCopy (Optional) - whether to create a copy for the value
     */
    FORCEINLINE void GetValue(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const 
    {   
        GetValueInternal(L, Property->ContainerPtrToValuePtr<void>(ContainerPtr), bCreateCopy);
    }

    /**
     * Set the value of this property as an element at the given Lua index
     * 在给定Lua栈位置设置属性的值
     *
     * @param ContainerPtr - the address of the container for this property
     * @param IndexInStack - Lua index
     * @param bCopyValue - whether to create a copy for the value
     * @return - true if 'ContainerPtr' should be cleaned up by 'DestroyValue_InContainer', false otherwise
     */
    FORCEINLINE bool SetValue(lua_State *L, void *ContainerPtr, int32 IndexInStack = -1, bool bCopyValue = true) const
    {
        return SetValueInternal(L, Property->ContainerPtrToValuePtr<void>(ContainerPtr), IndexInStack, bCopyValue);
    }

	// lua获取属性值的接口，根据属性类型使用不同的push方式
	// Integer等基本类型会直接push值，而像UObject类型会push一个UserData
    virtual void GetValueInternal(lua_State *L, const void *ValuePtr, bool bCreateCopy) const = 0;
	// lua中给属性赋值接口，从lua栈中取出lua中设置的值，给属性设置上，因此自然也要根据不同类型区分
    virtual bool SetValueInternal(lua_State *L, void *ValuePtr, int32 IndexInStack = -1, bool bCopyValue = true) const = 0;

    /**
     * Copy an element at the given Lua index to the value of this property
     * 在给定Lua栈位置复写元素
     *
     * @param SrcIndexInStack - source Lua index
     * @param DestContainerPtr - the destination address of the container for this property
     * @return - true if the operation succeed, false otherwise
     */
    virtual bool CopyBack(lua_State *L, int32 SrcIndexInStack, void *DestContainerPtr) { return false; }

    /**
     * Copy the value of this property to an element at the given Lua index
     * 在给定Lua栈位置复写属性的值
     *
     * @param SrcContainerPtr - the source address of the container for this property
     * @param DestIndexInStack - destination Lua index
     * @return - true if the operation succeed, false otherwise
     */
    virtual bool CopyBack(lua_State *L, void *SrcContainerPtr, int32 DestIndexInStack) { return false; }

    virtual bool CopyBack(void *Dest, const void *Src) { return false; }

    // interfaces of UnLua::ITypeInterface
	/**
	 * 如果一个类或结构是平凡的，具有标准布局的，且不包含任何非POD的非静态成员，那么它就被认定是POD。平凡的类或结构定义如下：
	 * 1.具有一个平凡的缺省构造器。（可以使用缺省构造器语法，如 SomeConstructor() = default;)
	 * 2.具有一个平凡的拷贝构造器。（可以使用缺省构造器语法)
	 * 3.具有一个平凡的拷贝赋值运算符。（可以使用缺省语法)
	 * 4.具有一个非虚且平凡的析构器。
	 * 一个具有标准布局的类或结构被定义如下：
	 * 1.所有非静态数据成员均为标准布局类型。
	 * 2.所有非静态成员的访问权限(public, private, protected) 均相同。
	 * 3.没有虚函数。
	 * 4.没有虚基类。
	 * 5.所有基类均为标准布局类型。
	 * 6.没有任何基类的类型与类中第一个非静态成员相同。
	 * 7.要么全部基类都没有非静态数据成员，要么最下层的子类没有非静态数据成员且最多只有一个基类有非静态数据成员。总之继承树中最多只能有一个类有非静态数据成员。所有非静态数据成员必须都是标准布局类型。
	 */
    virtual bool IsPODType() const override { return (Property->PropertyFlags & CPF_IsPlainOldData) != 0; }
    // 检查T是否是普通可破坏类型
    virtual bool IsTriviallyDestructible() const override { return (Property->PropertyFlags & CPF_NoDestructor) != 0; }
	// 获取偏移
    virtual int32 GetOffset() const override { return Property->GetOffset_ForInternal(); }
	// 获取大小
    virtual int32 GetSize() const override { return Property->GetSize(); }
	// 获取内存对齐
    virtual int32 GetAlignment() const override { return Property->GetMinAlignment(); }
	// 获取类型哈希 
    virtual uint32 GetValueTypeHash(const void *Src) const override { return Property->GetValueTypeHash(Src); }
	// 初始化
    virtual void Initialize(void *Dest) const override { Property->InitializeValue(Dest); }
	// 释放
    virtual void Destruct(void *Dest) const override { Property->DestroyValue(Dest); }
	// 拷贝
    virtual void Copy(void *Dest, const void *Src) const override { Property->CopySingleValue(Dest, Src); }
	// 判等
    virtual bool Identical(const void *A, const void *B) const override { return Property->Identical(A, B); }
	// 获取名字
    virtual FString GetName() const override { return TEXT(""); }
    virtual FProperty* GetUProperty() const override { return Property; }

	// 读
    virtual void Read(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const override 
    { 
        GetValueInternal(L, Property->ContainerPtrToValuePtr<void>(ContainerPtr), bCreateCopy);
    }
	// 写
    virtual void Write(lua_State *L, void *ContainerPtr, int32 IndexInStack) const override 
    { 
        SetValueInternal(L, Property->ContainerPtrToValuePtr<void>(ContainerPtr), IndexInStack, true); 
    }

#if ENABLE_TYPE_CHECK == 1
    virtual bool CheckPropertyType(lua_State* L, int32 IndexInStack, FString& ErrorMsg, void* UserData = nullptr) { return true; };
#endif

	// 设置属性类型
    void SetPropertyType(int8 Type);
	// 获取属性类型
    int8 GetPropertyType();
	
protected:
    explicit FPropertyDesc(FProperty *InProperty);

    union
    {
        FProperty *Property;
        FNumericProperty *NumericProperty;
        FEnumProperty *EnumProperty;
        FBoolProperty *BoolProperty;
        FObjectPropertyBase *ObjectBaseProperty;
        FSoftObjectProperty *SoftObjectProperty;
        FInterfaceProperty *InterfaceProperty;
        FNameProperty *NameProperty;
        FStrProperty *StringProperty;
        FTextProperty *TextProperty;
        FArrayProperty *ArrayProperty;
        FMapProperty *MapProperty;
        FSetProperty *SetProperty;
        FStructProperty *StructProperty;
        FDelegateProperty *DelegateProperty;
        FMulticastDelegateProperty *MulticastDelegateProperty;
    };

    int8 PropertyType;
public:
    static TMap<FProperty*,FPropertyDesc*> Property2Desc;
};

UNLUA_API int32 GetPropertyType(const FProperty *Property);
