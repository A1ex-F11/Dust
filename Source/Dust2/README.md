# Dust2 源码导览

本模块保留现有类名、蓝图接口和玩法职责。Public 是对外头文件，Private 是实现；两边按系统使用相同目录，测试在 Private/Tests 的对应分组。

| 目录 | 主要职责 | 阅读入口 |
| --- | --- | --- |
| Player | 玩家输入、动作调度、步态与脚步 | [Dust2PlayerCharacter](Public/Player/Dust2PlayerCharacter.h) |
| Weapons | 武器运行状态、射击许可、弹药与基础配置 | [WeaponRuntime](Public/Weapons/WeaponRuntime.h)、[WeaponDefinitionData](Public/Weapons/WeaponDefinitionData.h) |
| Presentation | 视觉绑定、拔收枪/换弹表现、持枪摆动、镜头后坐与弹道线 | [WeaponPresentationComponent](Public/Presentation/WeaponPresentationComponent.h)、[WeaponViewmodelData](Public/Presentation/WeaponViewmodelData.h) |
| AI | 敌人感知/交战、StateTree、巡逻与换弹协调 | [DustEnemyCombatAdapterComponent](Public/AI/DustEnemyCombatAdapterComponent.h)、[DustEnemyStateTreeTasks](Public/AI/DustEnemyStateTreeTasks.h) |
| Inventory | 双槽库存、拾取、敌人掉落与本局拾取消耗记录 | [WeaponInventoryComponent](Public/Inventory/WeaponInventoryComponent.h)、[DustPickupPersistenceSubsystem](Public/Inventory/DustPickupPersistenceSubsystem.h) |
| Combat | 射线结果、伤害回执、死亡镜头与布娃娃 | [WeaponShotResolution](Public/Combat/WeaponShotResolution.h)、[DustRagdollDeathComponent](Public/Combat/DustRagdollDeathComponent.h) |
| Mission | 任务阶段、遭遇统计、C4许可与结果 | [Dust2GameMode](Public/Mission/Dust2GameMode.h)、[DustEncounterComponent](Public/Mission/DustEncounterComponent.h) |
| UI | 准星、命中/击杀反馈、双武器显示与准星设置 | [DustCrosshairComponent](Public/UI/DustCrosshairComponent.h)、[DustCombatFeedbackComponent](Public/UI/DustCombatFeedbackComponent.h) |
| Editor | 编辑器详情面板、配置迁移与资源维护命令 | [WeaponDetailsCustomization](Private/Editor/WeaponDetailsCustomization.cpp)、[WeaponConfigurationUpgrade](Private/Editor/WeaponConfigurationUpgrade.cpp) |

## 阅读顺序

一次开火：Player 输入/调度 → Weapons 的提交许可与状态 → Combat 的射线和伤害 → Presentation/UI 的表现与反馈。

一次切枪/拾取：Inventory 管正式武器与槽位，Presentation 管对应视觉实例及动作请求。正式武器和视觉枪保持各自职责。

敌人行为：StateTree 与任务负责行为选择，CombatAdapter 提供交战上下文，EnemyWeapon/EnemyReload/EnemyPatrol 执行各自操作。

检查点与任务：Mission 提供任务/遭遇原生入口，Inventory 重建武器；现有蓝图仍承担未迁入C++的触发与检查点接线。

历史资料中的旧文件路径可按同名文件在上述分组定位；类名、模块名和 /Script/Dust2 类型路径保持。目录调整后由J重新生成VS项目文件并编译验证。
