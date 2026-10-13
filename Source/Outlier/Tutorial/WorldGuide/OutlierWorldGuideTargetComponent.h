#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTypes.h"
#include "OutlierWorldGuideTargetComponent.generated.h"

class AOutlierWorldGuideManager;
class UUserWidget;
class UWidgetComponent;

/** World anchor for a screen-space UI marker. Add to any actor, including Tuto actors. */
UCLASS(ClassGroup = (Tutorial), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UOutlierWorldGuideTargetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOutlierWorldGuideTargetComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (Categories = "UI.Info.World"))
	FGameplayTag WidgetID;

	/** Assign the WBP to render here. No MainUI/Canvas reference is required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide")
	TSubclassOf<UUserWidget> WidgetClass;

	/** Screen UI size, with the viewport's normal DPI scaling. Component Scale does not resize the UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "1.0", DisplayName = "UI Draw Size"))
	FVector2D DrawSize = FVector2D(128.0, 128.0);

	/** World units added to this component's location; unaffected by owner rotation/scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Guide")
	FVector WorldOffset = FVector::ZeroVector;

	/** Alignment of the UI marker around its projected world anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Guide", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D Pivot = FVector2D(0.5, 0.5);

	UFUNCTION(BlueprintPure, Category = "World Guide")
	bool IsGuideVisible() const { return bGuideVisible; }

	/** Manual gameplay completion, on the server. A non-empty next ID selects a branch. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool CompleteGuide(FGameplayTag NextWidgetID = FGameplayTag());

	/** Call from the arrival volume after filtering the intended pawn. Only Overlap rows complete. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "World Guide")
	bool NotifyGuideOverlap();

	void ApplyGuide(const FOutlierWorldGuideContext& Context);
	void ClearGuide();
	void SetGuideManager(AOutlierWorldGuideManager* Manager);
	FString DescribeDebugState() const;
	static bool CompleteOnActor(AActor* Actor, FGameplayTag NextWidgetID = FGameplayTag());
	static bool NotifyOverlapOnActor(AActor* Actor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void UpdateDisplayPlacement();

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> DisplayComponent;

	TWeakObjectPtr<AOutlierWorldGuideManager> GuideManager;
	bool bGuideVisible = false;
};
