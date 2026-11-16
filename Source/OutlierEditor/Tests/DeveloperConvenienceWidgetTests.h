#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "UI/DeveloperConvenienceWidget.h"
#include "DeveloperConvenienceWidgetTests.generated.h"

// 렌더링 창 없이 실제 LocalPlayer 레이어 경로를 검증한다.
UCLASS(Transient, NotBlueprintable)
class UDeveloperConvenienceTestViewport : public UGameViewportClient
{
	GENERATED_BODY()
public:
	void SetContext(UWorld* InWorld, UGameInstance* InGameInstance) { World = InWorld; GameInstance = InGameInstance; }
};

UCLASS(Transient, NotBlueprintable)
class UDeveloperConvenienceTestWidget : public UDeveloperConvenienceWidget
{
	GENERATED_BODY()
public:
	static TFunction<void()> OnInitialize;
	static TFunction<void()> OnConstruct;
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
};
