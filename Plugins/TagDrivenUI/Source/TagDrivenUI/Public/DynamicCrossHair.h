// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrossHairBase.h"
#include "MainUIBase.h"
#include "DynamicCrossHair.generated.h"


class ULocalPlayerUISubSystem;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;



UCLASS()
class TAGDRIVENUI_API UDynamicCrossHair : public UCrossHairBase
{
	GENERATED_BODY()

public:
	  virtual void NativeConstruct() override;

	  virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnCrossHairTick(float InDeltaTime);

	virtual void OnAiming()override; 

	virtual void OnAimingOff() override; 

	void SpawnAttackSign(EAttackSign InAttackSign) override;

	UFUNCTION(BlueprintImplementableEvent, Category ="Crosshair")
	void BP_SpawnAttackSign(EAttackSign InAttackSign);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")

	void CrossHairCollapsed();

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")

	void CrossHairVisible();

	void On_RepShoot();

	void SetPlayerState(EUIPlayerState InState);

	void AddShootSpread();

	float CalculateStateSpread() const;

	void UpdateMoveSpread();

	void UpdateShootSpread(float InDeltaTime);

	void UpdateMoveSpreadRecovery(float InDeltaTime);

	void UpdateFinalSpread();

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	TObjectPtr<UMaterialInterface> AttackSignMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	TObjectPtr<UMaterialInterface> Adjusted;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	TObjectPtr<UMaterialInterface> Critical;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	TObjectPtr<UMaterialInterface> Kill;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	FName AttackSignTextureParameterName = TEXT("Texture");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign")
	FName AttackSignTimeParameterName = TEXT("Time");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack Sign", meta = (ClampMin = "0.01"))
	float AttackSignDuration = 0.25f;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AttackSign_LeftTop;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AttackSign_LeftDown;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AttackSign_RightTop;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UImage> AttackSign_RightDown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	float CrossHairLength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")

	float CrossHairThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")

	float Offset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")

	float Ratio;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	EUIPlayerState CurrentState = EUIPlayerState::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float MaxMoveSpread = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float ShootSpreadStep = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float ShootRecoverSpeed = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float MoveRecoverSpeed = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float StateRecoverSpeed = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float MaxSpread = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float CurrentMoveSpread = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float CurrentStateSpread = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float CurrentShootSpread = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float FinalSpread = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float MoveSpreadReferenceSpeed = 600.f;


private:
	void InitializeAttackSignImages();
	UMaterialInterface* ResolveAttackSignMaterial(EAttackSign InAttackSign) const;
	bool ApplyAttackSignMaterial(UMaterialInterface* InMaterial);
	bool CanReuseAttackSignMIDs(EAttackSign InAttackSign) const;
	void UpdateAttackSign(float InDeltaTime);
	void SetAttackSignTime(float InNormalizedTime);
	void StopAttackSign();
	void SetAttackSignVisibility(ESlateVisibility InVisibility);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture>> AttackSignTextures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> AttackSignMIDs;

	EAttackSign CachedAttackSignType = EAttackSign::None;
	float AttackSignElapsedTime = 0.f;
	bool bAttackSignActive = false;

	UPROPERTY()
	TObjectPtr<ULocalPlayerUISubSystem> CachedUISubsystem;
};
