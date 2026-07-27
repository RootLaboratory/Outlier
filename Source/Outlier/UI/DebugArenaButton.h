// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DebugArenaButton.generated.h"

class UButton;

/**
 * 디버그용: 요청한 페어(자기 ArenaId) arena를 서버 권위로 리로드시키는 버튼.
 */
UCLASS()
class OUTLIER_API UDebugArenaButton : public UUserWidget
{
	GENERATED_BODY()

public:
	UDebugArenaButton(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;

	// BP에서 이 이름의 Button을 BindWidget으로 연결
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> DebugReloadButton;

public:
	UFUNCTION()
	void OnClicked_DebugReload();
};
