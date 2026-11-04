#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WidgetWeaponType.h"
#include "ShooterCrosshairWidget.generated.h"

class UCrossHairBase;

/**
 * 슈트 HUD 레이어와 독립적으로 무기 선택과 슈트 사용 조건을 관리한다.
 * 이 클래스를 상속하는 WBP에 RifleCrossHairUI와 PistolCrossHairUI를 배치한다.
 * 기존 조준점 위젯의 슬롯 설정을 유지하며 전체 화면 Canvas 안으로 옮긴다.
 * Main WBP의 DefaultLayer 바로 아래에 이 WBP를 CrosshairLayer라는 이름으로 배치하고,
 * 전체 화면 앵커와 0 오프셋을 적용한다. 기존 Main 직속 조준점은 제거한다.
 */
UCLASS(Blueprintable)
class TAGDRIVENUI_API UShooterCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetWeaponType(EWidgetWeaponType Type);
	void SetSuitAcquired(bool bAcquired);
	void ResetRifleCrosshair();
	UCrossHairBase* GetCurrentCrosshair() const;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCrossHairBase> RifleCrossHairUI;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCrossHairBase> PistolCrossHairUI;

private:
	void RefreshPresentation();
	UPROPERTY(Transient)
	TObjectPtr<UCrossHairBase> ActiveCrosshair;
	bool bPresentationInitialized = false;
	EWidgetWeaponType WeaponType = EWidgetWeaponType::Unarmed;
	bool bSuitAcquired = false;
};
