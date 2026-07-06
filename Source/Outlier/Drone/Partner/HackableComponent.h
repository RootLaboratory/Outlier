#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "HackableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackStarted, const FHackQueryContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackCompleted, const FHackResultContext&, Context);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHackEffectTriggered,
	FGameplayTag, EffectTag,
	const FHackResultContext&, Context
);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UHackableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHackableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Hack")
	FGameplayTagContainer HackTags;  // Target/State/Query

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Effect", meta = (Categories = "Hack.Effect"))
	FGameplayTagContainer SuccessEffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Effect", meta = (Categories = "Hack.Effect"))
	FGameplayTagContainer FailEffectTags;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackStarted OnHackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackCompleted OnHackCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackEffectTriggered OnHackEffectTriggered;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool CanBeHackTarget(const FHackQueryContext& Context) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool MatchesHackQuery(const FGameplayTagQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void BeginHack(const FHackQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CompleteHack(const FHackResultContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool HasHackTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackTargetType() const;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastTriggerHackEffects(const FGameplayTagContainer& EffectTags, const FHackResultContext& Context);


private:
	const FGameplayTagContainer& ResolveHackEffectTags(EHackResult Result) const;

};
