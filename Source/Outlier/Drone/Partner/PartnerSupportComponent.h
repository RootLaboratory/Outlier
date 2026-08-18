// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerSupportComponent.generated.h"

class APartnerShieldSphere;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class OUTLIER_API UPartnerSupportComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UPartnerSupportComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	void TryHack_Server(AActor* TargetActor);
	void TryAreaOfEffect_Server();
	void TryScan_Server();
	void TryShield_Server();
	void CancelForReboot();

private:
	static constexpr int32 DefaultScanStencilValue = 0;
	static constexpr float ScanUpdateInterval = 0.03f;

	UPROPERTY(EditDefaultsOnly, Category = "Shield")
	TSubclassOf<APartnerShieldSphere> ShieldActorClass;

	UPROPERTY(EditAnywhere, Category = "Shield|Debug")
	uint8 bDebugShieldActorSpawn : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Shield|Debug", meta = (EditCondition = "bDebugShieldActorSpawn", ClampMin = "0.0"))
	float ShieldActorDebugLifeTime = 3.0f;

	UPROPERTY(Transient)
	TObjectPtr<APartnerShieldSphere> ActiveShieldActor;

	TMap<FName, float> LastUseTimes;

	FTimerHandle ShieldMonitorTimerHandle;
	FTimerHandle ScanServerStateTimerHandle;
	FTimerHandle ScanClientUpdateTimerHandle;

	float CurrentScanRadius = 0.0f;
	float ScanElapsedTime = 0.0f;
	float ActiveScanRange = 0.0f;
	float ActiveScanDuration = 0.0f;

	TArray<TObjectPtr<AActor>> ScannedActors;

	FVector ScanOrigin = FVector::ZeroVector;

private:
	bool CanUseSkill_Server(FName SkillName, float CoolDown) const;
	void MarkSkillUsed(FName SkillName);

	AActor* FindTarget(float Range) const;
	void SpawnShieldActor_Server(AShooterCharacter* Shooter);
	void DestroyShieldActor_Server();
	void UpdateShield_Server();
	void EndShield_Server();

	void EndScan_Server();
	void UpdateScan_Client();
	void EndScan_Client();

	bool CanUseShield() const;
	void NotifySkillResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result) const;

	bool IsInsideView(AActor* Actor) const;
	bool HasLineOfSight(AActor* Actor) const;
	int32 ResolveScanStencilValue(AActor* Actor) const;

	void ApplyScanEffect(AActor* Actor);
	void ClearScanEffect(AActor* Actor);
	bool ShouldProcessLocalScanVisual() const;

	void ApplyAreaOfEffect(AActor* Actor);

	UFUNCTION(Client, Reliable)
	void ClientStartScanVisual(FVector InScanOrigin, float InScanRange, float InScanDuration);

	UFUNCTION(Client, Reliable)
	void ClientCancelScanForReboot();
};
