// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DamageFeedbackIndicator.h"
#include "ShieldDamageFeedbackWidget.generated.h"

class UImage;

/** Shield hit direction indicator. Pooled by UDamageFeedbackLayer and shown when bShowShieldFeedback is set. */
UCLASS(Abstract, Blueprintable, meta = (DisableNativeTick))
class TAGDRIVENUI_API UShieldDamageFeedbackWidget : public UDamageFeedbackIndicator
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual UImage* GetFeedbackImage() const override { return ShieldFeedback.Get(); }

	// Shield 는 피격 순간의 방향만 잡고, 이후 플레이어 회전은 따라가지 않는다.
	virtual bool ShouldTrackDamageDirection() const override { return false; }

	// 기존 Shield BP 에 저장된 수명 값을 읽기 위한 숨김 호환 필드.
	UPROPERTY()
	float FeedbackDuration = 0.5f;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> ShieldFeedback;
};
