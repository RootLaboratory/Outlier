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
class UAbilitySlotUI;
class UCanvasPanel;
class UDamageFeedbackLayer;
class UWidget;
class UInfoCanvasBase;

UCLASS()
class TAGDRIVENUI_API UMainUIBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UEventDrivenUI* GetModule(const FGameplayTag& ModuleTag) const;
	UAbilityIconUI* GetAbilityIcon(const FGameplayTag& AbilityTag) const;
	UAbilitySlotUI* GetAbilitySlot(const FGameplayTag& AbilityTag) const;
	void AddAbilityIconEntry(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag);
	void ResetAbilityCooldowns();
	// 재사용할 HUD에 이전 피격 표시와 Info 요청이 남지 않도록 비운다.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void FlushTransientWidgets();
	virtual void On_RepAbilityDisabledByDistance();
	virtual void On_RepAbilityabledByDistance();

	virtual void ModulesControl(bool Flag);
	/** HUD 비활성화 시 레이어를 숨긴 뒤 호출된다. 사망 가시성에는 실행되지 않는다. */
	virtual void ApplyExceptionVisibility() {}

	// 개별 모듈 상태는 부모 Layer의 가시성과 독립적으로 유지한다.
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
	// MainUI 가시성은 바꾸지 않고 예외 위젯의 브랜치를 DefaultLayer까지 연다.
	void ShowExceptionLayer(UWidget* Layer);
	void RegisterModule(UEventDrivenUI* InModule);
	void RegisterModule(const FGameplayTag& ModuleTag, UEventDrivenUI* InModule);
	void RegisterAbilityIcon(UAbilityIconUI* Icon, const FGameplayTag& AbilityTag, bool bUnlock = false);
	void RegisterAbilitySlot(UAbilitySlotUI* AbilitySlot, const FGameplayTag& AbilityTag);

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UAbilitySlotUI>> AbilitySlots;

	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UEventDrivenUI>> Modules;

	// DefaultLayer 직속 Layer의 전역 가시성 상태. 개별 모듈의 활성 상태와는 별개다.
	bool bModulesActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ability Sections")
	TMap<FGameplayTag, TObjectPtr<UAbilityIconUI>> AbilitySections;

public:
	// DefaultLayer 직속 InfoCanvas WBP(InfoCanvasWidget 파생). 다른 Layer를 닫아도 예외로 남고,
	// 영역별 가시성은 내부 Queue Widget이 각자 관리한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI|Info")
	TObjectPtr<UInfoCanvasBase> InfoLayer;

	// 루트 캔버스에 미리 배치해두고 재사용하는 피격 방향 피드백 레이어.
	// 기존 WBP 호환 작업 전까지는 선택적으로 바인딩한다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UDamageFeedbackLayer> DamageFeedbackLayer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> DefaultLayer;
};
