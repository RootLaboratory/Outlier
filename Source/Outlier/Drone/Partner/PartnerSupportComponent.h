// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "PartnerSupportComponent.generated.h"


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

public:	
	void TryHack_Server(AActor* TargetActor);
	void TryAreaOfEffect_Server();
	void TryScan_Server();
	void TryShield_Server();

private:
	TMap<FName, float> LastUseTimes;

	FTimerHandle ShieldTimerHandle;
	FTimerHandle ScanTimerHandle;

	float ShieldElapsedTime = 0.0f;
	float CurrentScanRadius = 0.0f;

	TArray<TObjectPtr<AActor>> PendingScanActors;
	TArray<TObjectPtr<AActor>> ScannedActors;

private:
	bool CanUseSkill_Server(FName SkillName, float CoolDown) const;
	void MarkSkillUsed(FName SkillName);

	AActor* FindTarget(float Range) const;
	void EndShield_Server();

	void UpdateScan_Server();
	void EndScan_Server();

	bool CanUseShield() const;
	void NotifySkillResult(EPartnerSkillType SkillType, EPartnerSkillUseResult Result) const;

	bool IsInsideView(AActor* Actor) const;
	bool HasLineOfSight(AActor* Actor) const;

	void ApplyScanEffect(AActor* Actor);
	void ClearScanEffect(AActor* Actor);

	void ApplyAreaOfEffect(AActor* Actor);
};
