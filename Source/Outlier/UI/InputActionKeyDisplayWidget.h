#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "InputActionDisplayInterface.h"
#include "InputActionKeyDisplayWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class ULocalPlayerSettingsSubsystem;
class UBorder;
class UTextBlock;
class UTexture2D;
class UDataTable;
struct FInputKeyTextureDataRow;
class SInputKeyFrame;

/**
 * 특정 Input Action 하나의 현재 바인딩 키를 보여주는 공용 부품.
 * 실제 키 이름으로 DT의 Texture를 조회한다. 전용 키 이미지는 텍스트 없이,
 * bShowKeyText 행은 매핑된 프레임과 텍스트로 표시하고, 텍스처가 없으면 공용 프레임을 사용한다.
 * 조합 위젯은 SetDisplayedKey로 이미 조회한 FKey를 전달하여 같은 표시 경로를 재사용한다.
 * UILayerKeyHintWidget의 Confirm/Escape 키 표시가 이 클래스를 멤버로 갖는 식으로
 * 쓰이고, InteractKeyWidget 등 "이 키를 누르세요" UI도 동일하게
 * WatchedInputAction만 넣어서 재사용하면 된다.
 *
 * 이 위젯이 생성되는 시점(NativeConstruct/SetWatchedInputAction 호출 시점)과
 * Enhanced Input이 실제로 IMC를 등록/재구성 완료하는 시점 사이엔 순서 보장이
 * 없다. 액션의 키를 표시하는 동안 ControlMappingsRebuiltDelegate를 구독하여
 * 최초 조회 성공 후에도 활성 매핑 변경에 맞춰 다시 조회한다.
 * 고정 키·고정 문구·액션 없음 상태에서는 구독을 해제하고,
 * 액션 표시로 돌아오면 다시 연결한다. NativeDestruct에서도 연결을 해제한다.
 *
 * SettingWidget에서 키 리바인드가 커밋되면 ULocalPlayerSettingsSubsystem::
 * OnInputActionKeyChanged가 브로드캐스트되고, 이 위젯은 그 이벤트를 구독하고
 * 있다가 자기가 보고 있는 IA와 일치할 때만 표시를 갱신한다. SettingWidget
 * 인스턴스가 아니라 LocalPlayer 상시 Subsystem을 구독하므로, 설정 창이 열려
 * 있지 않을 때도(즉 이 위젯이 더 오래 살아있어도) 정상적으로 갱신된다.
 */
UCLASS(Abstract, Blueprintable)
class OUTLIER_API UInputActionKeyDisplayWidget : public UUserWidget, public IInputActionDisplayInterface
{
	GENERATED_BODY()

public:
	virtual void SetDisplayedInputAction_Implementation(UInputAction* InputAction) override;

	virtual void SynchronizeProperties() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetWatchedInputAction(UInputAction* NewInputAction);

	/** IA를 감시하지 않고 동일한 texture/text 규칙으로 해석된 키 하나를 표시한다. */
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayedKey(FKey NewKey);

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	UInputAction* GetWatchedInputAction() const { return WatchedInputAction; }

	/**
	 * 생성된 뒤 마지막 조회에서 표시할 키를 찾지 못해 MissingKeyText로 빠졌으면 true.
	 * 현재 입력 모드에 IA 매핑이 없을 때(예: 다른 모드 전용 IMC만 활성)도 여기에 해당한다.
	 * 아직 생성 전이면 판단할 수 없으므로 false다.
	 */
	UFUNCTION(BlueprintPure, Category = "UI|Input")
	bool IsMissingKey() const { return bIsMissingKey; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMissingKeyChanged, bool /*bIsMissingKey*/);
	/** IsMissingKey 값이 바뀔 때만 브로드캐스트한다. */
	FOnMissingKeyChanged OnMissingKeyChanged;

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

	/** Scaled 154x154 key frame. Clears NonFrameTexture and the legacy background. */
	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetFrameTexture(UTexture2D* Texture);

	/** Background using the existing brush/margin calculation. Clears FrameTexture. */
	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetNonFrameTexture(UTexture2D* Texture);

	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetKeyTextColor(FLinearColor NewColor);

	UFUNCTION(BlueprintSetter, Category = "UI|Input")
	void SetShowKeyTextWithImage(bool bNewShowKeyTextWithImage);

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetDisplayHeight(float NewHeight);

	/** Center the shared Latin cap line rather than the font's full ascent/descent box. */
	UFUNCTION(BlueprintSetter, Category = "UI|Layout")
	void SetUseCaplineAlignment(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	UTexture2D* GetBorderImage() const;

	/** 이미지 키 크기 또는 DisplayHeight 기준 폰트로 측정한 라벨 너비에 맞춘 텍스트 키 크기. */
	UFUNCTION(BlueprintPure, Category = "UI|Input")
	FVector2D GetKeyDisplaySize() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// WBP에서 Border의 Content를 원하는 Image/Overlay 등으로 구성할 수 있게
	// Border 자체를 BindWidget으로 노출한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> Border;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> KeyText;

	/** 154x154 / corner 8px. Corners scale with height; only the middle stretches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFrameTexture, Category = "UI|Input")
	TObjectPtr<UTexture2D> FrameTexture;

	/** Non-frame background. Retains FrameSliceMargin and the existing rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNonFrameTexture, Category = "UI|Input")
	TObjectPtr<UTexture2D> NonFrameTexture;

	// Retained for serialized assets and old BP callers; migrate assignments to the two fields above.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use FrameTexture or NonFrameTexture."))
	TObjectPtr<UTexture2D> BorderImage;

	/** 켜면 지정한 FrameTexture/NonFrameTexture 배경과 키 텍스트를 함께 표시한다. 배경이 없으면 DT의 프레임을 사용한다. 끄면 DT의 bShowKeyText 설정을 따른다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetShowKeyTextWithImage, Category = "UI|Input")
	bool bShowKeyTextWithImage = false;

	/** 키 라벨과 override 텍스트 색상. BP 기본값이나 런타임에 변경할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKeyTextColor, Category = "UI|Input")
	FLinearColor KeyTextColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float DisplayHeight = 40.0f;

	/** 텍스트 키 너비 하한. DisplayHeight보다 작으면 DisplayHeight를 사용해 한 글자 키가 정사각형이 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float MinTextKeyWidth = 48.0f;

	/** 텍스트 키 너비 상한. 이를 넘는 라벨만 내부 ScaleBox가 축소한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "1.0"))
	float MaxTextKeyWidth = 88.0f;

	/**
	 * 텍스트 키에서 글자 줄 높이가 차지하는 비율 (Border 상하 padding을 뺀 높이 기준).
	 * 폰트 크기는 DisplayHeight와 이 값으로 계산되며, KeyText에 지정한 폰트 크기는 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TextHeightRatio = 0.75f;

	/** H defines one common cap line for A-Z and mixed-case key names; never recenter each label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUseCaplineAlignment, Category = "UI|Layout")
	bool bUseCaplineAlignment = true;

	/** 라벨이 정사각형 키를 넘을 때만 좌우에 각각 추가하는 여백. DisplayHeight에 대한 비율이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LongLabelSidePaddingRatio = 0.15f;

	/** Nine-slice margin으로 공용 frame이 키 라벨에 맞춰 늘어난다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Layout")
	FMargin FrameSliceMargin = FMargin(0.15f);

	/** 기본 BindKey WBP에 할당하는 공용 테이블. Row Name은 엔진 키 이름과 같아야 한다. */
	// 기존 WBP 할당이 유효하도록 직렬화된 프로퍼티 이름을 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input", meta = (DisplayName = "Key Texture Table", RequiredAssetDataTags = "RowStructure=/Script/Outlier.InputKeyTextureDataRow"))
	TObjectPtr<UDataTable> MouseKeyTextureTable;

	/** 텍스트로 표시하는 키의 HUD 라벨 테이블. Row Name은 엔진 키 이름이며, 행이 없으면 엔진 표시 이름을 사용한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input", meta = (RequiredAssetDataTags = "RowStructure=/Script/Outlier.InputKeyDisplayNameDataRow"))
	TObjectPtr<UDataTable> KeyDisplayNameTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Input")
	TObjectPtr<UInputAction> WatchedInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input")
	FText MissingKeyText;

private:
	friend class SInputKeyFrame;
	bool PaintScaledFrame(const FGeometry& Geometry, FSlateWindowElementList& Elements,
		int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const;

	// Owns the texture reference and tint while Border's ordinary background is suppressed.
	UPROPERTY(Transient)
	FSlateBrush ScaledFrameBrush;

	bool bDrawScaledFrame = false;

	UFUNCTION()
	void HandleInputActionKeyChanged(UInputAction* ChangedInputAction, FKey NewKey);

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	// 액션 키를 갱신하고, 조회 성공 후에도 활성 매핑의 변경을 구독한다.
	void EnsureWatchedKeyIsResolved();

	void BindSettingsSubsystem();
	void UnbindSettingsSubsystem();
	void RefreshBorderBrush();
	void RefreshKeyTextColor();
	FText GetKeyDisplayName(FKey Key) const;
	const FInputKeyTextureDataRow* FindKeyTextureRow(FKey Key) const;
	void ShowKeyText(const FText& Text, FKey Key = FKey());
	void ShowKeyImage(const FInputKeyTextureDataRow& Row);
	void RefreshKeyLayout();
	void RefreshKeyTextAlignment(float FontScale = 1.f);
	void RefreshKeyTextAlignmentForPaint(const FGeometry& Geometry);
	/** DisplayHeight에 맞춘 KeyText 폰트. Slate 폰트 측정을 사용할 수 없으면 false. */
	bool GetFittedKeyFont(FSlateFontInfo& OutFont) const;
	FMargin GetTextBorderPadding() const;
	bool ShowResolvedKey(FKey Key);
	void BindControlMappingsRebuiltDelegate();
	void UnbindControlMappingsRebuiltDelegate();
	void SetIsMissingKey(bool bNewIsMissingKey);

	bool bIsMissingKey = false;

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

	/** WBP에 지정된 KeyText 폰트. 크기만 DisplayHeight 기준으로 덮어쓴다. */
	FSlateFontInfo DefaultKeyFont;
	bool bHasDefaultKeyFont = false;

	FVector2D DefaultKeyTextTranslation = FVector2D::ZeroVector;
	bool bHasDefaultKeyTextTranslation = false;

	bool bUseExplicitKey = false;

	TSet<FName> WarnedMissingMouseKeys;
	bool bHasDefaultBorderBrush = false;
	bool bDisplayingImageKey = false;

	bool bIsConstructed = false;
};
