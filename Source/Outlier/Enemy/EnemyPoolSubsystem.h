#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyPoolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyPoolSubsystem.generated.h"

class AEnemyBase;
class UEnemyPoolDefinition;

UCLASS()
class OUTLIER_API UEnemyPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	bool PrewarmPool(UEnemyPoolDefinition* Definition);
	AEnemyBase* LeaseEnemy(
		TSubclassOf<AEnemyBase> EnemyClass,
		const FTransform& SpawnTransform,
		const FEnemyPoolLeaseContext& Context);
	bool ReturnEnemy(AEnemyBase* Enemy, int32 GameplayGeneration, int32 LeaseSerial);
	void DestroyPool();

	int32 GetTotalCount(TSubclassOf<AEnemyBase> EnemyClass) const;
	int32 GetIdleCount(TSubclassOf<AEnemyBase> EnemyClass) const;
	int32 GetLeasedCount(TSubclassOf<AEnemyBase> EnemyClass) const;

private:
	struct FEnemyPoolBucket
	{
		TSubclassOf<AEnemyBase> EnemyClass;
		int32 MaxCount = 0;
		TArray<TWeakObjectPtr<AEnemyBase>> IdleEnemies;
		TSet<TWeakObjectPtr<AEnemyBase>> LeasedEnemies;
	};

	bool PrewarmConfiguredPool();
	AEnemyBase* CreatePoolEnemy(FEnemyPoolBucket& Bucket);
	void CompactBucket(FEnemyPoolBucket& Bucket) const;
	void HandleArenaReady();
	void HandleArenaGameplayReady(uint32 GameplayGeneration);
	void HandleArenaGameplayReloadStarted(uint32 GameplayGeneration);
	void HandleArenaReleased();

	TMap<TSubclassOf<AEnemyBase>, FEnemyPoolBucket> Buckets;
	TWeakObjectPtr<UEnemyPoolDefinition> ActiveDefinition;
	int32 NextLeaseSerial = 0;
};
