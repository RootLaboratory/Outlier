#pragma once

#include "CoreMinimal.h"
#include "Components/ProgressBar.h"
#include "GhostProgressBar.generated.h"

class SGhostProgressBar;
class UWorld;

/** Main은 즉시 반영하고 Ghost만 지연 후 감소시키는 공통 게이지. */
UCLASS(meta = (DisplayName = "Ghost Progress Bar"))
class TAGDRIVENUI_API UGhostProgressBar : public UProgressBar
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghost")
	bool bEnableGhost = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghost")
	FSlateBrush GhostFillBrush;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghost")
	FLinearColor GhostFillColorAndOpacity = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghost", meta = (ClampMin = "0", Units = "s"))
	float GhostDelay = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghost", meta = (ClampMin = "0", Units = "s"))
	float GhostDuration = 0.60f;

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void SetTargetPercent(float InPercent);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void SetPercentImmediate(float InPercent);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void ResetGhostToCurrent();

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void SetGhostEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void SetGhostTiming(float InDelay, float InDuration);

	UFUNCTION(BlueprintPure, Category = "Ghost")
	float GetGhostPercent() const { return GhostPercent; }

	bool IsGhostAnimating() const { return bAnimating; }

	// Slate는 게임 시간 차이를 넘기며, 자동화에서는 같은 상태 전이에 시간을 직접 공급한다.
	void AdvanceGhostAnimation(float DeltaTime);
	void TickGhostFromGameTime();
	virtual void SetVisibility(ESlateVisibility InVisibility) override;
	virtual void RemoveFromParent() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void RefreshSlateAppearance();
	void RefreshSlatePercent();
	void ValidateTiming();
	void BeginGhostAnimation();

	TSharedPtr<SGhostProgressBar> GhostWidget;
	TWeakObjectPtr<UWorld> AnimationWorld;
	float GhostPercent = 0.0f;
	float GhostStartPercent = 0.0f;
	double Elapsed = 0.0;
	double LastGameTime = 0.0;
	float EffectiveDelay = 0.25f;
	float EffectiveDuration = 0.60f;
	bool bHasReceivedValue = false;
	bool bAnimating = false;
	bool bInvalidTimingWarningLogged = false;
};
