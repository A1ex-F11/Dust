# 自制曳光：源码与本地接入

本功能只显示一段从当前可见枪口飞向本次射线终点的亮线。命中使用原 HitResult.ImpactPoint，未命中使用原 Trace.End；伤害仍在射线命中时立即处理。不会重新计算散布、后坐或弹药。

运行时由 UShotTracerComponent 管理 Niagara 实例。它在激活前写入距离、飞行时长、亮线长度、宽度和颜色；无效参数/无资产/专用服务器安全跳过，最多保留 64 个有效实例，EndPlay 清理。飞行时间下限一帧，上限默认 0.15 秒。Niagara 发射器只产生一个面片粒子，独立生命周期 Once，ParticleState 唯一推进年龄。亮线尾端限制在枪口之后，前端限制在实际落点之前。

编辑器命令只在 WITH_EDITOR 编译：
- Dust2.BuildTracerAssets：创建新材质 M_ShotTracerGlow 和系统 NS_ShotTracer，已有同名资产时拒绝覆盖。
- Dust2.VerifyTracerAssets：只读验证资产参数、渲染器、材质和运行脚本编译状态。
- Dust2.InstallTracerWiring：备份并接入 Bp_WeaponBase.RequestFire；重复完整安装不会重复加节点，部分安装拒绝覆盖。
- Dust2.VerifyTracerWiring：只读验证保存后的执行链和终点来源。
- Dust2.InspectTracerWiring：只读检查相关组件和射线节点。
- Dust2.RepairTracerLifetime：本轮生成资产的生命周期修正工具，保存前备份。
- Dust2.UpgradeTracerMaterial：备份并将已生成的加法材质升级为半透明橙色主体、浅色亮芯；只修改材质，不重新安装蓝图或重置枪口与粒子参数。

当前本地接入使用 BP_Dust2_TacticalPresentationTest.TacticalPresentationWeapon 上作者武器的 MuzzleFlash 世界位置，支持该测试玩家当前切换的武器。其他玩家类的转换失败会安全跳过曳光；正式玩家接回属于后续任务。Trace 后 Sequence.Then0 只走视觉表现，Then1 保留原命中伤害分支。DrawDebugType 从 ForDuration 改为 None。

2026-10-12 证据：真实 Dust2Editor Win64 Development 编译和链接成功；Dust2.Tracer 两项测试通过、0 错误。实际 Niagara CPU 模拟在 0.03 秒时年龄为 0.03，段中心 X=560、长度80、宽1.2（目标距离3000，时长0.15）；系统在0.21秒结束。测试覆盖几何边界、一个实际粒子、年龄推进、段头/尾界限和结束清理。

本地材质和 Niagara 系统位于 /Game/Code/VFX/Tracer。保留原 M_TracerGlow。蓝图和 Content 不在此源码仓库；SavedAssetFlightAndCleanup 测试需要真实工程中生成的 NS_ShotTracer，源码镜像单独克隆不含完整可运行游戏。

用户已确认曳光可见，并自行调整了枪口位置；随后反馈明亮背景下不明显。本次材质改为 Translucent/Unlit，保留深度测试，以最高0.92透明度的橙色主体遮盖部分明亮背景，中央窄亮芯保留发光感。新版明亮场景 PIE 效果尚待用户确认。自动模拟通过不替代枪口视觉、空弹/换弹拒绝、切枪后当前枪口以及原伤害/扣弹的用户运行检查。五枪已接受的后坐/散布配置未修改，W05暂缓。
