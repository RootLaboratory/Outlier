#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "EMPableComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UEMPableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEMPableComponent();

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
	void RemoveEMPTag(FGameplayTag Tag);
};
