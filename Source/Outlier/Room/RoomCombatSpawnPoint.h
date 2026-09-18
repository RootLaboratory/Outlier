#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomCombatSpawnPoint.generated.h"

class USceneComponent;

UCLASS()
class OUTLIER_API ARoomCombatSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ARoomCombatSpawnPoint();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	const FGameplayTagContainer& GetSpawnPointTags() const { return SpawnPointTags; }
	float GetSpawnWeight() const { return SpawnWeight; }
	float GetSpawnRadius() const { return SpawnRadius; }
	FGameplayTag GetActivationGroupTag() const { return ActivationGroupTag; }
	bool IsRuntimeActive() const { return bRuntimeActive; }
	void SetRuntimeActive(bool bActive);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn")
	FGameplayTagContainer SpawnPointTags;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float SpawnWeight = 1.0f;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SpawnRadius = 300.0f;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn")
	bool bInitiallyActive = true;

	UPROPERTY(EditInstanceOnly, Category = "Room Combat|Spawn")
	FGameplayTag ActivationGroupTag;

private:
	// 에디터 기본값과 해킹 등 런타임 활성화를 분리한다. 다음 WP 로드에서는 다시 기본값으로 시작한다.
	bool bRuntimeActive = true;
};
