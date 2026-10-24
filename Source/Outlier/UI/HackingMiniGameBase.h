#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Drone/Partner/HackType.h"
#include "HackingMiniGameBase.generated.h"

class UHackableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackingMiniGameFinished, EHackResult, Result);

UCLASS(Abstract, Blueprintable)
class OUTLIER_API UHackingMiniGameBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void InitializeMiniGame(AActor* InTargetActor, UHackableComponent* InHackableComponent, float InSuccessRangeMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void StartMiniGame();

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	void FinishMiniGame(EHackResult Result);

	UFUNCTION(BlueprintCallable, Category = "Hack|MiniGame")
	bool IsMiniGameActive() const { return bMiniGameActive; }

	virtual bool HandlePrimaryClick();

	UPROPERTY(BlueprintAssignable, Category = "Hack|MiniGame")
	FOnHackingMiniGameFinished OnMiniGameFinished;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void OnMiniGameStarted();

	virtual void OnMiniGameUpdated(float DeltaTime);

	virtual void OnMiniGameFinishedEvent(EHackResult Result);

	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	TObjectPtr<UHackableComponent> HackableComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	uint8 bMiniGameActive : 1 = false;

	// 시작 시 주입된 값만 사용한다. Widget은 GAS나 업그레이드 상태를 조회하지 않는다.
	UPROPERTY(BlueprintReadOnly, Category = "Hack|MiniGame")
	float SuccessRangeMultiplier = 1.0f;
};
