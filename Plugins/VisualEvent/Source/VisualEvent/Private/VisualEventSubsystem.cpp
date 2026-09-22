////// Fill out your copyright notice in the Description page of Project Settings.


#include "VisualEventSubsystem.h"
#include "Components/DecalComponent.h"
#include "Particles/ParticleSystemComponent.h"
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
	ActiveMarks.Reset();
}

void UVisualEventSubsystem::SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation)
{
    if (!Def || !Def->DecalMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("NO DECAL "));
        return;
    }

	ActiveMarks.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Mark)
	{
		return !Mark.IsValid();
	});

	while (ActiveMarks.Num() >= MaxActiveMarks)
	{
		if (UDecalComponent* OldestMark = ActiveMarks[0].Get())
		{
			OldestMark->DestroyComponent();
		}
		ActiveMarks.RemoveAt(0, 1, EAllowShrinking::No);
	}

    UDecalComponent* SpawnedMark = UGameplayStatics::SpawnDecalAtLocation(
        GetWorld(),
        Def->DecalMaterial,
        Def->DecalSize,
        Location,
        Rotation + Def->RotationOffset,
        Def->LifeSpan
    );

	if (SpawnedMark)
	{
		ActiveMarks.Add(SpawnedMark);
	}
}

void UVisualEventSubsystem::SpawnBeamTrail(const UTrailEffectDefinition* Def, const FVector& Start, const FVector& End)
{
	if (!Def || !Def->FXAsset)
	{
	   // UE_LOG(LogTemp, Error, TEXT("NO DEF EFFEECT "));
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
			ENCPoolMethod::AutoRelease,
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

void UVisualEventSubsystem::SpawnMuzzleEffect(const UTrailEffectDefinition* Def, const FVector& Location, const FRotator& Rotation, bool bFirstPerson)
{
	if (!Def || !Def->FXAsset)
	{
		return;
	}

	const FVector FinalScale = Def->GetViewpointScale(bFirstPerson);

	// RelativeLocation 은 총구 기준 오프셋이다. 월드 좌표에 그냥 더하면 월드 축을 따라
	// 움직여서 캐릭터가 보는 방향에 따라 앞/옆으로 제각각 어긋난다. 총구 회전으로 돌려서 적용한다.
	// ( 회전 오프셋을 바꿔도 위치가 흔들리지 않도록, 합성 후가 아닌 원본 Rotation 을 쓴다 )
	const FVector FinalLocation = Location + Rotation.RotateVector(Def->RelativeLocation);

	// FRotator 덧셈은 Pitch/Yaw/Roll 을 각각 더할 뿐이라 올바른 회전 합성이 아니다.
	// 각도가 커지면 짐벌처럼 어긋나므로 쿼터니언으로 합성한다.
	const FRotator FinalRotation = (Rotation.Quaternion()
		* Def->RelativeRotation.Quaternion()
		* Def->RotationOffset.Quaternion()).Rotator();

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		// 주의: FinalScale 은 컴포넌트 스케일이라 스프라이트 렌더러에는 크기로 반영되지 않는다.
		// Niagara 는 Local Space 여도 파티클 "위치"만 컴포넌트 트랜스폼으로 변환하고,
		// 스프라이트 쿼드 크기는 Particles.SpriteSize 에서 월드 단위로 그대로 가져간다.
		// 스프라이트 기반 이펙트의 크기를 바꾸려면 시스템에 User 파라미터를 두고 크기 모듈에 곱해야 한다.
		// ( 메시 렌더러는 컴포넌트 스케일이 반영된다 )
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalLocation,
			FinalRotation,
			FinalScale,
			true,
			true,
			ENCPoolMethod::AutoRelease,
			true);
	}
	else if (UParticleSystem* Particle = Cast<UParticleSystem>(Def->FXAsset))
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			Particle,
			FinalLocation,
			FinalRotation,
			FinalScale,
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

	// SpawnMuzzleEffect 와 동일 — 오프셋은 로컬 기준으로 돌려서 적용하고, 회전은 쿼터니언으로 합성한다.
	const FVector FinalLocation = Location + Rotation.RotateVector(Def->RelativeLocation);
	const FRotator FinalRotation = (Rotation.Quaternion()
		* Def->RelativeRotation.Quaternion()
		* Def->RotationOffset.Quaternion()).Rotator();

	if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Def->FXAsset))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			Niagara,
			FinalLocation,
			FinalRotation,
			Def->Scale,
			true,
			true,
			ENCPoolMethod::AutoRelease,
			true);
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
