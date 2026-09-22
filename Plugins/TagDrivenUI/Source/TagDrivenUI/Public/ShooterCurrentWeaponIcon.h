// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EventDrivenUI.h"
#include "ShooterMainWidget.h"
#include "ShooterCurrentWeaponIcon.generated.h"

class UImage;
class UTexture2D;

USTRUCT(BlueprintType)
struct TAGDRIVENUI_API FWeaponCarouselLayoutSettings
{
	GENERATED_BODY()

	// 해당 무기가 선택되었을 때 멈추는 위치. 위젯 중앙 기준 좌표다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FVector2D ActivePosition = FVector2D(0.0f, -34.0f);

	// 해당 무기가 왼쪽 비활성 슬롯에 있을 때 멈추는 위치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FVector2D InactiveLeftPosition = FVector2D(-72.0f, 17.0f);

	// 해당 무기가 오른쪽 비활성 슬롯에 있을 때 멈추는 위치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FVector2D InactiveRightPosition = FVector2D(72.0f, 17.0f);

	// 해당 무기가 현재 선택되어 위쪽 기준 위치에 있을 때의 X/Y Scale.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FVector2D ActiveScale = FVector2D(1.0f, 1.0f);

	// 해당 무기가 비활성 상태로 아래쪽에 있을 때의 X/Y Scale.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FVector2D InactiveScale = FVector2D(0.6f, 0.6f);
};

UCLASS()
class TAGDRIVENUI_API UShooterCurrentWeaponIcon : public UEventDrivenUI
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetCurrentWeapon(EWidgetWeaponType WeaponType);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EWidgetWeaponType GetCurrentWeaponType() const { return CurrentWeaponType; }

private:
	// 기존 단일 이미지 방식에서 무기 타입에 맞는 텍스처를 찾는다.
	UTexture2D* GetTextureForWeapon(EWidgetWeaponType WeaponType) const;

	// BP에 지정된 텍스처를 각 캐러셀 이미지에 연결한다.
	void RefreshWeaponTextures();

	// 현재 회전값을 기준으로 세 아이콘의 위치, 크기, Tint, ZOrder를 한 번에 갱신한다.
	void RefreshCarouselVisuals();

	// 회전형 이미지가 아직 없는 기존 BP는 단일 이미지 교체 방식을 유지한다.
	void SetLegacyWeaponImage(EWidgetWeaponType WeaponType);
	bool HasCarouselWidgets() const;

	// 화면 배치 순서를 Rifle=0, Melee=1, Pistol=2로 고정한다.
	static int32 GetCarouselIndex(EWidgetWeaponType WeaponType);
	const FWeaponCarouselLayoutSettings& GetLayoutSettings(EWidgetWeaponType WeaponType) const;
	FVector2D GetCarouselPosition(
		const FWeaponCarouselLayoutSettings& LayoutSettings,
		float DisplayAngle) const;

public:
	// 기존 단일 이미지 BP와의 호환용. 회전형 이미지 3개가 모두 바인딩되면 사용하지 않는다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> CurrentWeaponImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> RifleWeaponImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PistolWeaponImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> MeleeWeaponImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> UnarmedTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> PistolTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> RifleTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UTexture2D> MeleeTexture;

	// 세 이미지의 Canvas Slot 크기를 통일한다. 원본 텍스처 크기는 배치에 영향을 주지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel")
	FVector2D IconSize = FVector2D(100.0f, 52.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel")
	float FocusAngleDegrees = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel", meta = (ClampMin = "0.01", UIMin = "0.05", UIMax = "1.0"))
	float RotationDuration = 0.22f;

	// 세 위치 사이 곡선의 휘어지는 정도. 0이면 직선에 가깝고 값이 클수록 회전감이 커진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PositionCurveTension = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel|Layout")
	FWeaponCarouselLayoutSettings RifleLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel|Layout")
	FWeaponCarouselLayoutSettings PistolLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel|Layout")
	FWeaponCarouselLayoutSettings MeleeLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel")
	FLinearColor SelectedTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel")
	FLinearColor InactiveTint = FLinearColor(0.35f, 0.4f, 0.45f, 0.3f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Carousel")
	EWidgetWeaponType PreviewWeaponType = EWidgetWeaponType::Rifle;

private:
	EWidgetWeaponType CurrentWeaponType = EWidgetWeaponType::Unarmed;

	// 회전 시작값과 목표값을 따로 보관해 Tick에서 부드럽게 보간한다.
	float CurrentRotationDegrees = 0.0f;
	float StartRotationDegrees = 0.0f;
	float TargetRotationDegrees = 0.0f;
	float RotationElapsed = 0.0f;
	bool bCarouselInitialized = false;
	bool bIsRotating = false;
};
