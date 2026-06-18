// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "OutlierAbilityComponent.generated.h"

UENUM(BlueprintType)
enum class EOutlierAbilityResult : uint8
{
	Success,
	InvalidAbility,
	Locked,
	Cooldown,
	NoHandler,
	DistanceRestricted,
	ExecutionFailed
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierAbilityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Ability"))
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Cooldown"))
	FGameplayTag CooldownTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (Categories = "Input"))
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0"))
	float RangeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	uint8 bDefaultLocked : 1 = false;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierCooldownState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cooldown")
	float EndTime = 0.0f;

	FTimerHandle TimerHandle;
};

DECLARE_DELEGATE_RetVal_OneParam(EOutlierAbilityResult, FOutlierAbilityExecuteDelegate, const FOutlierAbilityRow&);

UCLASS(ClassGroup = (Ability), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UOutlierAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOutlierAbilityComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void RebuildAbilityCache();

	UFUNCTION(BlueprintCallable, Category = "Ability")
	EOutlierAbilityResult TryActivateAbilityByTag(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool CanActivateAbilityByTag(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	EOutlierAbilityResult GetActivationFailureReason(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsOnCooldown(FGameplayTag CooldownTag) const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	float GetCooldownRemaining(FGameplayTag CooldownTag) const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsAbilityLocked(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void LockAbility(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void UnlockAbility(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void ClearAbilityLockOverride(FGameplayTag AbilityTag);

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool HasRuntimeTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void AddRuntimeTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Ability")
	void RemoveRuntimeTag(FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "Ability")
	FGameplayTag ResolveAbilityTagFromInputTag(FGameplayTag InputTag) const;

	const FOutlierAbilityRow* FindAbilityRowByAbilityTag(FGameplayTag AbilityTag) const;
	const FOutlierAbilityRow* FindAbilityRowByInputTag(FGameplayTag InputTag) const;

	void RegisterAbilityHandler(FGameplayTag AbilityTag, FOutlierAbilityExecuteDelegate Handler);
	void UnregisterAbilityHandler(FGameplayTag AbilityTag);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UDataTable> AbilityDataTable;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTagContainer RuntimeTags;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTagContainer RuntimeLockedAbilities;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTagContainer RuntimeUnlockedAbilities;

	virtual void InitializeAbilityHandlers();
	virtual EOutlierAbilityResult ExecuteAbilityInternal(const FOutlierAbilityRow& AbilityRow);
	virtual void CommitCooldown(const FOutlierAbilityRow& AbilityRow);

	void ClearCooldownTag(FGameplayTag CooldownTag);

private:
	TMap<FGameplayTag, FOutlierAbilityRow> AbilityRowsByAbilityTag;
	TMap<FGameplayTag, FGameplayTag> AbilityTagsByInputTag;
	TMap<FGameplayTag, FOutlierAbilityExecuteDelegate> AbilityHandlers;
	TMap<FGameplayTag, FOutlierCooldownState> CooldownsByCooldownTag; //CoolTime
};
