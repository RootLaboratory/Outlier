#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "HackType.h"
#include "HackableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackStarted, const FHackQueryContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackCompleted, const FHackResultContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackProcessChanged, const FHackProcessContext&, Context);

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	FGameplayTagContainer HackTags;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|UI")
	uint8 bProjectWorldLocationToScreen : 1 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|UI")
	FVector2D LastProjectedScreenLocation = FVector2D::ZeroVector;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackStarted OnHackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackCompleted OnHackCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackProcessChanged OnHackProcessChanged;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackEffectTriggered OnHackEffectTriggered;

public:

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool CanBeHackTarget(const FHackQueryContext& Context) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool MatchesHackQuery(const FGameplayTagQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void BeginHack(const FHackQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void CompleteHack(const FHackResultContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void SetHackProcess(const FHackProcessContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	void SetProjectedScreenLocation(const FVector2D& ScreenLocation);

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool HasHackTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintCallable, Category = "Hack")
	bool IsHackTargetType() const;
};
