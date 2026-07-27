#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "EMPableComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UEMPableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEMPableComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "EMP")
	FGameplayTagContainer EMPTags;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool IsEMPTargetType() const;

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool CanBeEMPTarget(const FGameplayTagContainer& RequiredTags, const FGameplayTagContainer& BlockedTags) const;

	UFUNCTION(BlueprintCallable, Category = "EMP")
	bool HasEMPTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void AddEMPTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void ApplyEMPTagForDuration(FGameplayTag Tag, float Duration);

	UFUNCTION(BlueprintCallable, Category = "EMP")
	void RemoveEMPTag(FGameplayTag Tag);

private:
	TMap<FGameplayTag, FTimerHandle> EMPDurationTimerHandles;

	void HandleEMPTagDurationExpired(FGameplayTag Tag);
	void ClearEMPDurationTimer(FGameplayTag Tag);
};
