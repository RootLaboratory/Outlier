// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EventDrivenUI.h"
#include "PartnerHPUI.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerHPUI : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetShooterCondition(FGameplayTag InConditionTag);

	UFUNCTION(BlueprintPure, Category = "UI")
	FGameplayTag GetShooterCondition() const;

	void RefreshShooterConditionUI();
private:
	void InitializeShooterConditionMaterial();
	void UpdateShooterConditionMaterial();
	float GetShooterConditionMaterialValue(const FGameplayTag& InConditionTag) const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	TObjectPtr<UMaterialInterface> ShooterConditionMaterial;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UIMaterial")
	TObjectPtr<UMaterialInstanceDynamic> ShooterConditionMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIMaterial")
	FName ShooterConditionParameterName = TEXT("condition");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (Categories = "UI.Condition.Shooter"))
	FGameplayTag DefaultShooterConditionTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TMap<FGameplayTag, float> ConditionMaterialValues;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShooterConditionUI;

private:
	UPROPERTY(BlueprintReadOnly, Category = "Data", meta = (AllowPrivateAccess = "true", Categories = "UI.Condition.Shooter"))
	FGameplayTag CurShooterConditionTag;
};
