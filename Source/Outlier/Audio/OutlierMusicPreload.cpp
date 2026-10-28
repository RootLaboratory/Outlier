#include "Audio/OutlierAudioSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Outlier.h"
#include "Sound/SoundBase.h"

uint64 UOutlierAudioSubsystem::RequestMusicPreload(UWorld* World, const TArray<FGameplayTag>& Contexts)
{
	if (!IsValid(World) || World->bIsTearingDown || World->GetGameInstance() != GetGameInstance()
		|| IsRunningDedicatedServer() || World->GetNetMode() == NM_DedicatedServer || Contexts.IsEmpty())
	{
		return 0;
	}

	const uint64 RequestId = ++NextMusicPreloadRequestId;
	FMusicPreloadRequest& Request = MusicPreloadRequests.Add(RequestId);
	Request.World = World;
	for (FGameplayTag Context : Contexts) Request.Contexts.AddUnique(Context);
	ResumeMusicPreloads();
	return RequestId;
}

EOutlierMusicPreloadState UOutlierAudioSubsystem::GetMusicPreloadState(uint64 RequestId) const
{
	const FMusicPreloadRequest* Request = MusicPreloadRequests.Find(RequestId);
	return Request ? Request->State : EOutlierMusicPreloadState::Released;
}

void UOutlierAudioSubsystem::ResumeMusicPreloads()
{
	// 이미 로드된 Bank나 음원은 이 함수 안에서 바로 완료될 수 있으므로 재진입을 막는다.
	if (bResumingMusicPreloads || MusicPreloadRequests.IsEmpty()) return;
	TGuardValue<bool> ResumingGuard(bResumingMusicPreloads, true);
	const FGameplayTag Type = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM"));
	TArray<uint64> RequestIds;
	MusicPreloadRequests.GetKeys(RequestIds);
	for (uint64 RequestId : RequestIds)
	{
		FMusicPreloadRequest& Request = MusicPreloadRequests.FindChecked(RequestId);
		if (!Request.World.IsValid() || Request.World->bIsTearingDown)
		{
			ReleaseMusicPreload(RequestId);
			continue;
		}
		if (Request.State == EOutlierMusicPreloadState::Waiting)
		{
			// Bank 발견 → Definition 로드 → 정확한 Context 후보 수집 순서로 한 번만 진행한다.
			if (!bBankDiscoveryComplete) continue;
			FDiscoveredBank* Bank = DiscoveredBanksByType.Find(Type);
			if (Bank && !Bank->bContentLoaded)
			{
				LoadBankContent(Type);
				if (BankContentLoadHandles.Contains(Type)) continue;
			}

			const TArray<FRuntimeCatalogEntry>* Entries = CatalogEntriesByType.Find(Type);
			for (FGameplayTag Context : Request.Contexts)
			{
				bool bFound = false;
				if (Context.IsValid() && Entries)
				{
					for (const FRuntimeCatalogEntry& Entry : *Entries)
					{
						if (Entry.RequiredContext == Context && Entry.Weight > 0.0f && !Entry.Sound.IsNull())
						{
							Request.SoundPaths.Add(Entry.Sound.ToSoftObjectPath());
							bFound = true;
						}
					}
				}
				Request.bMissingContext |= !bFound;
			}
			if (Request.bMissingContext)
			{
				UE_LOG(LogOutlier, Warning, TEXT("[Audio] BGM preload %llu has missing Context/Variant entries."), RequestId);
			}

			// 선로드에서는 가중치 선택을 하지 않는다. 이후 재생에서 고를 수 있는 모든 후보를 유지한다.
			Request.State = EOutlierMusicPreloadState::Loading;
			for (const FSoftObjectPath& Path : Request.SoundPaths) AcquireMusicPreloadSound(Path);
		}

		if (Request.State != EOutlierMusicPreloadState::Loading) continue;
		// 일부 후보가 실패해도 나머지 로드는 유지하되, 요청 전체를 Ready로 보고하지 않는다.
		bool bComplete = true;
		bool bFailed = Request.bMissingContext;
		for (const FSoftObjectPath& Path : Request.SoundPaths)
		{
			const FMusicPreloadSound& Sound = MusicPreloadSounds.FindChecked(Path);
			bComplete &= Sound.bComplete;
			bFailed |= Sound.bFailed;
		}
		if (bComplete)
		{
			Request.State = bFailed ? EOutlierMusicPreloadState::Failed : EOutlierMusicPreloadState::Ready;
		}
	}
}

void UOutlierAudioSubsystem::AcquireMusicPreloadSound(const FSoftObjectPath& Path)
{
	if (FMusicPreloadSound* Shared = MusicPreloadSounds.Find(Path))
	{
		++Shared->Users;
		return;
	}
	FMusicPreloadSound& Sound = MusicPreloadSounds.Add(Path);
	Sound.Users = 1;
	const uint64 Serial = Sound.Serial = ++NextMusicPreloadSoundSerial;
	if (Cast<USoundBase>(Path.ResolveObject()))
	{
		HandleMusicPreloadSoundLoaded(Path, Serial);
		return;
	}

	// 재생용 핸들과 수명은 분리한다. 같은 패키지의 진행 중 로드는 StreamableManager가 공유한다.
	Sound.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleMusicPreloadSoundLoaded, Path, Serial));
	if (!Sound.Handle.IsValid()) HandleMusicPreloadSoundLoaded(Path, Serial);
}

void UOutlierAudioSubsystem::HandleMusicPreloadSoundLoaded(FSoftObjectPath Path, uint64 Serial)
{
	FMusicPreloadSound* Shared = MusicPreloadSounds.Find(Path);
	// 같은 경로를 해제 후 다시 요청했더라도 이전 완료 callback이 새 요청을 완료시키면 안 된다.
	if (!Shared || Shared->Serial != Serial || Shared->bComplete) return;
	USoundBase* Sound = Cast<USoundBase>(Path.ResolveObject());
	Shared->bComplete = true;
	Shared->bFailed = !Sound;
	if (Sound)
	{
		RetainedMusicPreloadSounds.Add(Path, Sound);
		// 시작 청크 캐시를 요청하지만 완료 통지는 없는 API다. Ready를 청크 준비 보장으로 사용하지 않는다.
		UGameplayStatics::PrimeSound(Sound);
	}
	else
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Audio] Failed to preload BGM '%s'."), *Path.ToString());
	}
	ResumeMusicPreloads();
}

void UOutlierAudioSubsystem::ReleaseMusicPreload(uint64 RequestId)
{
	FMusicPreloadRequest Request;
	if (!MusicPreloadRequests.RemoveAndCopyValue(RequestId, Request)) return;
	for (const FSoftObjectPath& Path : Request.SoundPaths)
	{
		FMusicPreloadSound* Shared = MusicPreloadSounds.Find(Path);
		if (!Shared || --Shared->Users > 0) continue;
		TSharedPtr<FStreamableHandle> Handle = Shared->Handle;
		MusicPreloadSounds.Remove(Path);
		RetainedMusicPreloadSounds.Remove(Path);
		if (Handle.IsValid()) Handle->CancelHandle();
	}
	// 재생 중인 AudioComponent의 참조와 재생 상태는 건드리지 않는다.
}

void UOutlierAudioSubsystem::ReleaseMusicPreloadsForWorld(UWorld* World)
{
	TArray<uint64> RequestIds;
	MusicPreloadRequests.GetKeys(RequestIds);
	for (uint64 RequestId : RequestIds)
	{
		if (MusicPreloadRequests.FindChecked(RequestId).World == TWeakObjectPtr<UWorld>(World)) ReleaseMusicPreload(RequestId);
	}
}

void UOutlierAudioSubsystem::ClearMusicPreloads()
{
	TArray<uint64> RequestIds;
	MusicPreloadRequests.GetKeys(RequestIds);
	for (uint64 RequestId : RequestIds) ReleaseMusicPreload(RequestId);
}
