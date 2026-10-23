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
	FGameplayTag MusicTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}
}

FGameplayTag UOutlierAudioSubsystem::ResolveMusicContext(const FOutlierAudioPlayRequest& Request) const
{
	if (Request.EventTag != MusicTag(TEXT("Audio.Type.BGM"))) return FGameplayTag();
	for (const TCHAR* Name : {
		TEXT("Audio.Context.Bgm.inbattle.Stop"),
		TEXT("Audio.Context.Bgm.inbattle.End"),
		TEXT("Audio.Context.Bgm.inbattle.Start"),
		TEXT("Audio.Context.Bgm.inbattle.Loop"),
		TEXT("Audio.Context.Bgm.intro.main") })
	{
		const FGameplayTag Context = MusicTag(Name);
		if (Request.ContextTags.HasTagExact(Context)) return Context;
	}
	return FGameplayTag();
}

void UOutlierAudioSubsystem::HandleMusicContext(FGameplayTag Context)
{
	if (Context == MusicTag(TEXT("Audio.Context.Bgm.inbattle.Start")))
	{
		SetCombatMusicActive(true);
	}
	else if (Context == MusicTag(TEXT("Audio.Context.Bgm.inbattle.End")))
	{
		SetCombatMusicActive(false);
	}
	else if (Context == MusicTag(TEXT("Audio.Context.Bgm.inbattle.Stop")))
	{
		SetCombatMusicActive(false, false);
	}
	else if (Context == MusicTag(TEXT("Audio.Context.Bgm.inbattle.Loop")))
	{
		bFrontendMusic = false;
		bCombatMusic = true;
		if (DesiredMusicContext != Context) RequestMusic(Context);
	}
	else if (Context == MusicTag(TEXT("Audio.Context.Bgm.intro.main")))
	{
		StartFrontendMusic();
	}
}

void UOutlierAudioSubsystem::StartFrontendMusic()
{
	if (bFrontendMusic) return;
	bFrontendMusic = true;
	bCombatMusic = false;
	RequestMusic(MusicTag(TEXT("Audio.Context.Bgm.intro.main")));
}

void UOutlierAudioSubsystem::EnterGameplayMusic()
{
	if (!bFrontendMusic) return;
	bFrontendMusic = false;
	RequestMusic(FGameplayTag());
}

void UOutlierAudioSubsystem::SetCombatMusicActive(bool bActive, bool bPlayEnd)
{
	if (bCombatMusic == bActive)
	{
		// A reset must cancel an End that is still loading or playing, too.
		if (!bActive && !bPlayEnd && !bFrontendMusic) RequestMusic(FGameplayTag());
		return;
	}
	bCombatMusic = bActive;
	bFrontendMusic = false;
	RequestMusic(bActive ? MusicTag(TEXT("Audio.Context.Bgm.inbattle.Start"))
		: bPlayEnd ? MusicTag(TEXT("Audio.Context.Bgm.inbattle.End")) : FGameplayTag());
}

void UOutlierAudioSubsystem::StopMusicPlayback()
{
	++MusicRevision;
	if (MusicLoadHandle.IsValid()) MusicLoadHandle->CancelHandle();
	MusicLoadHandle.Reset();
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleMusicFinished);
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}
}

void UOutlierAudioSubsystem::RequestMusic(FGameplayTag Context)
{
	StopMusicPlayback();
	DesiredMusicContext = Context;
	if (!Context.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(MusicTickerHandle);
		MusicTickerHandle.Reset();
	}
	else if (!MusicTickerHandle.IsValid())
	{
		MusicTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickMusic), 0.1f);
	}
}

bool UOutlierAudioSubsystem::TickMusic(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World && World->IsPaused()) return true;
	if (!DesiredMusicContext.IsValid() || MusicComponent || MusicLoadHandle.IsValid()
		|| !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer) return true;
	const FGameplayTag Type = MusicTag(TEXT("Audio.Type.BGM"));
	if (!CatalogEntriesByType.Contains(Type))
	{
		LoadBankContent(Type);
		return true;
	}
	FGameplayTagContainer Contexts;
	Contexts.AddTag(DesiredMusicContext);
	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Type, Contexts);
	// Do not use an unrelated fallback for a missing music context.
	if (!Entry || Entry->RequiredContext != DesiredMusicContext || Entry->Sound.IsNull()) return true;
	const TSoftObjectPtr<USoundBase> Sound = Entry->Sound;
	if (!Sound.IsValid())
	{
		const uint64 Revision = MusicRevision;
		MusicLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Sound.ToSoftObjectPath(),
			FStreamableDelegate::CreateWeakLambda(this, [this, Revision]()
			{
				if (MusicRevision == Revision)
				{
					MusicLoadHandle.Reset();
					TickMusic(0.0f);
				}
			}));
		return true;
	}
	MusicBaseVolume = Entry->VolumeMultiplier;
	// Only frontend music survives map travel; gameplay music belongs to this world.
	MusicComponent = UGameplayStatics::SpawnSound2D(World, Sound.Get(),
		MusicBaseVolume * GetCombinedVolumeMultiplier(EOutlierAudioVolumeType::BGM),
		Entry->PitchMultiplier, 0.0f, nullptr, bFrontendMusic, false);
	if (MusicComponent)
	{
		SyncAudioPause(MusicComponent);
		MusicComponent->OnAudioFinished.AddUniqueDynamic(this, &ThisClass::HandleMusicFinished);
	}
	return true;
}

void UOutlierAudioSubsystem::HandleMusicFinished()
{
	if (bCombatMusic && DesiredMusicContext == MusicTag(TEXT("Audio.Context.Bgm.inbattle.Start")))
	{
		RequestMusic(MusicTag(TEXT("Audio.Context.Bgm.inbattle.Loop")));
	}
	else if ((bCombatMusic && DesiredMusicContext == MusicTag(TEXT("Audio.Context.Bgm.inbattle.Loop")))
		|| (bFrontendMusic && DesiredMusicContext == MusicTag(TEXT("Audio.Context.Bgm.intro.main"))))
	{
		// Also repeat finite waves; already-looping Sound assets never finish naturally.
		RequestMusic(DesiredMusicContext);
	}
	else
	{
		RequestMusic(FGameplayTag());
	}
}

void UOutlierAudioSubsystem::UpdateMusicVolume()
{
	if (MusicComponent)
	{
		MusicComponent->SetVolumeMultiplier(
			MusicBaseVolume * GetCombinedVolumeMultiplier(EOutlierAudioVolumeType::BGM));
	}
}
