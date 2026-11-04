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
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

void UVisualEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bAcceptTaggedEffects = GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (!bAcceptTaggedEffects)
	{
		return;
	}

	TaggedDefinitions = GetDefault<UVisualEventSettings>()->TaggedEffects;
	TArray<FSoftObjectPath> Paths;
	for (const TPair<FGameplayTag, FTaggedVisualEventDefinition>& Entry : TaggedDefinitions)
	{
		if (!Entry.Value.DecalDef.IsNull())
		{
			Paths.AddUnique(Entry.Value.DecalDef.ToSoftObjectPath());
		}
		if (!Entry.Value.TrailEffectDef.IsNull())
		{
			Paths.AddUnique(Entry.Value.TrailEffectDef.ToSoftObjectPath());
		}
	}

	// 로딩 중인 참조는 NULL 설정과 구분한다. 완료 전 요청은 보관하고 월드 종료 시 폐기한다.
	if (!Paths.IsEmpty())
	{
		bTaggedEffectsLoading = true;
		TaggedEffectsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
			FStreamableDelegate::CreateUObject(this, &UVisualEventSubsystem::FinishLoadingTaggedEffects));
		if (!TaggedEffectsLoadHandle.IsValid())
		{
			FinishLoadingTaggedEffects();
		}
	}
}

void UVisualEventSubsystem::Deinitialize()
{
	bAcceptTaggedEffects = false;
	bTaggedEffectsLoading = false;
	if (TaggedEffectsLoadHandle.IsValid())
	{
		TaggedEffectsLoadHandle->CancelHandle();
		TaggedEffectsLoadHandle.Reset();
	}
	PendingTaggedEffects.Reset();
	LoadedTaggedEffects.Reset();
	TaggedDefinitions.Reset();
	ActiveMarks.Reset();
	Super::Deinitialize();
}

void UVisualEventSubsystem::FinishLoadingTaggedEffects()
{
	if (!bAcceptTaggedEffects)
	{
		return;
	}

	// Definition과 내부 hard reference는 UPROPERTY 캐시에 남겨 GC 이후에도 재사용한다.
	for (const TPair<FGameplayTag, FTaggedVisualEventDefinition>& Entry : TaggedDefinitions)
	{
		FVisualEventSet& Loaded = LoadedTaggedEffects.Add(Entry.Key);
		Loaded.DecalDef = Entry.Value.DecalDef.Get();
		Loaded.TrailEffectDef = Entry.Value.TrailEffectDef.Get();
		if ((!Entry.Value.DecalDef.IsNull() && !Loaded.DecalDef)
			|| (!Entry.Value.TrailEffectDef.IsNull() && !Loaded.TrailEffectDef))
		{
			UE_LOG(LogTemp, Warning, TEXT("Visual Event asset load failed for Context %s; using fallback."),
				*Entry.Key.ToString());
		}
	}
	bTaggedEffectsLoading = false;

	// 재생을 마칠 때까지 대기 요청의 기존 효과도 UPROPERTY로 유지한다.
	for (const FPendingTaggedVisualEvent& Request : PendingTaggedEffects)
	{
		PlayTaggedEffectsAtLocation(Request.Contexts, Request.Location, Request.Rotation, Request.Fallback);
	}
	PendingTaggedEffects.Reset();
}

FVisualEventSet UVisualEventSubsystem::ResolveTaggedEffects(const TArray<FGameplayTag>& Contexts,
	const TMap<FGameplayTag, FVisualEventSet>& Definitions, const FVisualEventSet& Fallback)
{
	FVisualEventSet Result;
	auto FillMissingEffects = [&Result](const FVisualEventSet& Candidate)
	{
		if (!Result.DecalDef && Candidate.DecalDef && Candidate.DecalDef->DecalMaterial)
		{
			Result.DecalDef = Candidate.DecalDef;
		}
		if (!Result.TrailEffectDef && Candidate.TrailEffectDef && Candidate.TrailEffectDef->FXAsset)
		{
			Result.TrailEffectDef = Candidate.TrailEffectDef;
		}
	};

	// 표면 → 명시적 Default → 기준 Context 순서. 데칼과 VFX는 서로 다른 단계에서 보완될 수 있다.
	for (const FGameplayTag& Context : Contexts)
	{
		if (const FVisualEventSet* Candidate = Definitions.Find(Context))
		{
			FillMissingEffects(*Candidate);
		}
		if (Result.DecalDef && Result.TrailEffectDef)
		{
			break;
		}
	}
	FillMissingEffects(Fallback);
	return Result;
}

void UVisualEventSubsystem::PlayTaggedEffectsAtLocation(const TArray<FGameplayTag>& Contexts,
	const FVector& Location, const FRotator& Rotation, const FVisualEventSet& Fallback)
{
	if (!bAcceptTaggedEffects || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (bTaggedEffectsLoading)
	{
		// 초기 로딩이 길어져도 일회성 효과가 무한히 쌓이지 않도록 최근 요청만 남긴다.
		static constexpr int32 MaxPendingTaggedEffects = 128;
		if (PendingTaggedEffects.Num() >= MaxPendingTaggedEffects)
		{
			PendingTaggedEffects.RemoveAt(0, 1, EAllowShrinking::No);
		}
		FPendingTaggedVisualEvent& Pending = PendingTaggedEffects.AddDefaulted_GetRef();
		Pending.Contexts = Contexts;
		Pending.Location = Location;
		Pending.Rotation = Rotation;
		Pending.Fallback = Fallback;
		Pending.Fallback.SoundDef = nullptr;
		return;
	}

	const FVisualEventSet Effects = ResolveTaggedEffects(Contexts, LoadedTaggedEffects, Fallback);
	if (Effects.DecalDef)
	{
		SpawnMarkAtLocation(Effects.DecalDef, Location, Rotation);
	}
	if (Effects.TrailEffectDef)
	{
		SpawnEffectAtLocation(Effects.TrailEffectDef, Location, Rotation);
	}
}

void UVisualEventSubsystem::SpawnMarkAtLocation(UProjectionMarkDefinition* Def, FVector Location, FRotator Rotation)
{
	if (bEndingPresentation) return;
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

const FName UVisualEventSubsystem::TriggerParameterName(TEXT("User.Trigger"));

bool UVisualEventSubsystem::HasUserParameter(const UNiagaraSystem* System, FName ParameterName)
{
	if (!System)
	{
		return false;
	}

	// 키가 "User." 접두어를 가질 수도, 없을 수도 있어 둘 다 비교한다.
	FString ShortString = ParameterName.ToString();
	ShortString.RemoveFromStart(TEXT("User."));
	const FName ShortName(*ShortString);

	TArray<FNiagaraVariable> UserParameters;
	System->GetExposedParameters().GetUserParameters(UserParameters);
	return UserParameters.ContainsByPredicate([&](const FNiagaraVariable& Variable)
	{
		return Variable.GetName() == ParameterName || Variable.GetName() == ShortName;
	});
}

bool UVisualEventSubsystem::SupportsTrigger(const UNiagaraSystem* System)
{
	if (!System)
	{
		return false;
	}

	// 발사마다 User 파라미터 목록을 훑지 않도록 시스템별로 한 번만 조회한다.
	// 월드 서브시스템이라 PIE 를 새로 띄우면 캐시도 새로 만들어져, 에디터에서 에셋을 고친 뒤에도 다시 조회된다.
	if (const bool* Cached = TriggerSupportCache.Find(System))
	{
		return *Cached;
	}

	const bool bSupports = HasUserParameter(System, TriggerParameterName);
	TriggerSupportCache.Add(System, bSupports);
	return bSupports;
}

void UVisualEventSubsystem::ApplyTrigger(UNiagaraComponent* Comp, const UNiagaraSystem* System, const UTrailEffectDefinition* Def)
{
	if (!SupportsTrigger(System))
	{
		// Always 를 잘못 고른 경우만 시스템별로 한 번 알린다.
		if (Def->TriggerMode == ETrailTriggerMode::Always && !WarnedMissingTrigger.Contains(System))
		{
			WarnedMissingTrigger.Add(System);
			UE_LOG(LogTemp, Warning, TEXT("[VisualEvent] %s has no %s. Set TriggerMode to Auto on %s."),
				*System->GetName(), *TriggerParameterName.ToString(), *Def->GetName());
		}
		return;
	}

	// AutoRelease 풀에서 재사용된 컴포넌트는 이전 발사의 User 파라미터를 그대로 들고 있다.
	// Never 여도 false 를 명시해야 다른 Definition 이 남긴 true 로 발사되지 않는다.
	// NM_WeaponTrigger 는 이전 프레임에 캐시한 값과 달라질 때 한 번 발사하고, Activate(true) 로 리셋되면 캐시가 false 로 시작한다.
	Comp->SetVariableBool(TriggerParameterName, Def->TriggerMode != ETrailTriggerMode::Never);
}

void UVisualEventSubsystem::SpawnBeamTrail(const UTrailEffectDefinition* Def, const FVector& Start, const FVector& End)
{
	if (bEndingPresentation) return;
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

		// 트레이서( NS_Trail_*_Original )는 World Space 에미터로, 컴포넌트 위치를 총구( TraceStart )로 쓰고 User.End 까지 날아간다.
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
			ApplyTrigger(Comp, Niagara, Def);
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
	if (bEndingPresentation) return;
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

void UVisualEventSubsystem::BeginEndingPresentation()
{
	bEndingPresentation = true;
	bAcceptTaggedEffects = false;
	PendingTaggedEffects.Reset();
	for (const auto& Mark : ActiveMarks)
	{
		if (Mark.IsValid()) Mark->DestroyComponent();
	}
	ActiveMarks.Reset();
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld()) It->DeactivateImmediate();
	}
	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->DeactivateSystem();
			It->SetVisibility(false);
		}
	}
}

void UVisualEventSubsystem::PlaySoundAtLocation(USoundDefinition* SoundDefinition, FVector Location)
{
	if (bEndingPresentation) return;
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
	if (bEndingPresentation) return;
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
	if (bEndingPresentation) return;
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
	if (bEndingPresentation) return;
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
