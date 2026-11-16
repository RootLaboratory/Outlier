#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/DeveloperConvenienceTypes.h"
#include "UI/UILayerInputReceiver.h"
#include "DeveloperConvenienceWidget.generated.h"

// 메뉴와 상대 대기 WBP가 공통으로 상속한다. 조작 가능 여부는 서버 Snapshot으로 결정한다.
UCLASS(Blueprintable)
class OUTLIER_API UDeveloperConvenienceWidget : public UUserWidget, public IUILayerInputReceiver
{
	GENERATED_BODY()
public:
	void InitializeDeveloperConvenience(const FDeveloperConvenienceSnapshot& Snapshot);
	void ReleaseDeveloperConvenienceContext() { bContextActive = false; }
	UFUNCTION(BlueprintPure, Category = "Developer Convenience")
	const FDeveloperConvenienceSnapshot& GetDeveloperConvenienceContext() const { return Context; }
	UFUNCTION(BlueprintPure, Category = "Developer Convenience")
	EDeveloperConveniencePage GetDeveloperConveniencePage() const { return Page; }
	UFUNCTION(BlueprintCallable, Category = "Developer Convenience")
	bool SetDeveloperConveniencePage(EDeveloperConveniencePage NewPage);
	UPROPERTY(BlueprintAssignable, Category = "Developer Convenience")
	FOnDeveloperConveniencePageChanged OnDeveloperConveniencePageChanged;

protected:
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeDestruct() override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override { return true; }
	virtual bool HandleUILayerUp_Implementation() override { return true; }
	virtual bool HandleUILayerDown_Implementation() override { return true; }
	virtual bool HandleUILayerLeft_Implementation() override { return true; }
	virtual bool HandleUILayerRight_Implementation() override { return true; }

private:
	UPROPERTY(Transient)
	FDeveloperConvenienceSnapshot Context;
	EDeveloperConveniencePage Page = EDeveloperConveniencePage::MainMenu;
	bool bContextActive = false;
};
