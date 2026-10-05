// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageFeedbackIndicator.generated.h"

class AActor;
class UCurveFloat;
class UImage;
class UMaterialInstanceDynamic;

/**
 * Pooled directional damage indicator owned by UDamageFeedbackLayer.
 * The WBP art should face up at zero degrees; the layer places it at screen center and this widget rotates itself.
 * Ticking is driven by the layer, so native tick is disabled.
 */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API UDamageFeedbackIndicator : public UUserWidget
{
	GENERATED_BODY()

public:
	// 풀에서 꺼내 쓸 때 Layer 가 호출한다. 방향을 계산할 수 없으면 정리하고 false 를 돌려준다.
	bool StartFeedback(AActor* InCharacter, const FVector& InDamageOrigin);

	// Layer Tick 에서 호출한다. 수명이 끝났거나 대상이 사라지면 스스로 정리하고 false 를 돌려준다.
	bool TickFeedback(float DeltaTime);

	void ClearFeedback();

	bool IsFeedbackActive() const { return bFeedbackActive; }
	float GetFeedbackElapsed() const { return FeedbackElapsed; }

protected:
	virtual void NativeOnInitialized() override;

	// Red / Shield 가 각자의 UMG Image 를 공통 페이드 처리에 제공한다.
	virtual UImage* GetFeedbackImage() const PURE_VIRTUAL(UDamageFeedbackIndicator::GetFeedbackImage, return nullptr;);
	virtual float GetFeedbackDuration() const;

	// 피드백이 활성화된 동안 공통 머티리얼 애니메이션과 페이드를 갱신한다.
	virtual void HandleFeedbackTick(float NormalizedElapsedTime);

	// false 면 피격 순간에만 방향을 잡고, 이후 플레이어 회전에 따른 Tick 갱신은 하지 않는다.
	virtual bool ShouldTrackDamageDirection() const { return true; }

	// TimeScale 머티리얼은 FadeIn 구간에 애니메이션을 재생한다. 없는 경우 같은 커브로 Alpha 를 올린다.
	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Fade", meta = (ClampMin = "0.0"))
	float FadeInTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Fade")
	TObjectPtr<UCurveFloat> FadeInCurve;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Fade", meta = (ClampMin = "0.0"))
	float FadeOutTime = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Fade")
	TObjectPtr<UCurveFloat> FadeOutCurve;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Material")
	FName TimeParameterName = TEXT("TimeScale");

	UPROPERTY(EditDefaultsOnly, Category = "Damage Feedback|Material")
	FName AlphaParameterName = TEXT("Alpha");

private:
	bool UpdateFeedbackRotation();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FeedbackMID;

	TWeakObjectPtr<AActor> FeedbackCharacter;
	FVector DamageOrigin = FVector::ZeroVector;
	bool bFeedbackActive = false;
	float FeedbackElapsed = 0.0f;
	float BaseImageOpacity = 1.0f;
	bool bHasTimeParameter = false;
	bool bHasAlphaParameter = false;
};
