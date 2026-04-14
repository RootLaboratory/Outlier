////// Fill out your copyright notice in the Description page of Project Settings.


#include "VisualEventSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectionMarkDefinition.h"
#include "TrailEffectDefinition.h"

#include "Niagara/Public/NiagaraComponent.h"
#include "Niagara/Classes/NiagaraSystem.h"
#include "Niagara/Public/NiagaraFunctionLibrary.h"

void UVisualEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    
	//UE_LOG(LogTemp, Error, TEXT("WORLDSUBSYSTEM Initalize"));
}

void UVisualEventSubsystem::Deinitialize()
{
}

void UVisualEventSubsystem::SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation)
{
    if (!Def || !Def->DecalMaterial)
    {
      //  UE_LOG(LogTemp, Error, TEXT("NO DECAL "));

        return;
    }

 //   UE_LOG(LogTemp, Error, TEXT(" DECAL WORKS "));


    UGameplayStatics::SpawnDecalAtLocation(
        GetWorld(),
        Def->DecalMaterial,
        Def->DecalSize,
        Location,
        Rotation + Def->RotationOffset,
        Def->LifeSpan
    );
}

void UVisualEventSubsystem::SpawnBeamTrail(const UTrailEffectDefinition* Def, const FVector& Start, const FVector& End)
{
	if (!Def || !Def->FXAsset)
	{

		//UE_LOG(LogTemp, Error, TEXT("NO DEF EFFEECT "));

		return;
	}

	const FVector FinalStart = Start + Def->StartOffset;
	const FVector FinalEnd = End + Def->EndOffset;

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		const FVector Direction = (FinalEnd - FinalStart).GetSafeNormal();
		const float Speed = Def->Speed;

		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalStart,
			Direction.Rotation() + Def->RotationOffset,
			Def->Scale,
			true,
			true);

		if (Comp)
		{
			Comp->SetVariableVec3(TEXT("User.Direction"), Direction);
			Comp->SetVariableFloat(TEXT("User.Speed"), Speed);
			Comp->SetAutoDestroy(true);
		}

	}
	else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
	{


		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			Particle,
			FinalStart,
			(FinalEnd - FinalStart).Rotation() + Def->RotationOffset,
			Def->Scale,
			true);
	}
}

void UVisualEventSubsystem::SpawnProjectileTrail(const UTrailEffectDefinition* Def, USceneComponent* AttachTarget)
{
	if (!Def || !Def->FXAsset || !AttachTarget)
	{
		return;
	}

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Niagara,
			AttachTarget,
			Def->AttachSocketName,
			Def->RelativeLocation,
			Def->RelativeRotation,
			Def->Scale,
			EAttachLocation::KeepRelativeOffset,
			true,
			ENCPoolMethod::None,
			true,
			true);

		if (Comp && Def->LifeSpan > 0.f)
		{
			Comp->SetAutoDestroy(true);
		}
	}
	else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
	{
		UGameplayStatics::SpawnEmitterAttached(
			Particle,
			AttachTarget,
			Def->AttachSocketName,
			Def->RelativeLocation,
			Def->RelativeRotation,
			Def->Scale,
			EAttachLocation::KeepRelativeOffset,
			true);
	}
}

void UVisualEventSubsystem::SpawnMarkFromHit(UProjectionMarkDefinition* Definition, const FHitResult& HitResult)
{
}


void UVisualEventSubsystem::PlaySoundAtLocation(UProjectionMarkDefinition* Definition, FVector Location)
{
}
