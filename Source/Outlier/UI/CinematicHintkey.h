#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CinematicHintkey.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UTextBlock;

/** 시네마틱 재생 동안 표시하는 스킵 안내 문구. 스킵 키(스페이스)는 고정이므로 키 표시는 위젯에 직접 배치한다. */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UCinematicHintkey : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Cinematic")
	void SetHintText(const FText& InText);

	/** 스킵 홀드 진행도(0~1). 키를 떼면 0으로 초기화된다. */
	UFUNCTION(BlueprintCallable, Category = "Cinematic")
	void SetHoldProgress(float InProgress);

	UFUNCTION(BlueprintPure, Category = "Cinematic")
	float GetHoldProgress() const { return HoldProgress; }

	/** 스킵하려면 눌러야 하는 시간(초). */
	UFUNCTION(BlueprintPure, Category = "Cinematic")
	float GetHoldSeconds() const { return FMath::Max(0.0f, HoldSeconds); }

	virtual void SynchronizeProperties() override;

protected:
	virtual void NativeConstruct() override;

	/** 진행도가 바뀔 때 BP 연출(채움 이펙트 등)에 사용한다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Cinematic")
	void OnHoldProgressChanged(float Progress);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> HintText;

	/** 스킵 버튼 이미지. 브러시 머티리얼의 Scalar 파라미터로 홀드 진행도(0~1)를 받는다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> SkipButtonImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cinematic")
	FText HintMessage = NSLOCTEXT("CinematicHintkey", "Skip", "건너뛰기");

	/** 스킵하려면 스페이스를 누르고 있어야 하는 시간(초). 중간에 떼면 초기화된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cinematic", meta = (ClampMin = "0.0", Units = "s"))
	float HoldSeconds = 2.0f;

	/** SkipButtonImage 머티리얼에서 진행도를 받는 Scalar 파라미터 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cinematic")
	FName ProgressParameterName = TEXT("Progress");

private:
	void RefreshHint();
	void ApplyProgressToMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkipButtonMaterial;

	float HoldProgress = 0.0f;
};
