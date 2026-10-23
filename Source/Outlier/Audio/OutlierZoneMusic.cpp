#include "Audio/OutlierAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	FGameplayTag ZoneMusicType()
	{
		return FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM"));
	}

	// 볼륨 0 이면 엔진이 소리를 정리할 수 있어, 페이드 바닥에서도 들리지 않을 만큼만 남긴다.
	constexpr float MinZoneMusicFadeAlpha = 0.001f;
}

void UOutlierAudioSubsystem::SetZoneMusic(const UObject* Requester, FGameplayTag Context, bool bEnabled,
	float FadeInDuration, float FadeOutDuration)
{
	if (bStoppingAllLocalAudio || !Requester || !Context.IsValid()) return;
	UWorld* World = Requester->GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	if (ZoneMusicWorld.IsValid() && ZoneMusicWorld.Get() != World) ClearZoneMusic();
	ZoneMusicWorld = World;

	// 영역당 요청은 하나. 다시 들어오면 맨 뒤( 최우선 )로 옮긴다.
	ZoneMusicRequests.RemoveAll([Requester](const FZoneMusicRequest& Request) { return Request.Requester == Requester; });
	if (bEnabled) ZoneMusicRequests.Add({ Requester, Context, FadeInDuration, FadeOutDuration });
	RefreshZoneMusicTargets();

	if (!ZoneMusicTickerHandle.IsValid())
	{
		ZoneMusicTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickZoneMusic));
	}
}

const UOutlierAudioSubsystem::FZoneMusicRequest* UOutlierAudioSubsystem::RefreshZoneMusicTargets()
{
	ZoneMusicRequests.RemoveAll([](const FZoneMusicRequest& Request) { return !Request.Requester.IsValid(); });
	const FZoneMusicRequest* Desired = ZoneMusicRequests.IsEmpty() ? nullptr : &ZoneMusicRequests.Last();
	for (FZoneMusicTrack& Track : ZoneMusicTracks)
	{
		const bool bDesired = Desired && Track.Context == Desired->Context;
		Track.TargetAlpha = bDesired ? 1.0f : 0.0f;
		if (bDesired)
		{
			Track.FadeInDuration = Desired->FadeInDuration;
			Track.FadeOutDuration = Desired->FadeOutDuration;
		}
	}
	// 다른 곡으로 바뀌었으면 받던 로드는 버린다.
	if (ZoneMusicLoadHandle.IsValid() && (!Desired || Desired->Context != ZoneMusicLoadingContext))
	{
		ZoneMusicLoadHandle->CancelHandle();
		ZoneMusicLoadHandle.Reset();
	}
	return Desired;
}

void UOutlierAudioSubsystem::TryStartZoneMusicTrack(const FZoneMusicRequest& Request)
{
	if (ZoneMusicLoadHandle.IsValid()) return;
	const FGameplayTag Type = ZoneMusicType();
	if (!CatalogEntriesByType.Contains(Type))
	{
		LoadBankContent(Type);
		return;
	}
	FGameplayTagContainer Contexts;
	Contexts.AddTag(Request.Context);
	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Type, Contexts);
	// 없는 Context 에 엉뚱한 기본 곡이 깔리지 않게 정확히 일치할 때만 재생한다.
	if (!Entry || Entry->RequiredContext != Request.Context || Entry->Sound.IsNull()) return;
	if (!Entry->Sound.IsValid())
	{
		ZoneMusicLoadingContext = Request.Context;
		ZoneMusicLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			Entry->Sound.ToSoftObjectPath(), FStreamableDelegate());
		return;
	}

	UAudioComponent* AudioComponent = UGameplayStatics::SpawnSound2D(ZoneMusicWorld.Get(), Entry->Sound.Get(),
		Entry->VolumeMultiplier * GetCombinedVolumeMultiplier(Entry->VolumeType) * MinZoneMusicFadeAlpha,
		Entry->PitchMultiplier, 0.0f, nullptr, false, false);
	if (!AudioComponent) return;
	SyncAudioPause(AudioComponent);

	FZoneMusicTrack& Track = ZoneMusicTracks.AddDefaulted_GetRef();
	Track.Component = AudioComponent;
	Track.Context = Request.Context;
	Track.BaseVolume = Entry->VolumeMultiplier;
	Track.VolumeType = Entry->VolumeType;
	Track.FadeInDuration = Request.FadeInDuration;
	Track.FadeOutDuration = Request.FadeOutDuration;
}

bool UOutlierAudioSubsystem::TickZoneMusic(float DeltaTime)
{
	const UWorld* World = ZoneMusicWorld.Get();
	if (World && !World->bIsTearingDown && World->IsPaused()) return true;
	if (!World || World->bIsTearingDown)
	{
		ClearZoneMusic();
	}
	else
	{
		if (ZoneMusicLoadHandle.IsValid() && ZoneMusicLoadHandle->HasLoadCompleted()) ZoneMusicLoadHandle.Reset();
		const FZoneMusicRequest* Desired = RefreshZoneMusicTargets();
		if (Desired && !ZoneMusicTracks.ContainsByPredicate(
			[Desired](const FZoneMusicTrack& Track) { return Track.Context == Desired->Context; }))
		{
			TryStartZoneMusicTrack(*Desired);
		}
	}

	for (int32 Index = ZoneMusicTracks.Num() - 1; Index >= 0; --Index)
	{
		FZoneMusicTrack& Track = ZoneMusicTracks[Index];
		UAudioComponent* AudioComponent = Track.Component.Get();
		// 루프가 아닌 웨이브가 끝났으면 버리고, 아직 원하는 곡이면 다음 틱에 처음부터 다시 튼다.
		if (!AudioComponent || !AudioComponent->IsPlaying())
		{
			if (AudioComponent) AudioComponent->DestroyComponent();
			ZoneMusicTracks.RemoveAtSwap(Index);
			continue;
		}

		const float PreviousAlpha = Track.FadeAlpha;
		const float Duration = Track.TargetAlpha > Track.FadeAlpha ? Track.FadeInDuration : Track.FadeOutDuration;
		Track.FadeAlpha = Duration > 0.0f
			? FMath::FInterpConstantTo(Track.FadeAlpha, Track.TargetAlpha, DeltaTime, 1.0f / Duration)
			: Track.TargetAlpha;
		if (Track.TargetAlpha <= 0.0f && Track.FadeAlpha <= 0.0f)
		{
			AudioComponent->Stop();
			AudioComponent->DestroyComponent();
			ZoneMusicTracks.RemoveAtSwap(Index);
			continue;
		}
		if (Track.FadeAlpha != PreviousAlpha) ApplyZoneMusicVolume(Track);
	}

	if (!ZoneMusicTracks.IsEmpty() || !ZoneMusicRequests.IsEmpty() || ZoneMusicLoadHandle.IsValid()) return true;
	ZoneMusicTickerHandle.Reset();
	return false;
}

void UOutlierAudioSubsystem::ApplyZoneMusicVolume(const FZoneMusicTrack& Track) const
{
	if (UAudioComponent* AudioComponent = Track.Component.Get())
	{
		AudioComponent->SetVolumeMultiplier(Track.BaseVolume * GetCombinedVolumeMultiplier(Track.VolumeType)
			* FMath::Max(Track.FadeAlpha, MinZoneMusicFadeAlpha));
	}
}

void UOutlierAudioSubsystem::UpdateZoneMusicVolumes()
{
	for (const FZoneMusicTrack& Track : ZoneMusicTracks) ApplyZoneMusicVolume(Track);
}

void UOutlierAudioSubsystem::ClearZoneMusic()
{
	if (ZoneMusicLoadHandle.IsValid()) ZoneMusicLoadHandle->CancelHandle();
	ZoneMusicLoadHandle.Reset();
	for (const FZoneMusicTrack& Track : ZoneMusicTracks)
	{
		if (UAudioComponent* AudioComponent = Track.Component.Get())
		{
			AudioComponent->Stop();
			AudioComponent->DestroyComponent();
		}
	}
	ZoneMusicTracks.Reset();
	ZoneMusicRequests.Reset();
	ZoneMusicWorld.Reset();
}
