// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "InputActionKeyDisplayWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class ULocalPlayerSettingsSubsystem;
class UBorder;
class UTextBlock;
class UTexture2D;
class UDataTable;
struct FInputKeyTextureDataRow;

/**
 * 특정 Input Action 하나의 현재 바인딩 키를 보여주는 공용 부품.
 * 실제 키 이름으로 DT의 Texture를 조회한다. 전용 키 이미지는 텍스트 없이,
 * 일반 키 또는 bShowKeyText 행은 공용 프레임과 텍스트로 표시한다.
 * 조합 위젯은 SetDisplayedKey로 이미 조회한 FKey를 전달하여 같은 표시 경로를 재사용한다.
 * UILayerKeyHintWidget의 Confirm/Escape 키 표시가 이 클래스를 멤버로 갖는 식으로
 * 쓰이고, InteractKeyWidget 등 "이 키를 누르세요" UI도 동일하게
 * WatchedInputAction만 넣어서 재사용하면 된다.
 *
 * 이 위젯이 생성되는 시점(NativeConstruct/SetWatchedInputAction 호출 시점)과
 * Enhanced Input이 실제로 IMC를 등록/재구성 완료하는 시점 사이엔 순서 보장이
 * 없다 — 그래서 "생성될 때 한 번 조회하고 끝"이 아니라, 조회에 실패하면
 * UEnhancedInputLocalPlayerSubsystem::ControlMappingsRebuiltDelegate(매핑이
 * 실제로 재구성될 때 엔진이 쏘는 이벤트)를 구독해뒀다가, 데이터가 유효해지는
 * 시점에 알아서 다시 시도한다. 이미 데이터가 유효하면(=바로 조회 성공) 이
 * 델리게이트는 구독하지 않는다. 즉 "언제 만들어지든/언제 IA가 꽂히든 상관없이
 * 알아서 맞는 값을 보여준다"를 이 클래스 하나가 보장하므로, 이걸 쓰는 쪽
 * (UILayerKeyHintWidget, InteractKeyWidget, 그 외 나중에 생길 것들)은 타이밍을
 * 신경 쓸 필요가 없다.
 *
 * SettingWidget에서 키 리바인드가 커밋되면 ULocalPlayerSettingsSubsystem::
 * OnInputActionKeyChanged가 브로드캐스트되고, 이 위젯은 그 이벤트를 구독하고
 * 있다가 자기가 보고 있는 IA와 일치할 때만 표시를 갱신한다. SettingWidget
 * 인스턴스가 아니라 LocalPlayer 상시 Subsystem을 구독하므로, 설정 창이 열려
 * 있지 않을 때도(즉 이 위젯이 더 오래 살아있어도) 정상적으로 갱신된다.
 */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInputActionKeyDisplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetWatchedInputAction(UInputAction* NewInputAction);

	/** IA를 감시하지 않고 동일한 texture/text 규칙으로 해석된 키 하나를 표시한다. */
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayedKey(FKey NewKey);

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	UInputAction* GetWatchedInputAction() const { return WatchedInputAction; }

	// 키 대신 임의의 텍스트("Ready" 등)를 강제로 보여주고 싶을 때 사용.
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetTextOverride(const FText& InOverrideText);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void ClearTextOverride();

	// 지금 당장 조회해서 표시를 갱신한다. 성공(실제 키를 찾음/오버라이드
	// 텍스트 사용)하면 true, 아직 매핑이 준비 안 돼 MissingKeyText로
	// 빠졌으면 false. 대부분의 경우 이 함수를 직접 부를 필요 없이
	// SetWatchedInputAction만 쓰면 된다 — 실패 시의 재시도 예약은
	// 그쪽에서 알아서 처리한다.
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	bool RefreshDisplayedKey();

	// WBP에서 지정한 이미지를 Border의 Brush 리소스로 사용한다.
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetBorderImage(UTexture2D* NewImage);

	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetKeyTextColor(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayHeight(float NewHeight);

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	UTexture2D* GetBorderImage() const { return BorderImage; }

	/** 해석된 이미지 크기. 텍스트 키는 최소 frame 크기. */
	UFUNCTION(BlueprintPure, Category = "UI|Input")
	FVector2D GetKeyDisplaySize() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// WBP에서 Border의 Content를 원하는 Image/Overlay 등으로 구성할 수 있게
	// Border 자체를 BindWidget으로 노출한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> Border;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> KeyText;

	// WBP 파생 클래스에서 이미지 텍스처를 주입하면 Border Brush에 반영된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UTexture2D> BorderImage;

	/** 키 라벨과 override 텍스트 색상. BP 기본값이나 런타임에 변경할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKeyTextColor, Category = "UI|Input")
	FLinearColor KeyTextColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float DisplayHeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float MinTextKeyWidth = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float MaxTextKeyWidth = 88.0f;

	/** Nine-slice margin으로 공용 frame이 키 라벨에 맞춰 늘어난다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FMargin FrameSliceMargin = FMargin(0.15f);

	/** 기본 BindKey WBP에 할당하는 공용 테이블. Row Name은 엔진 키 이름과 같아야 한다. */
	// 기존 WBP 할당이 유효하도록 직렬화된 프로퍼티 이름을 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input", meta = (DisplayName = "Key Texture Table", RequiredAssetDataTags = "RowStructure=/Script/Outlier.InputKeyTextureDataRow"))
	TObjectPtr<UDataTable> MouseKeyTextureTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UInputAction> WatchedInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input")
	FText MissingKeyText;

private:
	UFUNCTION()
	void HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey);

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	// RefreshDisplayedKey를 시도해서, 실패하면(아직 매핑이 없으면) 재구성
	// 완료 델리게이트를 구독해서 기다리고, 성공하면 그 구독을 해제한다.
	void EnsureWatchedKeyIsResolved();

	void BindSettingsSubsystem();
	void UnbindSettingsSubsystem();
	void RefreshBorderBrush();
	void RefreshKeyTextColor();
	const FInputKeyTextureDataRow* FindKeyTextureRow(FKey Key) const;
	void ShowKeyText(const FText& Text, FKey Key = FKey());
	void ShowKeyImage(const FInputKeyTextureDataRow& Row);
	void RefreshKeyLayout();
	bool ShowResolvedKey(FKey Key);
	void BindControlMappingsRebuiltDelegate();
	void UnbindControlMappingsRebuiltDelegate();

	UPROPERTY(Transient)
	FText TextOverride;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayerSettingsSubsystem> BoundSettingsSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystemForRebuild;

	UPROPERTY(Transient)
	FSlateBrush DefaultBorderBrush;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DisplayedKeyTexture;

	UPROPERTY(Transient)
	FKey ExplicitKey;

	FVector2D ImageDisplaySize = FVector2D::ZeroVector;
	FMargin DefaultBorderPadding;
	bool bHasDefaultBorderPadding = false;

	bool bUseExplicitKey = false;

	TSet<FName> WarnedMissingMouseKeys;
	bool bHasDefaultBorderBrush = false;
	bool bDisplayingImageKey = false;

	bool bIsConstructed = false;
};
