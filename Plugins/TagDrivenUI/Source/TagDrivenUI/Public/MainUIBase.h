// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MainUIBase.generated.h"

/**
 * 
 */

// UI 전용 Player State
// Controller에 BroadCast 받아놓아서, Sub에 처리하도록 함. 
UENUM(BlueprintType)
enum class EUIPlayerState : uint8
{
	Idle,
	Move,
	Jump,
	Slide
};

class UEventDrivenUI;
class UAbilityIconUI;
class UCanvasPanel;

UCLASS()
class TAGDRIVENUI_API UMainUIBase : public UUserWidget
{
	GENERATED_BODY()

public:

	UEventDrivenUI* GetModule(const FGameplayTag& ModuleTag) const;
	UAbilityIconUI* GetAbilityIcon(const FGameplayTag& AbilityTag) const;
	void AddAbilityIconEntry(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag);
	void ResetAbilityCooldowns();
	virtual void On_RepAbilityDisabledByDistance();
	virtual void On_RepAbilityabledByDistance();

	virtual void ModulesControl(bool Flag);

	// 게이트를 존중하는 활성화. bModulesActive 가 false 면 무시한다.
	// 모듈을 개별로 켜는 곳(무기 타입별 Ammo/CrossHair 등)이 ->Activate() 를 직접 부르면
	// MainWidget 전체 게이트를 우회해버리므로, 그런 자리에서는 이걸 쓴다.
	void ActivateModuleIfAllowed(UEventDrivenUI* InModule);
	virtual void AbilitySectionControl(bool Flag);
public:

	virtual void ModuleInit() {}
	virtual void ModuleDestruct() {}
	virtual void ModuleActivate() {}
	virtual void ModuleDeActivate() {}
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	void RegisterModule(UEventDrivenUI* InModule);
	void RegisterModule(const FGameplayTag& ModuleTag, UEventDrivenUI* InModule);
	void RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock = false);

	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UEventDrivenUI>> Modules;

	// ModulesControl 이 마지막으로 지시한 상태. 등록이 늦은 모듈에도 같은 상태를 물려준다.
	bool bModulesActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ability Sections")
	TMap<FGameplayTag, TObjectPtr<UAbilityIconUI>> AbilitySections;

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> InteractionLayer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> DefaultLayer;
};
