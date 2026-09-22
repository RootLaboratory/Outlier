// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "DamageFeedBackWidget.generated.h"

class UImage;
class UVerticalBox;
class AActor;

/**
 * Directional damage indicator shared by the Shooter and Partner HUDs.
 * The WBP art should face up at zero degrees; the widget applies the hit direction as render rotation.
 */
UCLASS(Abstract, Blueprintable)
class TAGDRIVENUI_API UDamageFeedBackWidget : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ShowDamageFeedback(AActor* InCharacter, const FVector& InDamageOrigin);

	UFUNCTION(BlueprintCallable, Category = "Damage Feedback")
	void ClearDamageFeedback();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Red/Shield 이미지를 함께 회전시키는 공통 루트.
	// Render Transform Pivot은 WBP에서 설정한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UVerticalBox> Root; //구조를 내가 이해 못해서 수정 예정.

	// 피격 시 ShieldFeedback과 함께 갱신되는 Red 레이어.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> RedFeedback;

	// WBP에서 임시 제거할 수 있도록 선택적으로 바인딩한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ShieldFeedback;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage Feedback", meta = (ClampMin = "0.0"))
	float FeedbackDuration = 0.5f;

	// 피드백이 활성화된 동안에만 호출한다. 이후 Material 효과도 C++에서 여기 처리한다.
	void HandleDamageFeedbackTick(float NormalizedElapsedTime);

private:
	bool UpdateFeedbackRotation();
	void SetFeedbackRotation(float RenderAngle);

	TWeakObjectPtr<AActor> FeedbackCharacter;
	FVector DamageOrigin = FVector::ZeroVector;
	bool bFeedbackActive = false;
	float FeedbackElapsed = 0.0f;
};
