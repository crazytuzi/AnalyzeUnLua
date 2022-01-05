# Change Log
All notable changes to this project will be documented in this file.
 
The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [2.1.0] - 2021-12-6

### Added

- 支持最新的UE5抢先体验版
- 基于UE的自动化测试系统的全API测试、回归测试覆盖
- 支持自定义加载器，允许用户自己扩展实现Lua文件的查找与加载逻辑
- 自定义加载器的示例教程
- 编辑器下增加了Toolbar用来进行快速绑定/解绑Lua，以及一些常用功能入口
- 增加 `UUnLuaLatentAction` 用于包装异步行为，支持 `SetTickableWhenPaused` 
- 支持使用 `ADD_STATIC_PROPERTY` 宏来导出静态成员变量
- 支持在Lua中直接访问 `EKeys`
- 编辑器下动态绑定一个静态绑定的对象，会有警告日志避免误操作
- `UnLuaDefaultParamCollector` 模块的默认值生成现在支持 `AutoCreateRefTerm` 标记

### Changed

- 在Lua中访问UE对象统一使用`UE`，考虑到向后兼容，原来的`UE4`继续保留
- 统一结构类型的构造参数表达，与UE相同
  - FVector/FIntVector/FVector2D/FVector4/FIntPoint
  - FColor/FLinearColor
  - FQuat/FRotator/FTransform
- 统一UObject在Lua侧IsValid的语义，与UE相同

### Fixed
- Lua中调用UClass的方法提示方法为nil [#274](https://github.com/Tencent/UnLua/issues/274)
- 游戏世界暂停后，在协程里的Delay后不继续执行 [#276](https://github.com/Tencent/UnLua/pull/276)
- 在非主线程崩溃时不应该访问Lua堆栈 [#278](https://github.com/Tencent/UnLua/pull/278)
- 无法将FVector/FRotator/FTransform等类型的函数参数保存到lua对象中 [#279](https://github.com/Tencent/UnLua/issues/279)
- 调用Lua时参数传递顺序异常 [#280](https://github.com/Tencent/UnLua/issues/280)
- require不存在的lib会崩 [#284](https://github.com/Tencent/UnLua/issues/284)
- 蓝图TMap的FindRef错误 [#286](https://github.com/Tencent/UnLua/issues/286)
- UMG里Image用到的Texture内存泄漏 [#288](https://github.com/Tencent/UnLua/issues/288)
- UClass在被UEGC后没有释放相应的绑定 [#289](https://github.com/Tencent/UnLua/issues/289)
- 调用K2_DestroyActor后，立刻调用IsValid会返回true [#292](https://github.com/Tencent/UnLua/issues/292)
- 关闭RPC会导致部分函数在非Editor模式下crash [#293](https://github.com/Tencent/UnLua/issues/293)
- 索引DataTable时，如果没有这个key，会得到异常的返回值 [#298](https://github.com/Tencent/UnLua/pull/298)


### Removed

- 移除宏定义 `CHECK_BLUEPRINTEVENT_FOR_NATIVIZED_CLASS`
- 移除宏定义 `CLEAR_INTERNAL_NATIVE_FLAG_DURING_DUPLICATION`

## [2.0.1] - 2021-10-20

### Added

1. UMG释放引用教程
2. Lua热重载机制的开关
3. 部分英文文档

### Fixed

1. 首次切换场景到新场景的Actor的ReceiveBeginPlay没有调用到的问题 #268
2. 不同Actor绑定到同一个Lua脚本没有生效的问题 #269
3. 在编辑器中绑定某些对象时会报class is out of data的问题
4. 在Lua中获取对象属性异常时可能会导致Crash的问题
5. 在编辑器通过-game运行游戏时RestartLevel会导致Crash的问题

## [2.0.0] - 2021-09-18

1. 调整了UObject Ref 的释放机制
2. 更新并适配 Lua 5.4.2
3. 支持对非必要过滤对象的绑定过滤（包括load阶段产生的临时对象、抽象对象等）
4. 增加中文说明文档
5. 增加 Hotfix 功能（增加UnLuaFramework Module，并且使用时需要添加Content/Script 目录下的 G6HotfixHelper.lua/G6Hotfix.lua）
6. 修复若干bugs（参考commits）

## [1.2.0] - 2021-02-09

fix some bugs, could see details from commits

## [1.1.0] - 2020-04-04

Fix the crash bug caused incorrect userdata address.

## [1.0.0] - 2019-08-13

First version of UnLua.