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
class UDamageFeedbackLayer;
class UWidget;

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

	// 개별 모듈 상태는 ModuleLayer의 전역 가시성과 독립적으로 유지한다.
	// 전역 HUD가 닫힌 동안에도 무기 변경 등의 최신 상태를 기록해두기 위함이다.
	void SetModuleActive(UEventDrivenUI* InModule, bool bActive);
	virtual void AbilitySectionControl(bool Flag);
public:

	virtual void ModuleInit() {}
	virtual void ModuleDestruct() {}
	virtual void ModuleActivate();
	virtual void ModuleDeActivate();
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	void RegisterModule(UEventDrivenUI* InModule);
	void RegisterModule(const FGameplayTag& ModuleTag, UEventDrivenUI* InModule);
	void RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock = false);

	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UEventDrivenUI>> Modules;

	// ModuleLayer의 전역 가시성 상태. 개별 모듈의 활성 상태와는 별개다.
	bool bModulesActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ability Sections")
	TMap<FGameplayTag, TObjectPtr<UAbilityIconUI>> AbilitySections;

public:
	// 선택적 전역 가시성 루트. 지정하지 않으면 기존 DefaultLayer를 그대로 사용한다.
	// 컨테이너 타입은 Overlay/CanvasPanel/SizeBox 중 무엇이든 가능하다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ModuleLayer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> InteractionLayer;

	// 루트 캔버스에 미리 배치해두고 재사용하는 피격 방향 피드백 레이어.
	// 기존 WBP 호환 작업 전까지는 선택적으로 바인딩한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDamageFeedbackLayer> DamageFeedbackLayer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> DefaultLayer;
};
