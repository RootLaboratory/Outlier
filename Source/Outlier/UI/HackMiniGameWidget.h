#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Drone/Partner/HackType.h"
#include "GameplayTagContainer.h"
#include "HackMiniGameWidget.generated.h"

class UHackableComponent;
class UHackingMiniGameBase;
class UCanvasPanel;
class UPartnerHackComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackMiniGameWidgetFinished, const FHackResultContext&, ResultContext);

UCLASS(Blueprintable)
class OUTLIER_API UHackMiniGameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void InitializeHackMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent, UPartnerHackComponent* InHackComponent);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void SetMiniGameTimeLimit(float InTimeLimit);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	bool StartHacking();

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void CancelHacking();

	UPROPERTY(BlueprintAssignable, Category = "Hack|MiniGame")
	FOnHackMiniGameWidgetFinished OnHackMiniGameFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MiniGameRoot;

	UPROPERTY(EditDefaultsOnly, Category = "Hack|MiniGame")
	TMap<FGameplayTag, TSubclassOf<UHackingMiniGameBase>> MiniGameWidgetClassesByTag;

	UPROPERTY(EditDefaultsOnly, Category = "Hack|MiniGame")
	TSubclassOf<UHackingMiniGameBase> DefaultMiniGameWidgetClass;

private:
	TSubclassOf<UHackingMiniGameBase> ResolveMiniGameWidgetClass() const;
	void ClearActiveMiniGame();

	UFUNCTION()
	void HandleActiveMiniGameFinished(EHackResult Result);

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY()
	TObjectPtr<UPartnerHackComponent> HackComponent;

	UPROPERTY()
	TObjectPtr<UHackingMiniGameBase> ActiveMiniGameWidget;

	float MiniGameTimeLimit = 0.0f;
};
