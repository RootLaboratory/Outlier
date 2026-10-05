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

#if WITH_EDITORONLY_DATA
	// PIE에서는 에디터가 ESC를 Stop 단축키로 먼저 가져가므로, ESC 액션을 다른 키로 묶은 IMC를 PIE에서만 추가한다.
	// 입력 모드 필터는 원본 IMC와 같아야 한다. 쿡에서 빠지므로 패키지와 설정창에는 나오지 않는다.
	UPROPERTY(EditAnywhere, Category = "Editor")
	TArray<TObjectPtr<UInputMappingContext>> PIEMappingContexts;
#endif
};
