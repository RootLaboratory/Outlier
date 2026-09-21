#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TurretReinforcementHatch.generated.h"

class AAutoTurret;
class USceneComponent;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class ETurretReinforcementHatchState : uint8
{
	DormantClosed UMETA(DisplayName = "Dormant Closed"),
	Deploying UMETA(DisplayName = "Deploying"),
	Active UMETA(DisplayName = "Active"),
	WaitingForTurretDestruction UMETA(DisplayName = "Waiting For Turret Destruction"),
	Closing UMETA(DisplayName = "Closing"),
	ConsumedClosed UMETA(DisplayName = "Consumed Closed")
};

UCLASS()
class OUTLIER_API ATurretReinforcementHatch : public AActor
{
	GENERATED_BODY()

public:
	ATurretReinforcementHatch();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	int32 GetCombatPhaseIndex() const { return CombatPhaseIndex; }
	int32 GetWaveIndex() const { return WaveIndex; }
	AAutoTurret* GetLinkedTurret() const { return LinkedTurret; }
	ETurretReinforcementHatchState GetHatchState() const { return HatchState; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_DEV_AUTOMATION_TESTS
	void ConfigureForTesting(
		FGameplayTag InRoomTag,
		int32 InCombatPhaseIndex,
		int32 InWaveIndex,
		AAutoTurret* InLinkedTurret)
	{
		RoomTag = InRoomTag;
		CombatPhaseIndex = InCombatPhaseIndex;
		WaveIndex = InWaveIndex;
		LinkedTurret = InLinkedTurret;
	}
#endif

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Combat|Turret Hatch")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Combat|Turret Hatch")
	TObjectPtr<USkeletalMeshComponent> HatchMesh;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Turret Hatch", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Turret Hatch", meta = (ClampMin = "0", UIMin = "0"))
	int32 CombatPhaseIndex = 0;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Turret Hatch", meta = (ClampMin = "0", UIMin = "0"))
	int32 WaveIndex = 0;

	// 터렛 종류는 별도 enum이 아니라 맵에 연결한 BP 인스턴스로 결정한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Turret Hatch")
	TObjectPtr<AAutoTurret> LinkedTurret;

	UPROPERTY(ReplicatedUsing = OnRep_HatchState, VisibleInstanceOnly, BlueprintReadOnly,
		Category = "Room Combat|Turret Hatch")
	ETurretReinforcementHatchState HatchState =
		ETurretReinforcementHatchState::DormantClosed;

	UFUNCTION()
	void OnRep_HatchState(ETurretReinforcementHatchState PreviousState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Room Combat|Turret Hatch")
	void OnTurretHatchStateChanged(
		ETurretReinforcementHatchState PreviousState,
		ETurretReinforcementHatchState NewState);

private:
	void PrepareLinkedTurretForWave();
	void ApplyHatchStatePresentation(ETurretReinforcementHatchState PreviousState);
};
