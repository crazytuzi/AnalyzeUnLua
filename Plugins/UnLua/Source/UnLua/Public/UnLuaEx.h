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

#include "UnLua.h"

namespace UnLua
{

    /**
     * Exported constructor
     * 导出构造函数
     */
    template <typename ClassType, typename... ArgType>
    struct TConstructor : public IExportedFunction
    {
        explicit TConstructor(const FString &InClassName);

        virtual void Register(lua_State *L) override;
        virtual int32 Invoke(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return TEXT(""); }
        virtual void GenerateIntelliSense(FString &Buffer) const override {}
#endif

    private:
        template <uint32... N>
        void Construct(lua_State *L, TTuple<typename TArgTypeTraits<ArgType>::Type...> &Args, TIndices<N...>);

        FString ClassName;
    };

    /**
     * Exported smart pointer constructor
     * 导出智能指针构造函数
     */
    template <typename SmartPtrType, typename ClassType, typename... ArgType>
    struct TSmartPtrConstructor : public IExportedFunction
    {
        explicit TSmartPtrConstructor(const FString &InFuncName);

        virtual void Register(lua_State *L) override;
        virtual int32 Invoke(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return TEXT(""); }
        virtual void GenerateIntelliSense(FString &Buffer) const override {}
#endif

        // GC
        static int32 GarbageCollect(lua_State *L);

    protected:
        template <uint32... N>
        void Construct(lua_State *L, TTuple<typename TArgTypeTraits<ArgType>::Type...> &Args, TIndices<N...>);

        FString FuncName;
    };

    template <ESPMode Mode, typename ClassType, typename... ArgType>
    struct TSharedPtrConstructor : public TSmartPtrConstructor<TSharedPtr<ClassType, Mode>, ClassType, ArgType...>
    {
        TSharedPtrConstructor()
            : TSmartPtrConstructor<TSharedPtr<ClassType, Mode>, ClassType, ArgType...>(Mode == ESPMode::NotThreadSafe ? TEXT("SharedPtr") : TEXT("ThreadsafeSharedPtr"))
        {}
    };

    template <ESPMode Mode, typename ClassType, typename... ArgType>
    struct TSharedRefConstructor : public TSmartPtrConstructor<TSharedRef<ClassType, Mode>, ClassType, ArgType...>
    {
        TSharedRefConstructor()
            : TSmartPtrConstructor<TSharedRef<ClassType, Mode>, ClassType, ArgType...>(Mode == ESPMode::NotThreadSafe ? TEXT("SharedRef") : TEXT("ThreadsafeSharedRef"))
        {}
    };

    /**
     * Exported destructor
     * 导出析构函数
     */
    template <typename ClassType>
    struct TDestructor : public IExportedFunction
    {
        virtual void Register(lua_State *L) override;
        virtual int32 Invoke(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return TEXT(""); }
        virtual void GenerateIntelliSense(FString &Buffer) const override {}
#endif
    };

    /**
     * Exported glue function
     * 导出Lua方法
     */
    struct FGlueFunction : public IExportedFunction
    {
        FGlueFunction(const FString &InName, lua_CFunction InFunc)
            : Name(InName), Func(InFunc)
        {}

        virtual void Register(lua_State *L) override
        {
            // 把classmetatable.funcname设置为函数地址。至于参数解析，返回值压栈等，都需要被导出的函数自己处理，毕竟只是导出了原生C模块
            // make sure the meta table is on the top of the stack
            // 确保元表在栈顶
            lua_pushstring(L, TCHAR_TO_UTF8(*Name));
            lua_pushcfunction(L, Func);
            lua_rawset(L, -3);
        }

        virtual int32 Invoke(lua_State *L) override { return 0; }

#if WITH_EDITOR
        virtual FString GetName() const override { return Name; }
        virtual void GenerateIntelliSense(FString &Buffer) const override {}
#endif

    private:
        FString Name;
        lua_CFunction Func;
    };

    /**
     * Exported global function
     * 导出全局方法
     */
    template <typename RetType, typename... ArgType>
    struct TExportedFunction : public IExportedFunction
    {
        TExportedFunction(const FString &InName, RetType(*InFunc)(ArgType...));

        virtual void Register(lua_State *L) override;
        virtual int32 Invoke(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return Name; }
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    protected:
        FString Name;
        TFunction<RetType(ArgType...)> Func;
    };

    /**
     * Exported member function
     * 导出成员方法
     */
    template <typename ClassType, typename RetType, typename... ArgType>
    struct TExportedMemberFunction : public IExportedFunction
    {
        // 包含了一个TFunction成员Func，构造函数中会创建一个lambda函数来初始化Func，之后调用Func时就会跳转到我们导出的函数了
        // lambda函数的参数包括对象和参数两部分，因为C++中调用成员函数总会传递一个隐含的this参数，这里保持了一致
        TExportedMemberFunction(const FString &InName, RetType(ClassType::*InFunc)(ArgType...), const FString &InClassName);
        TExportedMemberFunction(const FString &InName, RetType(ClassType::*InFunc)(ArgType...) const, const FString &InClassName);

        virtual void Register(lua_State *L) override;
        virtual int32 Invoke(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return Name; }
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    private:
        FString Name;
        TFunction<RetType(ClassType*, ArgType...)> Func;

#if WITH_EDITOR
        FString ClassName;
#endif
    };

    /**
     * Exported static member function
     * 导出静态成员方法
     */
    template <typename RetType, typename... ArgType>
    struct TExportedStaticMemberFunction : public TExportedFunction<RetType, ArgType...>
    {
        typedef TExportedFunction<RetType, ArgType...> Super;

        TExportedStaticMemberFunction(const FString &InName, RetType(*InFunc)(ArgType...), const FString &InClassName);

        virtual void Register(lua_State *L) override;

#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    private:
#if WITH_EDITOR
        FString ClassName;
#endif
    };


    /**
     * Exported property
     * 实现了注册属性到lua
     */
    struct FExportedProperty : public IExportedProperty
    {
        // 把FExportedProperty自身作为lightuserdata传递到lua中，然后设置classmetatable.propertyname = userdata
        virtual void Register(lua_State *L) override
        {
            // make sure the meta table is on the top of the stack
            lua_pushstring(L, TCHAR_TO_UTF8(*Name));
            lua_pushlightuserdata(L, this);
            lua_rawset(L, -3);
        }

#if WITH_EDITOR
        virtual FString GetName() const override { return Name; }
#endif

    protected:
        FExportedProperty(const FString &InName, uint32 InOffset)
            : Name(InName), Offset(InOffset)
        {}

        virtual ~FExportedProperty() {}

        FString Name;
        uint32 Offset;
    };

    struct FExportedBitFieldBoolProperty : public FExportedProperty
    {
        FExportedBitFieldBoolProperty(const FString &InName, uint32 InOffset, uint8 InMask)
            : FExportedProperty(InName, InOffset), Mask(InMask)
        {}

        virtual void Read(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const override
        {
            bool V = !!(*((uint8*)ContainerPtr + Offset) & Mask);
            UnLua::Push(L, V);
        }

        virtual void Write(lua_State *L, void *ContainerPtr, int32 IndexInStack) const override
        {
            bool V = UnLua::Get(L, IndexInStack, TType<bool>());
            uint8 *ValuePtr = (uint8*)ContainerPtr + Offset;
            *ValuePtr = ((*ValuePtr) & ~Mask) | (V ? Mask : 0);
        }

#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override
        {
            Buffer += FString::Printf(TEXT("---@field public %s boolean \r\n"), *Name);
        }
#endif

    private:
        uint8 Mask;
    };

    // 实现了属性的读写
    // 用于实现静态导出属性到lua，其基类FExportedProperty负责注册属性
    template <typename T>
    struct TExportedProperty : public FExportedProperty
    {
        TExportedProperty(const FString &InName, uint32 InOffset);

        virtual void Read(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const override;
        virtual void Write(lua_State *L, void *ContainerPtr, int32 IndexInStack) const override;

#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif
    };

    // 静态属性
    template <typename T>
    struct TExportedStaticProperty : public FExportedProperty
    {
    public:
        TExportedStaticProperty(const FString &InName, T* Value);

        virtual void Register(lua_State *L) override
        {
            // make sure the meta table is on the top of the stack
            // 确保元表在栈顶
            lua_pushstring(L, TCHAR_TO_UTF8(*Name));
            UnLua::Push<T>(L, Value);
            lua_rawset(L, -3);
        }

        virtual void Read(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const override;
        virtual void Write(lua_State *L, void *ContainerPtr, int32 IndexInStack) const override;
        
#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    private:
        T* Value;
    };
    
    // 导出Array元素属性
    template <typename T>
    struct TExportedArrayProperty : public FExportedProperty
    {
        TExportedArrayProperty(const FString &InName, uint32 InOffset, int32 InArrayDim);

        virtual void Read(lua_State *L, const void *ContainerPtr, bool bCreateCopy) const override;
        virtual void Write(lua_State *L, void *ContainerPtr, int32 IndexInStack) const override;

#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    protected:
        int32 ArrayDim;
    };


    /**
     * Exported class
     * 实现了IExportedClass的接口，属于基本静态导出类，实现可注册类和导出lua原生C模块功能
     */
    template <bool bIsReflected>
    struct TExportedClassBase : public IExportedClass
    {
        TExportedClassBase(const char *InName, const char *InSuperClassName = nullptr);
        virtual ~TExportedClassBase();

        // 注册自己和相关函数
        virtual void Register(lua_State *L) override;
        // 添加lua原生C模块
        virtual void AddLib(const luaL_Reg *InLib) override;
        virtual bool IsReflected() const override { return bIsReflected; }
        virtual FName GetName() const override { return ClassFName; }

#if WITH_EDITOR
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

    private:
#if WITH_EDITOR
        void GenerateIntelliSenseInternal(FString &Buffer, FFalse NotReflected) const;
        void GenerateIntelliSenseInternal(FString &Buffer, FTrue Reflected) const;
#endif

    protected:
        FString Name;
        FName ClassFName;
        FName SuperClassName;
        // 导出的属性
        TArray<IExportedProperty*> Properties;
        // 导出的方法
        TArray<IExportedFunction*> Functions;
        // 导出的胶水方法
        TArray<IExportedFunction*> GlueFunctions;
    };

    // 导出内容更丰富，包括C++的属性和方法等
    template <bool bIsReflected, typename ClassType, typename... CtorArgType>
    struct TExportedClass : public TExportedClassBase<bIsReflected>
    {
        typedef TExportedClassBase<bIsReflected> FExportedClassBase;

        TExportedClass(const char *InName, const char *InSuperClassName = nullptr);

        bool AddBitFieldBoolProperty(const FString &InName, uint8 *Buffer);

        // 添加C++属性
        template <typename T> void AddProperty(const FString &InName, T ClassType::*Property);
        template <typename T, int32 N> void AddProperty(const FString &InName, T (ClassType::*Property)[N]);
        template <typename T> void AddStaticProperty(const FString &InName, T *Property);

        // 添加C++方法
        template <typename RetType, typename... ArgType> void AddFunction(const FString &InName, RetType(ClassType::*InFunc)(ArgType...));
        template <typename RetType, typename... ArgType> void AddFunction(const FString &InName, RetType(ClassType::*InFunc)(ArgType...) const);
        template <typename RetType, typename... ArgType> void AddStaticFunction(const FString &InName, RetType(*InFunc)(ArgType...));

        template <ESPMode Mode, typename... ArgType> void AddSharedPtrConstructor();
        template <ESPMode Mode, typename... ArgType> void AddSharedRefConstructor();

        void AddStaticCFunction(const FString &InName, lua_CFunction InFunc);

    private:
        void AddDefaultFunctions(FFalse NotReflected);
        void AddDefaultFunctions(FTrue Reflected);

        void AddDefaultFunctions_Reflected(FFalse NotUObject);
        void AddDefaultFunctions_Reflected(FTrue IsUObject) {}

        void AddConstructor(FFalse) {}
        void AddConstructor(FTrue Constructible);

        void AddDestructor(FFalse NotTrivial);
        void AddDestructor(FTrue) {}
    };


    /**
     * Exported enum
     * 导出Enum
     */
    struct UNLUA_API FExportedEnum : public IExportedEnum
    {
        explicit FExportedEnum(const FString &InName)
            : Name(InName)
        {}

        virtual void Register(lua_State *L) override;

#if WITH_EDITOR
        virtual FString GetName() const override { return Name; }
        virtual void GenerateIntelliSense(FString &Buffer) const override;
#endif

        void Add(const TCHAR *Key, int32 Value) { NameValues.Add(Key, Value); }

    protected:
        FString Name;
        TMap<FString, int32> NameValues;
    };

} // namespace UnLua

/**
 * Export a class
 * 导出Class
 */
#define EXPORT_UNTYPED_CLASS(Name, bIsReflected, Lib) \
    struct FExported##Name##Helper \
    { \
        static FExported##Name##Helper StaticInstance; \
        FExported##Name##Helper() \
            : ExportedClass(nullptr) \
        { \
            UnLua::IExportedClass *Class = UnLua::FindExportedClass(#Name); \
            if (!Class) \
            { \
                ExportedClass = new UnLua::TExportedClassBase<bIsReflected>(#Name); \
                UnLua::ExportClass(ExportedClass); \
                Class = ExportedClass; \
            } \
            Class->AddLib(Lib); \
        } \
        ~FExported##Name##Helper() \
        { \
            delete ExportedClass; \
        } \
        UnLua::IExportedClass *ExportedClass; \
    };

#define BEGIN_EXPORT_CLASS(Type, ...) \
    BEGIN_EXPORT_NAMED_CLASS(Type, Type, ##__VA_ARGS__)

#define BEGIN_EXPORT_NAMED_CLASS(Name, Type, ...) \
    DEFINE_NAMED_TYPE(#Name, Type) \
    BEGIN_EXPORT_CLASS_EX(false, Name, , Type, nullptr, ##__VA_ARGS__)

#define BEGIN_EXPORT_REFLECTED_CLASS(Type, ...) \
    BEGIN_EXPORT_CLASS_EX(true, Type, , Type, nullptr, ##__VA_ARGS__)

#define BEGIN_EXPORT_CLASS_WITH_SUPER(Type, SuperType, ...) \
    BEGIN_EXPORT_NAMED_CLASS_WITH_SUPER_NAME(Type, Type, #SuperType, ##__VA_ARGS__)

#define BEGIN_EXPORT_NAMED_CLASS_WITH_SUPER_NAME(TypeName, Type, SuperTypeName, ...) \
    DEFINE_NAMED_TYPE(#TypeName, Type) \
    BEGIN_EXPORT_CLASS_EX(false, TypeName, , Type, SuperTypeName, ##__VA_ARGS__)

#define BEGIN_EXPORT_CLASS_EX(bIsReflected, Name, Suffix, Type, SuperTypeName, ...) \
    struct FExported##Name##Suffix##Helper \
    { \
        typedef Type ClassType; \
        static FExported##Name##Suffix##Helper StaticInstance; \
        UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__> *ExportedClass; \
        ~FExported##Name##Suffix##Helper() \
        { \
            delete ExportedClass; \
        } \
        FExported##Name##Suffix##Helper() \
            : ExportedClass(nullptr) \
        { \
            UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__> *Class = (UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__>*)UnLua::FindExportedClass(#Name); \
            if (!Class) \
            { \
                ExportedClass = new UnLua::TExportedClass<bIsReflected, Type, ##__VA_ARGS__>(#Name, SuperTypeName); \
                UnLua::ExportClass((UnLua::IExportedClass*)ExportedClass); \
                Class = ExportedClass; \
            }

// 调用TExportedClassBase::AddProperty方法
// 用&ClassType::Property获取到Property的内存偏移
#define ADD_PROPERTY(Property) \
            Class->AddProperty(#Property, &ClassType::Property);

#define ADD_STATIC_PROPERTY(Property) \
            Class->AddStaticProperty(#Property, &ClassType::Property);

#define ADD_BITFIELD_BOOL_PROPERTY(Property) \
            { \
                uint8 Buffer[sizeof(ClassType)] = {0}; \
                ((ClassType*)Buffer)->Property = 1; \
                bool bSuccess = Class->AddBitFieldBoolProperty(#Property, Buffer); \
                check(bSuccess); \
            }

// 导出成员函数
// GlueFunction优点为灵活，但功能比较基础，需要自己处理函数参数与返回值
// 而ADD_FUNCTION导出的C++成员函数则可以自动处理，使用起来更方便
#define ADD_FUNCTION(Function) \
            Class->AddFunction(#Function, &ClassType::Function);

// 该宏有两个参数，第一个为导出到lua中的函数名，第二个为C++中真实函数名
// 这样可以自定义导出到lua的函数名，与ADD_FUNCTION的区别仅此而已，可以方便使用
// 一个例子为FVector，"ToRotator"和"ToQuat"简化了C++中很长的函数名
// 还有一个例子为C++中的"operator=="这类重载操作符，这个函数名无法在lua中使用
// 因此可以用ADD_NAMED_FUNCTION("Equals"，operator==)，把Equals作为函数名导出到lua
#define ADD_NAMED_FUNCTION(Name, Function) \
            Class->AddFunction(Name, &ClassType::Function);

// 导出成员函数的完全体，可以手动指定函数名、返回值类型、导出函数地址、函数参数列表
#define ADD_FUNCTION_EX(Name, RetType, Function, ...) \
            Class->AddFunction<RetType, ##__VA_ARGS__>(Name, (RetType(ClassType::*)(__VA_ARGS__))(&ClassType::Function));

#define ADD_CONST_FUNCTION_EX(Name, RetType, Function, ...) \
            Class->AddFunction<RetType, ##__VA_ARGS__>(Name, (RetType(ClassType::*)(__VA_ARGS__) const)(&ClassType::Function));

// 用于导出静态成员函数，不需要类实例即可调用
// 宏展开会调用AddStaticFunction函数，创建TExportedStaticMemberFunction实例，并同样加入到Functions容器中
// 与ADD_FUNCTION的主要区别为成员Func初始化时不需要使用lambda中转成Obj->*InFunc的形式调用
// 直接用导出函数地址初始化Func即可，不需要考虑this，其他Invoke流程均相同
#define ADD_STATIC_FUNCTION(Function) \
            Class->AddStaticFunction(#Function, &ClassType::Function);

#define ADD_STATIC_FUNCTION_EX(Name, RetType, Function, ...) \
            Class->AddStaticFunction<RetType, ##__VA_ARGS__>(Name, &ClassType::Function);

#define ADD_EXTERNAL_FUNCTION(RetType, Function, ...) \
            Class->AddStaticFunction<RetType, ##__VA_ARGS__>(#Function, Function);

#define ADD_EXTERNAL_FUNCTION_EX(Name, RetType, Function, ...) \
            Class->AddStaticFunction<RetType, ##__VA_ARGS__>(Name, Function);

#define ADD_STATIC_CFUNTION(Function) \
            Class->AddStaticCFunction(#Function, &ClassType::Function);

#define ADD_NAMED_STATIC_CFUNTION(Name, Function) \
            Class->AddStaticCFunction(Name, &ClassType::Function);

// 导出lua原生C模块
#define ADD_LIB(Lib) \
            Class->AddLib(Lib);

#define ADD_SHARED_PTR_CONSTRUCTOR(Mode, ...) \
            Class->AddSharedPtrConstructor<Mode, ##__VA_ARGS__>();

#define ADD_SHARED_REF_CONSTRUCTOR(Mode, ...) \
            Class->AddSharedRefConstructor<Mode, ##__VA_ARGS__>();

#define END_EXPORT_CLASS(...) \
        } \
    };

#define IMPLEMENT_EXPORTED_CLASS(Name) \
    FExported##Name##Helper FExported##Name##Helper::StaticInstance;

#define IMPLEMENT_EXPORTED_CLASS_EX(Name, Suffix) \
    FExported##Name##Suffix##Helper FExported##Name##Suffix##Helper::StaticInstance;

/**
 * Export a global function
 * 导出全局函数，不依赖classmetatable
 * 参数有返回值、函数名、参数列表
 * 直接创建类一个新的struct，名称为FExportedFunc##Function，继承自TExportedFunction
 * struct的构造函数，除了调用基类构造函数外，还调用类UnLua::ExportFunction函数
 * 作用为把该struct实例加入到FLuaContext::ExportedFunctions实例中
 * 在luastate启动流程，在导出非反射类之后，就会遍历ExportedFunctions容器，调用Register方法，导出全局函数
 */
#define EXPORT_FUNCTION(RetType, Function, ...) \
    static struct FExportedFunc##Function : public UnLua::TExportedFunction<RetType, ##__VA_ARGS__> \
    { \
        FExportedFunc##Function(const FString &InName, RetType(*InFunc)(__VA_ARGS__)) \
            : UnLua::TExportedFunction<RetType, ##__VA_ARGS__>(InName, InFunc) \
        { \
            UnLua::ExportFunction(this); \
        } \
    } Exported##Function(#Function, Function);

#define EXPORT_FUNCTION_EX(Name, RetType, Function, ...) \
    static struct FExportedFunc##Name : public UnLua::TExportedFunction<RetType, ##__VA_ARGS__> \
    { \
        FExportedFunc##Name(const FString &InName, RetType(*InFunc)(__VA_ARGS__)) \
            : UnLua::TExportedFunction<RetType, ##__VA_ARGS__>(InName, InFunc) \
        { \
            UnLua::ExportFunction(this); \
        } \
    } Exported##Name(#Name, Function);

/**
 * Export an enum
 * 导出Enum
 */
#define BEGIN_EXPORT_ENUM(Enum) \
    DEFINE_NAMED_TYPE(#Enum, Enum) \
    static struct FExported##Enum : public UnLua::FExportedEnum \
    { \
        typedef Enum EnumType; \
        FExported##Enum(const FString &InName) \
            : UnLua::FExportedEnum(InName) \
        { \
            UnLua::ExportEnum(this);

#define ADD_ENUM_VALUE(Value) \
            NameValues.Add(#Value, Value);

#define ADD_SCOPED_ENUM_VALUE(Value) \
            NameValues.Add(#Value, (int32)EnumType::Value);

#define END_EXPORT_ENUM(Enum) \
        } \
    } Exported##Enum(#Enum);


#include "UnLuaEx.inl"
