#pragma once

#include "UI/TargetPointHudWidget.h"
#include "TimerManager.h"
#include "PairPositionMarkerWidget.generated.h"

class UImage;
class UTexture2D;
enum class EOutlierPlayerRole : uint8;

UCLASS(Blueprintable)
class OUTLIER_API UPairPositionMarkerWidget : public UTargetPointWidget
{
	GENERATED_BODY()

public:
	explicit UPairPositionMarkerWidget(const FObjectInitializer& ObjectInitializer);
	void SetTargetRole(EOutlierPlayerRole Role);
	void SetRearIndicatorVisible(bool bVisible);
	void PlayDamagePulse(float RemainingSeconds);
	void StopDamagePulse();
	void UpdateDamagePulse();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair Marker")
	FVector2D MarkerSize = FVector2D(32.0);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair Marker|Damage")
	FLinearColor DamagePulseColor = FLinearColor::Red;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair Marker|Damage", meta = (ClampMin = "0.001", Units = "s"))
	float DamagePulseDuration = 0.6f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Pair Marker")
	TObjectPtr<UImage> RoleIcon;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Pair Marker")
	TObjectPtr<UWidget> RearIndicator;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair Marker")
	TObjectPtr<UTexture2D> ShooterIcon;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pair Marker")
	TObjectPtr<UTexture2D> PartnerIcon;

private:
	friend class FPairPositionMarkerLifecycleTest;
	friend class FPairPositionMarkerDamageTest;

	void ApplyRoleIcon();
	void ApplyDamagePulseTint(float RemainingFraction);
	EOutlierPlayerRole TargetRole;
	FTimerHandle DamagePulseTimer;
	TOptional<FLinearColor> OriginalRoleIconTint;
	FLinearColor DamagePulseStartTint;
};
