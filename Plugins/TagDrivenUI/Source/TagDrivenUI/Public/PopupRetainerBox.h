#pragma once

#include "CoreMinimal.h"
#include "Components/RetainerBox.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "PopupRetainerBox.generated.h"

class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPopupRetainerEvent);

UCLASS(meta = (DisplayName = "Popup Retainer Box"))
class TAGDRIVENUI_API UPopupRetainerBox : public URetainerBox
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Popup")
	void PlayOpen();

	UFUNCTION(BlueprintCallable, Category = "Popup")
	void PlayClose();

	UFUNCTION(BlueprintCallable, Category = "Popup")
	void PlayPopup(bool bReverse);

	UFUNCTION(BlueprintCallable, Category = "Popup")
	void PlayPopupAdvanced(float Direction, float InDuration, bool bReverse);

	UFUNCTION(BlueprintCallable, Category = "Popup")
	void ResetPopup();

	UFUNCTION(BlueprintPure, Category = "Popup")
	bool IsPopupPlaying() const { return bPlaying; }

	UPROPERTY(BlueprintAssignable, Category = "Popup")
	FPopupRetainerEvent OnOpened;

	UPROPERTY(BlueprintAssignable, Category = "Popup")
	FPopupRetainerEvent OnClosed;

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	bool TryResetPopup();
	bool TryStartPlayback();
	EActiveTimerReturnType HandlePopupTick(double CurrentTime, float DeltaTime);

	UPROPERTY(EditAnywhere, Category = "Popup", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float OpenDirection = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Popup", meta = (ClampMin = "0.01"))
	float OpenDuration = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Popup", meta = (ClampMin = "0.01"))
	float CloseDuration = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Popup|Material")
	FName TextureParameterName = TEXT("Texture");

	UPROPERTY(EditAnywhere, Category = "Popup|Material")
	FName AmountParameterName = TEXT("Amount");

	UPROPERTY(EditAnywhere, Category = "Popup|Material")
	FName DirectionParameterName = TEXT("Dir");

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PopupMID;

	float Elapsed = 0.0f;
	float Duration = 0.0f;
	float Direction = 0.0f;

	bool bPlaying = false;
	bool bIsOpen = false;
	bool bReversePlayback = false;
	bool bPendingPlayback = false;
	bool bResetRequested = true;
	bool bActiveTimerRegistered = false;
};
