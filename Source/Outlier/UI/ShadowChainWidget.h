#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "ShadowChainWidget.generated.h"

class AShooterCharacter;
class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

// Presentation-only interpolation. Remaining time always comes from the server snapshot.
struct OUTLIER_API FShadowChainDisplayState
{
	void Update(const FOutlierShadowChainPresentationState& State, float Remaining, float DeltaSeconds,
		float TransitionSeconds, bool bImmediate = false);
	void Reset();
	bool NeedsUpdate() const { return bPassive || bTransitioning; }
	float Progress = 0.0f;
	bool bVisible = false;
	bool bPassive = false;

private:
	bool bHasSnapshot = false;
	bool bTransitioning = false;
	bool bRefilling = false;
	uint32 ActivationSerial = 0;
	float Target = 0.0f;
	float From = 0.0f;
	float Elapsed = 0.0f;
	float ReconcileOffset = 0.0f;
	float ReconcileElapsed = 0.0f;
};

// WBP provides ShadowChainImage, material, texture and placement. No gameplay authority.
UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API UShadowChainWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Shadow Chain")
	void SetObservedShooter(AShooterCharacter* Shooter);

	UFUNCTION(BlueprintCallable, Category = "Shadow Chain")
	void ClearObservedShooter();

	UFUNCTION(BlueprintPure, Category = "Shadow Chain")
	float GetDisplayedProgress() const { return Display.Progress; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance")
	TObjectPtr<UMaterialInterface> FillMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance")
	TObjectPtr<UTexture2D> IconTexture;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance")
	FLinearColor DimColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance")
	FLinearColor StackColor = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance")
	FLinearColor StealthColor = FLinearColor(0.2f, 0.7f, 1.0f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shadow Chain|Appearance", meta = (ClampMin = "0.0", Units = "s"))
	float FillTransitionSeconds = 0.15f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;
	UPROPERTY(BlueprintReadOnly, Category = "Shadow Chain", meta = (BindWidgetOptional))
	TObjectPtr<UImage> ShadowChainImage;

private:
	friend class FShadowChainWidgetMaterialTest;
	friend class FShadowChainWidgetAutoBindingTest;
	void BindObservedShooter();
	void UnbindObservedShooter();
	bool IsValidLocalShooter() const;
	void HandlePresentationChanged();
	void HandleDeathChanged(const FGameplayTag Tag, int32 Count);
	void RefreshDisplay(float DeltaSeconds, bool bImmediate);
	void ApplyDisplay();
	void EnsureMaterial();
	void StartUpdates();
	void StopUpdates();
	bool TickDisplay(float DeltaSeconds);
	UFUNCTION()
	void HandleObservedEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	UFUNCTION()
	void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);

	TWeakObjectPtr<AShooterCharacter> ObservedShooter;
	TWeakObjectPtr<UOutlierAbilitySystemComponent> ObservedASC;
	TWeakObjectPtr<APlayerController> ObservedController;
	FDelegateHandle PresentationHandle;
	FDelegateHandle DeathHandle;
	FTSTicker::FDelegateHandle UpdateHandle;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MaterialSource;
	FShadowChainDisplayState Display;
	double LastWorldTime = 0.0;
	bool bConstructed = false;
	bool bAutoObserveOwningShooter = true;
	bool bWarnedMissingMaterial = false;
};
