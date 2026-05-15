// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrossHairBase.h"
#include "MainUIBase.h"
#include "DynamicCrossHair.generated.h"


class ULocalPlayerUISubSystem;



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
	UPROPERTY()
	TObjectPtr<ULocalPlayerUISubSystem> CachedUISubsystem;
};

