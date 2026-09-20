#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoomCombatSpawnPoint.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;
class UTextRenderComponent;
class AEnemyBase;

UCLASS()
class OUTLIER_API ARoomCombatSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ARoomCombatSpawnPoint();
	virtual void OnConstruction(const FTransform& Transform) override;

	FGameplayTag GetRoomTag() const { return RoomTag; }
	const FGameplayTagContainer& GetSpawnPointTags() const { return SpawnPointTags; }
	float GetSpawnWeight() const { return SpawnWeight; }
	float GetSpawnRadius() const { return SpawnRadius; }
	FGameplayTag GetActivationGroupTag() const { return ActivationGroupTag; }
	bool IsRuntimeActive() const { return bRuntimeActive; }
	void SetRuntimeActive(bool bActive);
	bool FindSpawnTransform(
		TSubclassOf<AEnemyBase> EnemyClass,
		int32 SearchSeed,
		FTransform& OutSpawnTransform) const;

#if WITH_DEV_AUTOMATION_TESTS
	void SetForceSpawnLocationFailureForTesting(bool bEnabled)
	{
		bForceSpawnLocationFailureForTesting = bEnabled;
	}
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Combat|Visual")
	TObjectPtr<UStaticMeshComponent> SpawnPointMesh;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Room Combat|Debug")
	TObjectPtr<USphereComponent> SpawnRadiusPreview;

	UPROPERTY(VisibleAnywhere, Category = "Room Combat|Debug")
	TObjectPtr<UTextRenderComponent> SpawnInfoPreview;
#endif

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn")
	FGameplayTagContainer SpawnPointTags;

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float SpawnWeight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SpawnRadius = 300.0f;

	// 배치 Actor는 바닥에 두고, 실제 Enemy Actor 원점은 이 높이만큼 올려 탐색한다.
	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpawnHeightOffset = 150.0f;

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn")
	bool bInitiallyActive = true;

	UPROPERTY(EditAnywhere, Category = "Room Combat|Spawn")
	FGameplayTag ActivationGroupTag;

private:
	// 에디터 기본값과 해킹 등 런타임 활성화를 분리한다. 다음 WP 로드에서는 다시 기본값으로 시작한다.
	bool bRuntimeActive = true;

#if WITH_DEV_AUTOMATION_TESTS
	bool bForceSpawnLocationFailureForTesting = false;
#endif
};
