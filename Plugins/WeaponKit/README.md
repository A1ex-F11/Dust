# WeaponKit

从 Dust FPS 演示里抽出来的 **UE 5.8 运行时插件**。它只依赖引擎模块（Core、CoreUObject、Engine，以及曳光用的 Niagara），不包含 Dust2 角色、表现层、关卡或任务代码。拷贝到别的工程后，用自己的角色调用公开接口即可。

反射类型统一加了 `WeaponKit` 前缀，导出宏是 `WEAPONKIT_API`。这样插件可以和 Dust2 放在同一个工程里，而不会和原来的 `AWeaponRuntime`、`UWeaponDefinitionData` 等同名类冲突。

## 目录

| 路径 | 作用 |
| --- | --- |
| `Source/WeaponKit` | 运行时模块。游戏打包会带上它。 |
| `Source/WeaponKit/Public/Math` | 无状态数学：连续射击散布、弹道后坐、镜头冲量包络、曳光线段。 |
| `Source/WeaponKit/Public/Data` | 开火模式、散布配置、枪械 / 后坐 / 弹道 DataAsset、存档和伤害区域结构。 |
| `Source/WeaponKit/Public/Damage` | 按「骨骼 → 区域 → 倍率」结算伤害。 |
| `Source/WeaponKit/Public/Tracer` | 射击后播放 Niagara 曳光。 |
| `Source/WeaponKit/Public/Recoil` | 镜头 / 控制器后坐。 |
| `Source/WeaponKit/Public/Runtime` | 武器 Actor、拾取物、交互扫描、Child Actor 槽位实现。 |
| `Source/WeaponKit/Public/Inventory` | 可变槽位背包。 |
| `Source/WeaponKit/Public/Interfaces` | 携带者、槽位宿主、拾取物三个接口。 |
| `Source/WeaponKitTests` | 自动化测试模块，类型是 `UncookedOnly`，不会打进正式包。 |

`WeaponKit.Build.cs` 里 Niagara 是私有依赖：公开头文件只前向声明 Niagara 类型，真正刷参数的代码在 cpp 里。

## 装进任意 UE 5.8 工程

1. 把整个 `WeaponKit` 文件夹复制到目标工程的 `Plugins/WeaponKit`。如果该工程的 `.gitignore` 忽略了 `Plugins/`，要单独放行这个目录，否则源码不会进版本库。
2. 编辑器里打开 **Edit → Plugins**，启用 **Weapon Kit**。它声明了对 Niagara 的依赖，启用时 Niagara 也会打开。也可以在 `.uproject` 里写：

```json
{
  "Name": "WeaponKit",
  "Enabled": true
}
```

3. 在使用这些类的游戏模块 `Build.cs` 里增加依赖：

```csharp
PrivateDependencyModuleNames.Add("WeaponKit");
```

4. 重新生成工程文件并编译。不要再依赖 Dust2。角色、动画、准星和换弹表现留在游戏模块里，由游戏在合适的时机调用下面的接口。

插件描述里 `EnabledByDefault` 是 `false`。放进 Dust 工程时，不打开它就不会参与编译，原来的 Dust2 模块保持原样。

### 游戏侧最少要接的几件事

- 给角色加 `UWeaponKitInventoryComponent`、若干 `UWeaponKitChildActorSlot`（或自己的 `IWeaponKitSlotHost`）、一个 `UWeaponKitCameraRecoilComponent`。
- 右键瞄准时，由角色自己调用 `AWeaponKitRuntime::SetAiming` 和 `UWeaponKitCameraRecoilComponent::SetAiming`。插件不再认识具体的玩家类。
- 需要死亡、首抽是否播过，就让 Pawn 实现 `IWeaponKitCarrier`。不实现时，插件把携带者当成存活，并且没有「首抽已消耗」的记录。
- 拾取扫描用 `UWeaponKitInteractionScanner`。玩家按下拾取键时，把扫描结果传给 `TryPickup`。背包不会自己去角色身上找「当前瞄准的那把枪」。
- 自动拾取的重叠体用 `AWeaponKitPickup::SetAutoPickupVolume` 指过去。没有按组件名字搜索。

## 各部分做什么

### 数学

四个头文件都是纯函数或很小的状态结构，不碰 World，也不读资产。

- `FWeaponKitFiringSpreadMath`：连续射击散布的增加和延迟恢复。非法数值会收成 0。
- `FWeaponKitShotRecoilMath`：把累计的俯仰 / 偏航加到射击方向上，并按停火时间收回。瞄准时武器不会把这份偏移再加一次，下一发用的就是调用方给的视线。
- `FWeaponKitRecoilKickEnvelope`：一发镜头冲量。积分只跟已经过的时间有关，把一帧拆成多步不会改变总量。
- `FWeaponKitShotTracerGeometry`：只算曳光的飞行时间和线段，不做伤害射线。

### 数据资产

`UWeaponKitDefinitionData` 是一把枪的静态配置。运行时弹药、当前开火模式、散布累计都在 `AWeaponKitRuntime` 上，打出去的子弹不会改共享资产。

腰射和瞄准使用同一种 `FWeaponKitSpreadProfile`（基础锥角和连续射击散布都在里面）。`bUseSeparateAimingSpread` 为 false 时，瞄准沿用腰射那一份。

`UWeaponKitRecoilData`、`UWeaponKitBallisticsData` 在配置资产的构造函数里作为内联子对象创建，并标成 `RF_Public`，方便在编辑器里展开。弹道资产上的爆头倍率只是一个数；哪根骨骼算头，由伤害区域表决定。

`FWeaponKitLoadoutSave` 用一个数组描述每个槽位的武器类和 `FWeaponKitPickupState`，再加上当前槽位下标。

### 武器运行时

`AWeaponKitRuntime` 负责开火间隔、弹药、换弹（整匣或逐发）、开火模式循环、后坐序列和散布采样。静态数从 DataAsset 读。没有绑配置时，只用构造函数里的代码默认值，不再保留一套「给旧蓝图填的」可编辑字段，也不再通过属性名把弹道对象写回蓝图子类。

瞄准是公开的 `SetAiming(bool)`。瞄准期间提交的那一发会记下「不要再叠加弹道后坐」，松开之后这一发的采样结果不变。连续射击散布在腰射和瞄准两套配置上分别累计，来回切换不会把另一套清零。

### 镜头后坐

`UWeaponKitCameraRecoilComponent::ApplyRecoil` 是唯一的施加入口。

- 瞄准：按 `AimingRecoilScale` 写入控制旋转，下一发采样到的就是同一条视线。
- 腰射且开启「射击后坐与镜头分离」：只给摄像机加偏移，不改控制旋转。
- 其他腰射：走控制器输入。随机后坐仍受组件上的俯仰 / 偏航累计上限约束。

`SetAiming` 在瞄准状态真正发生变化时会清掉还没施加完的尾巴，已经写进视角的角度保持不动。`NotifyManualLook` 只取消自动恢复，不把视角拽回去。

### 曳光

`UWeaponKitShotTracerComponent::PlayShotTracer` 在伤害射线成功之后调用。距离、寿命、长度、宽度、颜色对应的 Niagara User 参数都是可编辑的 `FName`，默认仍是 `User.TracerDistance`、`User.TracerLifetime`、`User.TracerLength`、`User.TracerWidth`、`User.TracerColor`。旧特效不用改名；换了参数名的特效改组件上的字段即可。同时存在的曳光数量也有上限，默认 64。

插件里没有 Dust 工程的特效资产路径。几何测试不加载 `/Game/...` 资源。

### 背包、槽位、拾取、扫描

- `IWeaponKitSlotHost`：一个槽位如何生成和清空武器。`UWeaponKitChildActorSlot` 是用 Child Actor 做的默认实现，背包不依赖这个具体类。
- `UWeaponKitInventoryComponent`：槽位个数等于初始化时传入的宿主数量。切换会找下一个有枪的槽。身上至少还留一把枪时才允许丢弃当前武器。
- `IWeaponKitPickup` / `AWeaponKitPickup`：背包只认接口。丢枪时用 `PickupClassByWeaponClass` 里登记的 Actor 类生成，这个类必须实现接口。
- `UWeaponKitInteractionScanner`：登记附近的拾取物，按距离、视线和存活状态选出手动或自动目标，并用定时器刷新「当前瞄准的拾取物」。自动拾取要求接口返回的重叠体正在和 Pawn 重叠。
- `IWeaponKitCarrier`：可选。用来回答「还活着吗」和「这把枪的首抽动画是否已经播过」。

丢枪成功之后，如果背包上指定了扫描器，会再问一次自动拾取候选，用来补上原来「丢掉一把后立刻捡起脚边另一把」的行为。扫描逻辑不写在背包里面。

### 伤害

`UWeaponKitDamageLibrary::CalculateFinalDamage` 的入参是一组 `FWeaponKitDamageRegion`。区域列出骨骼名、倍率，以及是否吃头盔、是否吃护甲。先命中的区域生效。没有任何区域列出的骨骼，倍率是 1，并且吃身体护甲。因此骨骼名 `"head"` 本身没有特殊含义。

`MakeHeadBodyRegions` 用来按调用方给出的头部骨骼名拼出和旧算法相同的一张表：头部乘爆头倍率，有头盔再乘穿甲；其余骨骼有护甲时乘穿甲。

## 解耦：之前和之后

Dust 里这些类型当时直接服务于 `ADust2PlayerCharacter` 和两格背包。插件里的对应物去掉了这种绑定。Dust2 源码没有改，下面的「之前」描述的是抽出时的写法。

| 点 | 之前 | 之后 |
| --- | --- | --- |
| 瞄准 | `AWeaponRuntime` 和镜头后坐组件把 `ADust2PlayerCharacter` 设为 `friend`，由角色调用私有的 `SetOwnerAimState` / `SetAimingRecoilMode`。 | 两个 `SetAiming(bool)` 都是公开 API。角色、AI 或测试代码都可以调用。 |
| 腰射 / 瞄准散布 | 腰射是配置资产上的一排散落字段，瞄准才是 `FWeaponSpreadProfile`，另外还有只序列化、不再参与计算的 `bIgnoreShotRecoilWhenAiming`。 | 两边都是 `FWeaponKitSpreadProfile`。瞄准是否使用独立配置由 `bUseSeparateAimingSpread` 决定。瞄准时忽略弹道后坐是运行时规则，不再靠遗留布尔值。 |
| 蓝图兜底 | 武器 Actor 上有一套 `EditCondition = WeaponDefinition == nullptr` 的字段；弹道和连发恢复间隔用属性名反射写回蓝图。`CaptureDefinition` 和 `SyncLegacyWeaponReferences` 是迁移 / 旧变量镜像。 | 这些入口都没有搬进来。静态数据只住在 DataAsset 里。展示名、弹道、连发恢复间隔通过读取函数拿。 |
| 镜头后坐入口 | `ApplyRecoilKick` 是不带数据资产的兼容入口，`ApplyRecoilKickWithData` 再分流到旧路径。 | 只保留 `ApplyRecoil`。随机后坐的累计上限仍是组件上的可配置值，而不是一条单独的兼容函数。 |
| 背包槽位 | 写死 `Slot1` / `Slot2` 两个 `UChildActorComponent`。 | `TArray` 槽位，元素是 `IWeaponKitSlotHost`。 |
| 存档 | `RestoreCheckpointWeapons` 有 13 个参数（两个槽的类、弹药、开火模式、首抽标记，再加谁是当前武器）。 | `RestoreLoadout(槽位, FWeaponKitLoadoutSave)`。 |
| 拾取 | 背包直接依赖 `AWeaponPickupActor`，并 `Cast` 到 `ADust2PlayerCharacter` 查死亡、附近武器和首抽。 | 背包只调用 `IWeaponKitPickup` 和可选的 `IWeaponKitCarrier`。 |
| 交互 | 自动拾取体积靠组件名 `AutoPickupCollision` 查找；附近武器和瞄准武器的射线写在角色里。 | 体积由 `SetAutoPickupVolume` 指定。`UWeaponKitInteractionScanner` 单独负责登记、距离、视线和瞄准回调。 |
| 伤害 | `UDamageLibrary` 写死骨骼名 `"head"`。 | 骨骼到区域的映射是数据。不在表里的骨骼走身体规则。 |
| 曳光 | Niagara 参数名写在代码里；编辑器测试还加载 Dust 的特效资产。 | 参数名是组件属性。插件测试只检查几何和一个未配置组件的失败路径。 |

没有搬进插件的部分：`WeaponPresentationComponent`、武器数据迁移和特效搭建脚本、GameMode、任务流程，以及玩家角色本身。角色上的瞄准输入、换弹表现和血量仍然属于游戏。

## 自动化测试

启用插件后，在编辑器 **Session Frontend → Automation** 里筛选 `WeaponKit`。测试名前缀从原来的 `Dust2.*` 改成了 `WeaponKit.*`。

| 测试 | 覆盖 |
| --- | --- |
| `WeaponKit.Spread.MathBoundaries` | 散布加减和恢复的边界、NaN。 |
| `WeaponKit.Recoil.ShotMath` | 弹道后坐的延迟恢复和方向叠加。 |
| `WeaponKit.Recoil.KickEnvelopeIntegral` | 冲量包络拆帧后总量不变。 |
| `WeaponKit.Tracer.FlightAndSegmentBounds` | 曳光飞行时间、线段不越出枪口和落点；参数名可改。 |
| `WeaponKit.Damage.RegionMultipliers` | 旧的头 / 身体数值，以及 `"head"` 未列入区域时不再爆头。 |
| `WeaponKit.Runtime.InitialConfigAndRestore` | 初始弹药来自资产，读档不被 `PostInitialize` 覆盖，共享资产不被实例改写。 |
| `WeaponKit.Runtime.ContinuousFireSpread` | 连续射击散布的增长、上限和恢复。 |
| `WeaponKit.Runtime.AimSpreadSnapshotAndBloom` | 已提交的那一发保持提交时的锥角；两套散布分开累计。 |
| `WeaponKit.Runtime.AimIgnoresAccumulatedShotRecoil` | 瞄准提交的射击跟着输入方向走，镜头冲量仍然会生成。 |
| `WeaponKit.Recoil.AimViewWriter` | 瞄准写入控制旋转并乘瞄准倍率；腰射分离模式不写控制旋转。 |
| `WeaponKit.Recoil.TailCleanup` | 退出瞄准取消未完成的尾巴；手动观察不会被恢复逻辑拽回去。 |
| `WeaponKit.Inventory.LoadoutSwitchAndDrop` | 三个槽、一份存档结构、切换、丢弃、再通过接口捡回。 |
| `WeaponKit.Scanner.ManualPickupWithoutNameLookup` | 不实现接口的 Actor 被拒绝；距离和死亡生效；没有指定体积时不会自动拾取。 |

原来依赖 `ADust2PlayerCharacter` 和 `UWeaponPresentationComponent` 的瞄准输入、换弹取消瞄准、死亡清瞄准，没有搬进来。那些规则留在角色上。Dust 曳光资产的模拟测试也没有搬，因为它加载的是工程内容而不是插件代码。

## 验证边界

这份插件是按 UE 5.8 的模块、UHT 和自动化测试写法整理的。当前环境没有安装 Unreal Editor，也没有编译 UBT，所以 **没有实际编译，也没有跑过上面的自动化测试**。启用插件后的第一次编译，才是这些声明能不能通过 UHT 的依据。
