////// Fill out your copyright notice in the Description page of Project Settings.


#include "VisualEventSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectionMarkDefinition.h"
#include "TrailEffectDefinition.h"
#include "SoundDefinition.h"
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
        UE_LOG(LogTemp, Error, TEXT("NO DECAL "));
        return;
    }

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
	    UE_LOG(LogTemp, Error, TEXT("NO DEF EFFEECT "));
		return;
	}

	const FVector FinalStart = Start + Def->StartOffset;
	const FVector FinalEnd = End + Def->EndOffset;

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		const FVector Direction = (FinalEnd - FinalStart).GetSafeNormal();

		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalStart,
			Direction.Rotation() + Def->RotationOffset,
			Def->Scale,
			true,
			false,
			ENCPoolMethod::None,
			false);

		if (Comp)
		{
			Comp->SetVariablePosition(Def->StartParameterName, FinalStart);
			Comp->SetVariablePosition(Def->EndParameterName, FinalEnd);
			Comp->Activate(true);
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
		UNiagaraFunctionLibrary::SpawnSystemAttached(
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

void UVisualEventSubsystem::PlaySoundAtLocation(USoundDefinition* SoundDefinition, FVector Location)
{
	if (!SoundDefinition || !SoundDefinition->Sound)
	{
		UE_LOG(LogTemp, Error, TEXT("NO Sound "));

		return;
	}

	const FVector FinalLocation = Location + SoundDefinition->LocationOffset;

	UGameplayStatics::SpawnSoundAtLocation(
		GetWorld(),
		SoundDefinition->Sound,
		FinalLocation,
		FRotator::ZeroRotator,
		SoundDefinition->VolumeMultiplier,
		SoundDefinition->PitchMultiplier,
		SoundDefinition->StartTime,
		SoundDefinition->AttenuationSettings,
		SoundDefinition->ConcurrencySettings,
		SoundDefinition->bAutoDestroy
	);
}

// Weapon trails are handled by the weapon; hit effects only require a world-space location.
void UVisualEventSubsystem::FeaturesEffect(FVector Location, FRotator Rotation, FVisualEventSet& EffectSet)
{
	if (EffectSet.DecalDef)
	{
		//UE_LOG(LogTemp, Error, TEXT("DecalDef Valid "));

		SpawnMarkAtLocation(EffectSet.DecalDef, Location, Rotation);
	}

	if (EffectSet.TrailEffectDef)
	{
		//UE_LOG(LogTemp, Error, TEXT("DecalDef Valid "));

		SpawnEffectAtLocation(EffectSet.TrailEffectDef, Location, Rotation);
	}

	if (EffectSet.SoundDef)
	{
		//UE_LOG(LogTemp, Error, TEXT("Sound Valid "));

		PlaySoundAtLocation(EffectSet.SoundDef, Location);
	}


}

void UVisualEventSubsystem::SpawnMuzzleEffect(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation)
{
	if (!Def || !Def->FXAsset)
	{
		return;
	}

	const FVector FinalLocation = Location +Def->RelativeLocation;
	const FRotator FinalRotation = Rotation + Def->RelativeRotation + Def->RotationOffset;

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalLocation,
			FinalRotation,
			Def->Scale,
			true,
			true);

		if (Comp)
		{
			Comp->SetAutoDestroy(true);
		}
	}
	else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			Particle,
			FinalLocation,
			FinalRotation,
			Def->Scale,
			true);
	}
}

void UVisualEventSubsystem::SpawnEffectAtLocation(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation)
{
	if (!Def || !Def->FXAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("NO DEF EFFECT"));
		return;
	}

	const FVector FinalLocation = Location + Def->RelativeLocation;
	const FRotator FinalRotation = Rotation + Def->RelativeRotation + Def->RotationOffset;

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalLocation,
			FinalRotation,
			Def->Scale,
			true,
			true);

		if (Comp)
		{
			Comp->SetAutoDestroy(true);
		}
	}
	else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			Particle,
			FinalLocation,
			FinalRotation,
			Def->Scale,
			true);
	}
}
