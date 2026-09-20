#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyAdaptationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyAdaptationSubsystem.generated.h"

class AEnemyBase;
class UEnemyAdaptationDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnEnemyAdaptationUpdated,
	const FEnemyAdaptationUpdateResult&);

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
	int32 GetCurrentGunAdaptationStack() const { return CurrentGunAdaptationStack; }
	EEnemyAdaptationState GetCurrentAdaptationState() const { return CurrentAdaptationState; }

	// Save/Load와 Checkpoint 복원에서 공용 Stack만 되돌릴 때 사용한다.
	// 활성 Enemy 목록은 별도 런타임 수명이므로 이 API에서 다시 만들지 않는다.
	bool SetGunAdaptationStack(int32 NewStack);

	// 처치가 발생한 시점의 Generation/Lease를 함께 검증해 Pool Actor가 재사용된 뒤
	// 늦게 도착한 이전 처치 이벤트가 새 개체의 Stack을 변경하지 못하게 한다.
	bool ReportEnemyDefeat(
		AEnemyBase* Enemy,
		int32 GameplayGeneration,
		int32 PoolLeaseSerial,
		EEnemyFinalKillCategory KillCategory,
		FEnemyAdaptationUpdateResult& OutResult);

	FOnEnemyAdaptationUpdated OnAdaptationUpdated;

#if WITH_DEV_AUTOMATION_TESTS
	bool IsAcceptingRegistrationsForTesting() const { return bAcceptingRegistrations; }
	void SetDefinitionForTesting(UEnemyAdaptationDefinition* Definition);
#endif

private:
	struct FEnemyRegistration
	{
		int32 GameplayGeneration = 0;
		int32 PoolLeaseSerial = 0;
		bool bDefeatReported = false;
	};

	bool CanRegisterEnemy(const AEnemyBase* Enemy) const;
	bool IsCurrentRegistration(
		const AEnemyBase* Enemy,
		const FEnemyRegistration& Registration,
		int32 GameplayGeneration,
		int32 PoolLeaseSerial) const;
	void RefreshResolvedState();
	void ResetAdaptationState();
	void LoadConfiguredDefinition();
	void HandleArenaShown();
	void HandleArenaGameplayReloadStarted(uint32 GameplayGeneration);
	void HandleArenaGameplayGCReady(uint32 GameplayGeneration);
	void HandleArenaGameplayReady(uint32 GameplayGeneration);
	void HandleArenaReleased();

	TMap<TWeakObjectPtr<AEnemyBase>, FEnemyRegistration> ActiveEnemies;
	UPROPERTY(Transient)
	TObjectPtr<UEnemyAdaptationDefinition> ActiveDefinition;
	int32 CurrentGunAdaptationStack = 0;
	EEnemyAdaptationState CurrentAdaptationState = EEnemyAdaptationState::Normal;
	uint32 ActiveGameplayGeneration = 0;
	bool bAcceptingRegistrations = false;
};
