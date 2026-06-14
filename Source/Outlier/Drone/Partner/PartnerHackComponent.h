// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/PartnerCharacterComponentBase.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "PartnerHackComponent.generated.h"

namespace PartnerAbilityComponentTags
{
	inline FGameplayTag EMP()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.EMP")));
		return Tag;
	}

	inline FGameplayTag Shield()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Shield")));
		return Tag;
	}

	inline FGameplayTag Hacking()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Hacking")));
		return Tag;
	}

	inline FGameplayTag Scan()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Partner.Scan")));
		return Tag;
	}
}

class UHackableComponent;
class UHackCandidateLayerWidget;
class UHackCandidateMarkerWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartnerHackCandidateAdded, AActor*, TargetActor, UHackableComponent*, HackableComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartnerHackCandidateRemoved, AActor*, TargetActor, UHackableComponent*, HackableComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartnerHackStarted, AActor*, TargetActor, UHackableComponent*, HackableComponent);

UCLASS()
class OUTLIER_API UPartnerHackComponent : public UPartnerCharacterComponentBase
{
	GENERATED_BODY()

public:
	UPartnerHackComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	float CandidateRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CandidateHalfAngleDegrees = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	uint8 bRequireLineOfSight : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Debug")
	uint8 bDebugHack : 1 = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer RequiredCandidateTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Candidate")
	FGameplayTagContainer BlockedCandidateTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateLayerWidget> CandidateLayerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hack|UI")
	TSubclassOf<UHackCandidateMarkerWidget> CandidateMarkerWidgetClass;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnPartnerHackCandidateAdded OnHackCandidateAdded;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnPartnerHackCandidateRemoved OnHackCandidateRemoved;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnPartnerHackStarted OnHackStarted;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool TryHack();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void RefreshHackCandidates();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool TryStartHack(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CompleteHack(const FHackResultContext& ResultContext);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void ClearHackCandidates();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void StopHackCandidateSearch();

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackCandidateSearchActive() const { return bHackCandidateSearchActive; }

	UFUNCTION(BlueprintCallable, Category = "Hack")
	const TArray<AActor*>& GetHackCandidateActors() const { return HackCandidateActors; }

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHackableComponent>> HackCandidateComponents;

	UPROPERTY(Transient)
	TObjectPtr<UHackableComponent> ActiveHackableComponent;

	UPROPERTY(Transient)
	TObjectPtr<UHackCandidateLayerWidget> CandidateLayerWidget;

private:
	TArray<AActor*> HackCandidateActors;

	uint8 bHackCandidateSearchActive : 1 = false;
	int32 LastDebugCandidateCount = INDEX_NONE;

	FHackQueryContext BuildQueryContext() const;
	UHackableComponent* ResolveHackableComponent(AActor* Actor) const;
	bool IsCandidateActorValid(AActor* Actor, UHackableComponent* HackableComponent, const FHackQueryContext& Context, FVector2D& OutScreenLocation) const;
	bool IsInsidePartnerFrustum(AActor* Actor, FVector2D& OutScreenLocation) const;
	bool HasLineOfSight(AActor* Actor) const;
	void EnsureCandidateLayerWidget();
	void DestroyCandidateLayerWidget();
	void ApplyCandidateInputMode();
	void RestoreGameInputMode();
	void AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation);
	void RemoveHackCandidateAt(int32 Index);
	void DeactivateUnselectedCandidates(UHackableComponent* SelectedComponent);
	
};
