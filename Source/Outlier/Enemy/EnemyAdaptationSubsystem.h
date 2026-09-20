#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyAdaptationSubsystem.generated.h"

class AEnemyBase;
class UEnemyAdaptationDefinition;

UCLASS()
class OUTLIER_API UEnemyAdaptationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	bool RegisterEnemy(AEnemyBase* Enemy);
	void UnregisterEnemy(AEnemyBase* Enemy);
	void ResetActiveEnemies();

	bool IsEnemyRegistered(AEnemyBase* Enemy) const;
	int32 GetRegisteredEnemyCount() const;
	const UEnemyAdaptationDefinition* GetDefinition() const { return ActiveDefinition.Get(); }

#if WITH_DEV_AUTOMATION_TESTS
	bool IsAcceptingRegistrationsForTesting() const { return bAcceptingRegistrations; }
#endif

private:
	struct FEnemyRegistration
	{
		int32 GameplayGeneration = 0;
		int32 PoolLeaseSerial = 0;
	};

	bool CanRegisterEnemy(const AEnemyBase* Enemy) const;
	void LoadConfiguredDefinition();
	void HandleArenaShown();
	void HandleArenaGameplayReloadStarted(uint32 GameplayGeneration);
	void HandleArenaGameplayGCReady(uint32 GameplayGeneration);
	void HandleArenaGameplayReady(uint32 GameplayGeneration);
	void HandleArenaReleased();

	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyRegistration> ActiveEnemies;
	UPROPERTY(Transient)
	TObjectPtr<UEnemyAdaptationDefinition> ActiveDefinition;
	uint32 ActiveGameplayGeneration = 0;
	bool bAcceptingRegistrations = false;
};
