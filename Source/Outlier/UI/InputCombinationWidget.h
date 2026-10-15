#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "UI/InputDisplayTypes.h"
#include "InputCombinationWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UHorizontalBox;
class UInputAction;
class UInputActionKeyDisplayWidget;
class ULocalPlayerSettingsSubsystem;
class UTexture2D;
class UWidget;

/**
 * BindKey WBP를 재사용해 런타임 입력 표현식을 가로로 렌더링한다.
 * KeyDisplayWidgetClass에는 기존 InputActionKeyDisplayWidget WBP를 할당한다.
 * native 인스턴스는 자체 HorizontalBox를 만들고, WBP는 InputItems를 제공할 수 있다.
 * 예시 (각 항목은 앞 구분자를 포함한 term이다):
 * Move(AllMapped); Sprint(FirstMapped), + Move(AllMapped);
 * Crouch(FirstMapped), + Sprint(FirstMapped), + Move(AllMapped).
 */
UCLASS(Blueprintable, meta = (DisableNativeTick))
class OUTLIER_API UInputCombinationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayDefinition(const FInputDisplayDefinition& Definition);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetExpression(const TArray<FInputDisplayTerm>& NewExpression);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void ClearExpression();

	/** 활성 매핑을 다시 조회한다. false면 매핑 누락 또는 잘못된 위젯 설정이다. */
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	bool RefreshExpression();

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetKeyFrameTexture(UTexture2D* Texture);

	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetKeyTextColor(FLinearColor NewColor);

	virtual void SynchronizeProperties() override;

	/** resolver가 제공하는 런타임 데이터. WBP 기본값으로 표현식을 작성하지 않는다. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Input")
	TArray<FInputDisplayTerm> Expression;

	/** 공용 키 texture 테이블, frame, 텍스트 스타일을 가진 구체 WBP 서브클래스. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TSubclassOf<UInputActionKeyDisplayWidget> KeyDisplayWidgetClass;

	/** 생성되는 모든 텍스트 키에 주입하는 frame. 전용 이미지 키는 자체 texture를 유지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> DefaultKeyFrameTexture;

	/** 생성되는 모든 키 표시에 주입하는 색상. leaf WBP 기본값을 덮어쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKeyTextColor, Category = "UI|Input")
	FLinearColor KeyTextColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float KeyDisplayHeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> PlusTexture;

	/** Texture 기호는 종횡비를 유지한 채 이 영역에 맞게 스케일된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FVector2D SymbolSize = FVector2D(16.0, 16.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.0"))
	float KeySpacing = 8.0f;

	/** 보이는 기호로 구분되지 않을 때 인접한 IA 그룹 사이 간격. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.0"))
	float GroupSpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FMargin SymbolPadding = FMargin(8.0f, 0.0f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** native 사용 시 선택 사항. 커스텀 WBP 루트에는 InputItems라는 HorizontalBox가 있어야 한다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> InputItems;

private:
	bool EnsureInputPanel();
	UWidget* CreateSeparator(const FInputDisplayTerm& Term);
	void BindInputEvents();
	void UnbindInputEvents();

	UFUNCTION()
	void HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey);

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	bool bIsConstructed = false;
};
