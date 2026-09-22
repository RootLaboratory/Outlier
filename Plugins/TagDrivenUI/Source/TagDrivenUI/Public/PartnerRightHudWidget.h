// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PartnerRightHudWidget.generated.h"

class UAbilityIconUI;

/** 모듈화된 Partner 오른쪽 HUD가 네 능력 아이콘의 바인딩을 소유한다. */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UPartnerRightHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAbilityIconUI* GetAbilityShieldIcon() const { return AbilityShieldIcon; }
	UAbilityIconUI* GetAbilityHackingIcon() const { return AbilityHackingIcon; }
	UAbilityIconUI* GetAbilityScanIcon() const { return AbilityScanIcon; }
	UAbilityIconUI* GetAbilityEMPIcon() const { return AbilityEMPIcon; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityShieldIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityHackingIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityScanIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UAbilityIconUI> AbilityEMPIcon;
};
