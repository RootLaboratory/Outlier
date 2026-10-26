// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/OutlierDamageReceiver.h"
#include "PartnerShieldSphere.generated.h"

class APartnerCharacter;
class AShooterCharacter;
class UMaterialInstanceDynamic;
class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API APartnerShieldSphere : public AActor, public IOutlierDamageReceiver
{
	GENERATED_BODY()
	
public:	
	APartnerShieldSphere();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) override;

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void InitializeShield(AShooterCharacter* InShieldTarget, APartnerCharacter* InSourcePartner);

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void InitializeShieldForActor(AActor* InShieldTarget, APartnerCharacter* InSourcePartner);

	AActor* GetShieldTargetActor() const;

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetShieldMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetShieldRadius(float InShieldRadius);

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetTargetRelativeLocation(FVector InTargetRelativeLocation);

	void SetRecipientLocalTransform(const FTransform& InLocalTransform);

	UFUNCTION(BlueprintCallable, Category = "Shield")
	void EndShield();

	USphereComponent* GetShieldCollision() const { return ShieldCollision; }
	UStaticMeshComponent* GetShieldVisual() const { return ShieldVisual; }
	AShooterCharacter* GetShieldTarget() const { return ShieldTarget; }
	APartnerCharacter* GetSourcePartner() const { return SourcePartner; }
	float GetShieldRadius() const { return ShieldRadius; }
	FVector GetTargetRelativeLocation() const { return TargetRelativeLocation; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shield")
	TObjectPtr<USphereComponent> ShieldCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shield")
	TObjectPtr<UStaticMeshComponent> ShieldVisual;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield|Material")
	TObjectPtr<UMaterialInterface> ShieldMaterial;

	UPROPERTY(ReplicatedUsing = OnRep_ShieldTarget, BlueprintReadOnly, Category = "Shield")
	TObjectPtr<AShooterCharacter> ShieldTarget;

	// 기존 Shooter 대상 BP 속성/함수는 유지하고 범용 대상만 별도로 확장한다.
	UPROPERTY(ReplicatedUsing = OnRep_ShieldTarget, BlueprintReadOnly, Category = "Shield")
	TObjectPtr<AActor> ShieldTargetActor;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Shield")
	TObjectPtr<APartnerCharacter> SourcePartner;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_ShieldRadius, BlueprintReadOnly, Category = "Shield")
	float ShieldRadius = 120.0f;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Shield")
	FVector TargetRelativeLocation = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_RecipientLocalTransform)
	FTransform RecipientLocalTransform = FTransform::Identity;

	UFUNCTION()
	void OnRep_RecipientLocalTransform();

	UFUNCTION()
	void OnRep_ShieldTarget();

	UFUNCTION()
	void OnRep_ShieldRadius();

private:
	UPROPERTY(Replicated)
	FVector BaseShieldScale = FVector::OneVector;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShieldMID;

	void ApplyTargetTransform();
	void ApplyShieldRadius();
	void ApplyTraceOnlyCollision();
	void RefreshOwnerVisibility();
	void EnsureDynamicMaterial();
};
