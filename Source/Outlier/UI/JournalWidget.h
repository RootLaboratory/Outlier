#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JournalData.h"
#include "UI/UILayerContextReceiver.h"
#include "UI/UILayerInputReceiver.h"
#include "JournalWidget.generated.h"

class UHintKeyDisplayWidget;
class UTextBlock;
class AJournalInteraction;

UCLASS(Blueprintable)
class OUTLIER_API UJournalWidget : public UUserWidget, public IUILayerInputReceiver, public IUILayerContextReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Journal")
	void SetJournalEntry(const FDataTableRowHandle& InJournalEntry);

	UFUNCTION(BlueprintCallable, Category = "Journal")
	bool RefreshJournal();

	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors) override;
	virtual bool HandleUILayerEscape_Implementation() override;
	virtual bool HandleUILayerConfirmed_Implementation() override;
	virtual bool HandleUILayerUp_Implementation() override;
	virtual bool HandleUILayerDown_Implementation() override;
	virtual bool HandleUILayerLeft_Implementation() override;
	virtual bool HandleUILayerRight_Implementation() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal", meta = (RowType = "/Script/Outlier.JournalRow"))
	FDataTableRowHandle JournalEntry;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> Desc;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UHintKeyDisplayWidget> BackKeyDisplay;

private:
	void RefreshContextEntry();
	void UnbindJournalContext();
	TWeakObjectPtr<AJournalInteraction> JournalContext;
	FDelegateHandle JournalEntryChangedHandle;
};
