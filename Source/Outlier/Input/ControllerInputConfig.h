#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ControllerInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

// 폰 유무와 상관없이 PlayerController가 직접 받는 입력. Frontend와 인게임 컨트롤러가 같은 에셋을 공유한다.
UCLASS()
class OUTLIER_API UControllerInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> WidgetMappingContext;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetEscapeAction;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetConfirmedAction;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetUpAction;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetDownAction;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetLeftAction;

	UPROPERTY(EditAnywhere, Category = "Widget")
	TObjectPtr<UInputAction> WidgetRightAction;

	// 인게임 컨트롤러만 바인딩한다. 키 매핑은 게임플레이 IMC 쪽에 있다.
	UPROPERTY(EditAnywhere, Category = "InGame")
	TObjectPtr<UInputAction> InGameSettingAction;
};
