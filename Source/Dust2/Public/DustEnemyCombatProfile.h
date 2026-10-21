#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DustEnemyCombatProfile.generated.h"

/** Editable difficulty and combat pacing settings for an enemy. */
UCLASS(BlueprintType, meta = (DisplayName = "敌人战斗难度配置"))
class DUST2_API UDustEnemyCombatProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|反应",
		meta = (DisplayName = "发现后的反应时间（秒）", ClampMin = "0.0"))
	float ReactionDelay = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|瞄准",
		meta = (DisplayName = "稳定瞄准时间（秒）", ClampMin = "0.0"))
	float SettledAimSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|瞄准",
		meta = (DisplayName = "目标跟踪速度（度/秒）", ClampMin = "0.0",
			ToolTip = "瞄准方向向目标收敛的角速度。默认 360；已有资产保存过旧值，需手动改为 360。"))
	float TrackingSpeedDegPerSecond = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|压枪",
		meta = (DisplayName = "压枪补偿比例", ClampMin = "0.0", ClampMax = "1.0"))
	float CompensationFraction = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|压枪",
		meta = (DisplayName = "压枪修正速度（度/秒）", ClampMin = "0.0"))
	float CompensationSpeedDegPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|压枪",
		meta = (DisplayName = "压枪反应延迟（秒）", ClampMin = "0.0"))
	float CompensationDelay = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|压枪",
		meta = (DisplayName = "压枪漂移角度（度）", ClampMin = "0.0"))
	float CompensationDriftDegrees = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|压枪",
		meta = (DisplayName = "漂移响应速度", ClampMin = "0.0"))
	float DriftResponseSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|射击",
		meta = (DisplayName = "每轮最少射击数（全自动）", ClampMin = "1",
			ToolTip = "全自动每轮连射至少发射的发数；与最多发数相同表示固定发数。旧资产已保存的每轮射击数会保留为最少值，不会自动改写。"))
	int32 BurstShotCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|射击",
		meta = (DisplayName = "每轮最多射击数（全自动）", ClampMin = "1",
			ToolTip = "全自动每轮连射最多发射的发数，实际发数在最少与最多之间随机且包含两端；小于最少值时自动按较小/较大值归一化，最少为1。"))
	int32 MaxBurstShotCount = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|射击",
		meta = (DisplayName = "射击轮次间隔（秒）", ClampMin = "0.0"))
	float BurstPauseSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|瞄准",
		meta = (DisplayName = "瞄准高度偏移（厘米）", ToolTip = "从角色 ActorLocation 向上偏移后作为 AI 的基础瞄准点。"))
	float AimHeightOffset = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|移动",
		meta = (DisplayName = "判定站稳的最高速度（厘米/秒）", ClampMin = "0.0"))
	float RequiredSettledSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|移动",
		meta = (DisplayName = "交火距离（厘米）", ClampMin = "0.0"))
	float EngagementRange = 1200.0f;

	// ---- R13 close-range awareness, short memory, fast reacquire and spacing ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|感知",
		meta = (DisplayName = "近距全向确认距离（厘米）", ClampMin = "0.0",
			ToolTip = "只对当前已交战或最近确认过的同一目标生效：在此距离内且真实无遮挡时可忽略前方视野完成确认。不会扫描未知背后目标。"))
	float CloseCombatSenseRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|感知",
		meta = (DisplayName = "丢失视野宽限（秒）", ClampMin = "0.0",
			ToolTip = "普通丢失视野后保留已知目标并停止射击/追击的时长；到期仍无确认则进入原有最后位置搜索。0 表示保持旧的立即丢失行为。"))
	float LostSightGraceSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|感知",
		meta = (DisplayName = "同目标快速重获窗口（秒）", ClampMin = "0.0",
			ToolTip = "同一已知或搜索目标在最后确认后的此窗口内重获时，可缩短首次反应；首次发现和换目标不继承。"))
	float QuickReacquireWindowSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|感知",
		meta = (DisplayName = "快速重获反应时间（秒）", ClampMin = "0.0",
			ToolTip = "快速重获时实际使用 min(发现后的反应时间, 本值)；不会跳过站稳和实际身体对准。"))
	float QuickReacquireReactionSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|瞄准",
		meta = (DisplayName = "身体对准开火角（度）", ClampMin = "0.0", ClampMax = "180.0",
			ToolTip = "有目标知识适配器时，Pawn 水平前向与目标和跟踪方向都进入此角度才允许提交射击。无适配器的旧原生测试路径保持原行为。"))
	float FireAlignmentAngleDegrees = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距触发距离（厘米）", ClampMin = "0.0",
			ToolTip = "与当前确认目标近于此距离时，才允许进入独立的 Enemy Create Space 任务。"))
	float CreateSpaceTriggerDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "期望交战距离（厘米）", ClampMin = "0.0",
			ToolTip = "拉距目的地只按开始确认时的玩家位置快照选一次；单次步长向此距离靠拢，但不超过拉距步长。"))
	float DesiredEngagementDistance = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距单次最大步长（厘米）", ClampMin = "1.0",
			ToolTip = "一次拉距移动允许的最大位移；只发起一次短 MoveTo，不做持续退避。默认 400。"))
	float CreateSpaceStepDistance = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距移动速度（厘米/秒）", ClampMin = "0.01",
			ToolTip = "仅本次受控拉距期间临时覆盖 CharacterMovement.MaxWalkSpeed；结束/失败/超时/退出/丢确认/死亡/终局都会恢复原值。"))
	float CreateSpaceMoveSpeed = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距冷却（秒）", ClampMin = "0.0",
			ToolTip = "成功、失败和取消都会应用；冷却结束前 CanCreateSpace 为 false。"))
	float CreateSpaceCooldownSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距超时（秒）", ClampMin = "0.1",
			ToolTip = "单次拉距移动允许的最长时间；超时只取消自己的请求并回到原地战斗。"))
	float CreateSpaceTimeoutSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距",
		meta = (DisplayName = "拉距到达半径（厘米）", ClampMin = "0.0", ToolTip="拉开距离移动时使用的到达容差，单位厘米；数值越大，越容易判定已经到达。"))
	float CreateSpaceAcceptanceRadius = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|移动",
		meta = (DisplayName = "启用敌人局部避让", ToolTip = "在CharacterMovement启用RVO，移动时避让其他参与避让的敌人。不能穿墙或保证完全堵死处可通过。"))
	bool bEnableLocalAvoidance = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|移动",
		meta = (DisplayName = "局部避让考虑半径（厘米）", ClampMin = "1.0", EditCondition = "bEnableLocalAvoidance"))
	float LocalAvoidanceRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距", meta = (DisplayName = "交火时主动散开"))
	bool bEnableCombatSpread = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距", meta = (DisplayName = "友军期望间距（厘米）", ClampMin = "120.0", EditCondition = "bEnableCombatSpread"))
	float FriendlySpacingDistance = 180.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距", meta = (DisplayName = "分散单次最大位移（厘米）", ClampMin = "1.0", EditCondition = "bEnableCombatSpread"))
	float CombatSpreadStepDistance = 250.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距", meta = (DisplayName = "分散移动速度（厘米/秒）", ClampMin = "1.0", EditCondition = "bEnableCombatSpread"))
	float CombatSpreadMoveSpeed = 180.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|战斗|间距", meta = (DisplayName = "分散冷却（秒）", ClampMin = "0.5", EditCondition = "bEnableCombatSpread"))
	float CombatSpreadCooldownSeconds = 2.0f;
};
