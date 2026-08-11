#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "StatAllocatorWidget.generated.h"

class APartnerCharacter;
class AOutlierPlayerState;
class AShooterCharacter;
class UButton;
class UImage;
class UTextBlock;
class UWidgetSwitcher;

/** Blueprint supplies the layout only; stat allocator behavior lives in C++. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UStatAllocatorWidget : public UUserWidget,
	public IUILayerInputReceiver,
	public IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	void InjectCharacters(AShooterCharacter* InShooterCharacter, APartnerCharacter* InPartnerCharacter);

	AShooterCharacter* GetShooterCharacter() const { return ShooterCharacter; }

	APartnerCharacter* GetPartnerCharacter() const { return PartnerCharacter; }

	void ShowIntroPage();

	void ShowAllocatorPage();

	int32 GetCurrentPageIndex() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void InitializeUILayerContext_Implementation(
		const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> StatAllocatorSwitcher;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AbilityDescText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NodeCountText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> NodeImage;

	UPROPERTY(Transient)
	TObjectPtr<AShooterCharacter> ShooterCharacter;

	UPROPERTY(Transient)
	TObjectPtr<APartnerCharacter> PartnerCharacter;

private:
	UFUNCTION()
	void HandleStartButtonClicked();

	void BindOwningPlayerState();
	void UnbindPlayerState();
	void RefreshNodeCountText();
	void HandleNodeCountChanged(int32 NewNodeCount);

	bool SetActivePage(int32 PageIndex);

	TWeakObjectPtr<AOutlierPlayerState> BoundPlayerState;
	FDelegateHandle NodeCountChangedHandle;

	static constexpr int32 IntroPageIndex = 0;
	static constexpr int32 AllocatorPageIndex = 1;
};
