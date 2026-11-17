#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DustShotDamageReceiptLibrary.generated.h"

class AActor;
class UDustEncounterComponent;

/** Synchronous receipt from the existing damage receiver; does not apply damage. */
UCLASS()
class DUST2_API UDustShotDamageReceiptLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="战斗|命中反馈", meta=(DisplayName="Report Applied Shot Damage"))
    static void ReportAppliedShotDamage(AActor* DamagedActor, float PreviousHealth,
        float CurrentHealth, bool bWasAlreadyDead);
};

namespace DustShotDamageReceipt
{
    /** Only a first real removal from a live encounter can record a death. */
    void RecordEncounterDeath(const AActor* Enemy, UDustEncounterComponent* Encounter);

    struct FResult
    {
        bool bConfirmed = false;
        float ActualDamage = 0.0f;
        bool bKilled = false;
    };

    /** Scoped to one synchronous ReceiveDamage dispatch, including nested dispatches. */
    class DUST2_API FScopedCapture final
    {
    public:
        explicit FScopedCapture(const AActor* ExpectedTarget);
        ~FScopedCapture();
        FScopedCapture(const FScopedCapture&) = delete;
        FScopedCapture& operator=(const FScopedCapture&) = delete;
        FResult GetResult() const { return Result; }
        const TWeakObjectPtr<UDustEncounterComponent>& GetDeathEncounter() const { return DeathEncounter; }
        uint64 GetDeathSerial() const { return DeathSerial; }
        void RecordDeath(const AActor* Enemy, UDustEncounterComponent* Encounter);
        void Report(const AActor* Target, float PreviousHealth, float CurrentHealth, bool bWasAlreadyDead);
    private:
        const AActor* ExpectedTarget = nullptr;
        FScopedCapture* PreviousCapture = nullptr;
        FResult Result;
        TWeakObjectPtr<UDustEncounterComponent> DeathEncounter;
        uint64 DeathSerial = 0;
        bool bInstalled = false;
    };
}
