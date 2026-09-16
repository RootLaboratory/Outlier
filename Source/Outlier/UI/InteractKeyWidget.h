#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractKeyWidget.generated.h"

class UBorder;
class UInputAction;
class UInputActionKeyDisplayWidget;

UCLASS()
class OUTLIER_API UInteractKeyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 실제 키 표시는 KeyDisplay가 InteractionAction을 직접 물고 스스로
	// 그려준다(리바인드 시 자동 갱신, Enhanced Input 매핑이 아직 준비 안
	// 됐을 때의 재시도까지 KeyDisplay 쪽이 알아서 처리). 여기서는 보이기/
	// 숨기기만 담당한다.
	void UpdateInteractKey();
	void ClearInteractKey();

protected:
	// 상호작용 입력 액션. 인터랙터블마다 다를 이유가 없어서 이 위젯 하나가
	// 고정으로 들고 KeyDisplay에 물려준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interact")
	TObjectPtr<UInputAction> InteractionAction;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UInputActionKeyDisplayWidget> KeyDisplay;


};
