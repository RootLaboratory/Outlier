#include "Audio/OutlierAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "VisualEventSubsystem.h"
#include "SoundDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Outlier.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "Misc/ScopeExit.h"

namespace
{
	const TCHAR* PlaybackModeName(EOutlierAudioPlaybackMode PlaybackMode)
	{
		switch (PlaybackMode)
		{
		case EOutlierAudioPlaybackMode::TwoD:
			return TEXT("TwoD");
		case EOutlierAudioPlaybackMode::AtLocation:
			return TEXT("AtLocation");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* AudienceName(EOutlierAudioAudience Audience)
	{
		switch (Audience)
		{
		case EOutlierAudioAudience::Local:
			return TEXT("Local");
		case EOutlierAudioAudience::Owner:
			return TEXT("Owner");
		case EOutlierAudioAudience::Relevant:
			return TEXT("Relevant");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* RequestAuthorityName(EOutlierAudioRequestAuthority RequestAuthority)
	{
		switch (RequestAuthority)
		{
		case EOutlierAudioRequestAuthority::Local:
			return TEXT("Local");
		case EOutlierAudioRequestAuthority::OwningClient:
			return TEXT("OwningClient");
		case EOutlierAudioRequestAuthority::Server:
			return TEXT("Server");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* PlaybackPolicyName(EOutlierAudioPlaybackPolicy PlaybackPolicy)
	{
		switch (PlaybackPolicy)
		{
		case EOutlierAudioPlaybackPolicy::OneShot:
			return TEXT("OneShot");
		case EOutlierAudioPlaybackPolicy::Loop:
			return TEXT("Loop");
		case EOutlierAudioPlaybackPolicy::StopLoopThenOneShot:
			return TEXT("StopLoopThenOneShot");
		case EOutlierAudioPlaybackPolicy::StopLoop:
			return TEXT("StopLoop");
		default:
			return TEXT("Unknown");
		}
	}
}

void UOutlierAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	GetDefault<UOutlierSurfaceAudioSettings>()->BuildMappings(SurfaceContextMappings);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Master, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::BGM, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::SFX, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Voice, 1.0f);
	VoiceWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleVoiceWorldCleanup);
	AudioWorldTickHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::HandleAudioWorldTick);
	ReloadCatalog();
}

bool UOutlierAudioSubsystem::PlayLocalVoice(FGameplayTag SoundTag, int32 Priority,
	FGameplayTag RequestTag, ULocalPlayer* Listener)
{
	if (bStoppingAllLocalAudio) return false;
	UWorld* World = GetWorld();
	if (!SoundTag.IsValid() || !RequestTag.IsValid() || !IsValid(Listener)
		|| Listener->GetGameInstance() != GetGameInstance() || !World
		|| World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || Priority < 0 || Priority > 3)
	{
		return false;
	}
	if ((ActiveVoice.Id != 0 && ActiveVoice.Listener == Listener && ActiveVoice.RequestTag == RequestTag)
		|| VoiceQueue.ContainsByPredicate([Listener, RequestTag](const FVoiceRequest& Request)
		{
			return Request.Listener == Listener && Request.RequestTag == RequestTag;
		}))
	{
		return true;
	}
	FVoiceRequest& Request = VoiceQueue.AddDefaulted_GetRef();
	Request.Id = NextVoiceRequestId++;
	Request.SoundTag = SoundTag;
	Request.RequestTag = RequestTag;
	Request.Listener = Listener;
	Request.World = World;
	Request.Priority = Priority;
	if (ActiveVoice.Id != 0 && Priority < ActiveVoice.Priority)
	{
		FinishActiveVoice(false);
	}
	StartNextVoice();
	return true;
}

void UOutlierAudioSubsystem::StartNextVoice()
{
	if (bStartingVoice || ActiveVoice.Id != 0)
	{
		return;
	}
	TGuardValue<bool> Guard(bStartingVoice, true);
	const FGameplayTag VoiceType = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Voice"));
	while (!VoiceQueue.IsEmpty() && ActiveVoice.Id == 0)
	{
		VoiceQueue.RemoveAll([this](const FVoiceRequest& Request)
		{
			return !Request.Listener.IsValid() || !Request.World.IsValid() || Request.World.Get() != GetWorld();
		});
		if (VoiceQueue.IsEmpty())
		{
			return;
		}
		int32 NextIndex = 0;
		for (int32 Index = 1; Index < VoiceQueue.Num(); ++Index)
		{
			if (VoiceQueue[Index].Priority < VoiceQueue[NextIndex].Priority)
			{
				NextIndex = Index;
			}
		}
		FDiscoveredBank* Bank = DiscoveredBanksByType.Find(VoiceType);
		if (!Bank && BankMetadataLoadHandle.IsValid())
		{
			return;
		}
		if (Bank && !Bank->bContentLoaded)
		{
			LoadBankContent(VoiceType);
			// 빈 뱅크는 동기적으로 준비될 수 있다.
			if (!Bank->bContentLoaded)
			{
				if (!BankContentLoadHandles.Contains(VoiceType))
				{
					ActiveVoice = VoiceQueue[NextIndex];
					VoiceQueue.RemoveAt(NextIndex);
					FailActiveVoice();
					continue;
				}
				return;
			}
		}
		ActiveVoice = VoiceQueue[NextIndex];
		VoiceQueue.RemoveAt(NextIndex);
		FGameplayTagContainer ContextTags;
		ContextTags.AddTag(ActiveVoice.SoundTag);
		const FRuntimeCatalogEntry* Entry = ResolveBestEntry(VoiceType, ContextTags);
		if (!Entry || Entry->VolumeType != EOutlierAudioVolumeType::Voice
			|| Entry->PlaybackPolicy != EOutlierAudioPlaybackPolicy::OneShot)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Audio] Voice context '%s' needs a Voice-volume OneShot variant."),
				*ActiveVoice.SoundTag.ToString());
			FailActiveVoice();
			continue;
		}
		FPendingPlay PendingPlay;
		PendingPlay.World = ActiveVoice.World;
		PendingPlay.SourceName = Entry->SourceName;
		PendingPlay.VolumeMultiplier = Entry->VolumeMultiplier;
		PendingPlay.PitchMultiplier = Entry->PitchMultiplier;
		PendingPlay.VolumeType = EOutlierAudioVolumeType::Voice;
		PendingPlay.VoiceRequestId = ActiveVoice.Id;
		if (!QueueOrPlay(*Entry, PendingPlay))
		{
			FailActiveVoice();
		}
	}
}

void UOutlierAudioSubsystem::FailActiveVoice()
{
	if (ActiveVoice.Id == 0)
	{
		return;
	}
	const TWeakObjectPtr<ULocalPlayer> Listener = ActiveVoice.Listener;
	const FGameplayTag RequestTag = ActiveVoice.RequestTag;
	FinishActiveVoice(false);
	OnVoiceUnavailable.Broadcast(Listener.Get(), RequestTag);
}

void UOutlierAudioSubsystem::FinishActiveVoice(bool bCompleted)
{
	if (ActiveVoice.Id == 0)
	{
		return;
	}
	const FVoiceRequest Finished = ActiveVoice;
	ActiveVoice = FVoiceRequest();
	UAudioComponent* Component = VoiceAudioComponent.Get();
	VoiceAudioComponent.Reset();
	if (Component)
	{
		Component->OnAudioFinishedNative.RemoveAll(this);
		if (!bCompleted)
		{
			Component->Stop();
		}
	}
	OnVoiceFinished.Broadcast(Finished.Listener.Get(), Finished.RequestTag, bCompleted);
}

void UOutlierAudioSubsystem::HandleVoiceAudioFinished(UAudioComponent* AudioComponent)
{
	if (AudioComponent == VoiceAudioComponent.Get())
	{
		FinishActiveVoice(true);
		StartNextVoice();
	}
}

void UOutlierAudioSubsystem::StopLocalVoices(ULocalPlayer* Listener)
{
	VoiceQueue.RemoveAll([Listener](const FVoiceRequest& Request) { return Request.Listener == Listener; });
	if (ActiveVoice.Listener == Listener)
	{
		FinishActiveVoice(false);
	}
	StartNextVoice();
}

void UOutlierAudioSubsystem::HandleVoiceWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	ReleaseMusicPreloadsForWorld(World);
	for (auto& Pair : PendingPlaysByType)
	{
		Pair.Value.RemoveAll([World](const auto& Item) { return Item.Key.OriginWorld.Get() == World; });
	}
	for (auto& Pair : PendingResolvedPlaysByType)
	{
		Pair.Value.RemoveAll([World](const auto& Item) { return Item.Value.Get() == World; });
	}
	for (auto& Pair : PendingSurfaceSelectionsBySound)
	{
		Pair.Value.RemoveAll([World](const auto& Item) { return Item.Key.OriginWorld.Get() == World; });
	}
	if (World && World->GetGameInstance() == GetGameInstance()) PendingLegacySounds.Reset();
	if (World && World->GetGameInstance() == GetGameInstance() && !bFrontendMusic)
	{
		bCombatMusic = false;
		RequestMusic(FGameplayTag());
	}
	if (LocalLoopWorld.Get() == World) ClearLocalEmitterLoops();
	VoiceQueue.RemoveAll([World](const FVoiceRequest& Request) { return Request.World.Get() == World; });
	if (ActiveVoice.World.Get() == World)
	{
		FinishActiveVoice(false);
	}
}

void UOutlierAudioSubsystem::Deinitialize()
{
	ClearMusicPreloads();
	FWorldDelegates::OnWorldTickStart.Remove(AudioWorldTickHandle);
	WorldPausedAudioComponents.Reset();
	RequestMusic(FGameplayTag());
	FTSTicker::GetCoreTicker().RemoveTicker(MusicTickerHandle);
	MusicTickerHandle.Reset();
	ClearZoneMusic();
	FTSTicker::GetCoreTicker().RemoveTicker(ZoneMusicTickerHandle);
	ZoneMusicTickerHandle.Reset();
	ClearLocalEmitterLoops();
	FWorldDelegates::OnWorldCleanup.Remove(VoiceWorldCleanupHandle);
	VoiceQueue.Reset();
	FinishActiveVoice(false);
	if (BankMetadataLoadHandle.IsValid())
	{
		BankMetadataLoadHandle->CancelHandle();
	}
	BankMetadataLoadHandle.Reset();

	for (TPair<FGameplayTag, TSharedPtr<FStreamableHandle>>& Pair : BankContentLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	BankContentLoadHandles.Empty();

	for (TPair<FSoftObjectPath, TSharedPtr<FStreamableHandle>>& Pair : ActiveLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}

	ActiveLoadHandles.Empty();
	VolumeMultipliers.Empty();
	ActiveAudioPlaybacks.Empty();
	LoopAudioComponentPool.Empty();
	CancelledLoopAudioInstances.Empty();
	LoopInstances.Empty();
	PendingPlaysBySound.Empty();
	PendingPlaysByType.Empty();
	PendingResolvedPlaysByType.Empty();
	PendingSurfaceSelectionsBySound.Empty();
	PendingLegacySounds.Empty();
	FailedSurfaceSounds.Empty();
	CatalogEntriesByType.Empty();
	DiscoveredBanksByType.Empty();
	LoadedBanks.Empty();

	Super::Deinitialize();
}

bool UOutlierAudioSubsystem::ReloadCatalog()
{
	// 이전 카탈로그의 선로드 요청은 해제한다. 호출자는 새 카탈로그에 다시 요청해야 한다.
	ClearMusicPreloads();
	for (auto& Pair : LocalEmitterLoops)
	{
		StopLoopAudioLocally(Pair.Value.InstanceId);
		Pair.Value.InstanceId = 0;
	}
	VoiceQueue.Reset();
	FinishActiveVoice(false);
	if (BankMetadataLoadHandle.IsValid())
	{
		BankMetadataLoadHandle->CancelHandle();
		BankMetadataLoadHandle.Reset();
	}
	for (TPair<FGameplayTag, TSharedPtr<FStreamableHandle>>& Pair : BankContentLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	BankContentLoadHandles.Empty();
	LoopInstances.Empty();

	CatalogEntriesByType.Empty();
	DiscoveredBanksByType.Empty();
	LoadedBanks.Empty();
	PendingPlaysByType.Empty();
	PendingResolvedPlaysByType.Empty();
	PendingSurfaceSelectionsBySound.Empty();
	PendingLegacySounds.Empty();
	FailedSurfaceSounds.Empty();

	DiscoverAudioBanks();

	// 발견/로드는 비동기로 진행된다. 완료 여부는 IsCatalogReady() 로 확인한다.
	return true;
}

void UOutlierAudioSubsystem::DiscoverAudioBanks()
{
	bBankDiscoveryComplete = false;
	ON_SCOPE_EXIT
	{
		// 발견 자체가 실패한 경우에도 선로드 요청을 무기한 대기시키지 않는다.
		if (!BankMetadataLoadHandle.IsValid())
		{
			bBankDiscoveryComplete = true;
			ResumeMusicPreloads();
		}
	};
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		UE_LOG(LogOutlier, Error, TEXT("[Audio] Asset Manager is not initialized."));
		return;
	}

	TArray<FPrimaryAssetId> BankIds;
	if (!AssetManager->GetPrimaryAssetIdList(UOutlierAudioBank::PrimaryAssetType, BankIds)
		|| BankIds.IsEmpty())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] No Audio Banks were found under /Game/Audio/Banks."));
		return;
	}

	TArray<FSoftObjectPath> BankPaths;
	BankPaths.Reserve(BankIds.Num());
	for (const FPrimaryAssetId& BankId : BankIds)
	{
		BankPaths.Add(AssetManager->GetPrimaryAssetPath(BankId));
	}

	BankMetadataLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		BankPaths,
		FStreamableDelegate::CreateUObject(
			this,
			&UOutlierAudioSubsystem::HandleBankMetadataLoaded,
			BankIds));

	if (!BankMetadataLoadHandle.IsValid())
	{
		UE_LOG(LogOutlier, Error, TEXT("[Audio] Failed to start async load for Audio Banks."));
	}
}

void UOutlierAudioSubsystem::HandleBankMetadataLoaded(TArray<FPrimaryAssetId> BankIds)
{
	BankMetadataLoadHandle.Reset();
	ON_SCOPE_EXIT
	{
		bBankDiscoveryComplete = true;
		ResumeMusicPreloads();
	};

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		return;
	}

	int32 EagerBankCount = 0;
	for (const FPrimaryAssetId& BankId : BankIds)
	{
		const FSoftObjectPath BankPath = AssetManager->GetPrimaryAssetPath(BankId);
		UOutlierAudioBank* Bank = Cast<UOutlierAudioBank>(BankPath.ResolveObject());
		if (!Bank)
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Failed to resolve loaded Audio Bank '%s'."),
				*BankId.ToString());
			continue;
		}

		if (!Bank->TypeTag.IsValid())
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Bank '%s' has an invalid TypeTag and was skipped."),
				*GetNameSafe(Bank));
			continue;
		}

		if (DiscoveredBanksByType.Contains(Bank->TypeTag))
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Multiple Audio Banks use TypeTag '%s'. Only the first discovered is used."),
				*Bank->TypeTag.ToString());
			continue;
		}

		LoadedBanks.Add(Bank);

		FDiscoveredBank& Discovered = DiscoveredBanksByType.Add(Bank->TypeTag);
		Discovered.BankId = BankId;
		Discovered.bLoadImmediately = Bank->bLoadImmediately;
		Discovered.bContentLoaded = false;

		if (Bank->bLoadImmediately)
		{
			++EagerBankCount;
			LoadBankContent(Bank->TypeTag);
		}
	}

	UE_LOG(LogOutlier, Log,
		TEXT("[Audio] Discovered %d Audio Banks (%d eager)."),
		DiscoveredBanksByType.Num(),
		EagerBankCount);
	TArray<FGameplayTag> WaitingTypes;
	PendingPlaysByType.GetKeys(WaitingTypes);
	for (const auto& Pair : PendingResolvedPlaysByType) WaitingTypes.AddUnique(Pair.Key);
	for (FGameplayTag Type : WaitingTypes) FlushPendingPlaysForType(Type);
	StartNextVoice();
}

void UOutlierAudioSubsystem::LoadBankContent(FGameplayTag TypeTag)
{
	FDiscoveredBank* Discovered = DiscoveredBanksByType.Find(TypeTag);
	if (!Discovered || Discovered->bContentLoaded || BankContentLoadHandles.Contains(TypeTag))
	{
		return;
	}

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	UOutlierAudioBank* Bank = AssetManager
		? Cast<UOutlierAudioBank>(AssetManager->GetPrimaryAssetPath(Discovered->BankId).ResolveObject())
		: nullptr;
	if (!Bank)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] LoadBankContent: Bank for Type '%s' is not resolved."),
			*TypeTag.ToString());
		return;
	}

	TArray<FSoftObjectPath> DefinitionPaths;
	DefinitionPaths.Reserve(Bank->Definitions.Num());
	for (const TSoftObjectPtr<UOutlierAudioEventDefinition>& DefinitionSoftPtr : Bank->Definitions)
	{
		const FSoftObjectPath Path = DefinitionSoftPtr.ToSoftObjectPath();
		if (Path.IsValid())
		{
			DefinitionPaths.Add(Path);
		}
	}

	if (DefinitionPaths.IsEmpty())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Bank '%s' (Type '%s') has no Definitions."),
			*GetNameSafe(Bank),
			*TypeTag.ToString());
		Discovered->bContentLoaded = true;
		FlushPendingPlaysForType(TypeTag);
		return;
	}

	TSharedPtr<FStreamableHandle> LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		DefinitionPaths,
		FStreamableDelegate::CreateUObject(
			this,
			&UOutlierAudioSubsystem::HandleBankContentLoaded,
			TypeTag));

	if (!LoadHandle.IsValid())
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Failed to start async load for Bank content '%s'."),
			*TypeTag.ToString());
		Discovered->bContentLoaded = true;
		FlushPendingPlaysForType(TypeTag);
		return;
	}

	BankContentLoadHandles.Add(TypeTag, MoveTemp(LoadHandle));
}

void UOutlierAudioSubsystem::HandleBankContentLoaded(FGameplayTag TypeTag)
{
	BankContentLoadHandles.Remove(TypeTag);

	FDiscoveredBank* Discovered = DiscoveredBanksByType.Find(TypeTag);
	if (!Discovered)
	{
		return;
	}
	Discovered->bContentLoaded = true;

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	UOutlierAudioBank* Bank = AssetManager
		? Cast<UOutlierAudioBank>(AssetManager->GetPrimaryAssetPath(Discovered->BankId).ResolveObject())
		: nullptr;
	if (!Bank)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] HandleBankContentLoaded: Bank for Type '%s' failed to resolve."),
			*TypeTag.ToString());
		FlushPendingPlaysForType(TypeTag);
		return;
	}

	IngestBank(Bank);
	FlushPendingPlaysForType(TypeTag);
	StartNextVoice();
}

bool UOutlierAudioSubsystem::ResolveTypeForContext(
	FGameplayTag Context,
	FGameplayTag& OutType,
	bool& bOutPending)
{
	bOutPending = false;
	OutType = FGameplayTag();
	if (!Context.IsValid())
	{
		return false;
	}

	for (const TPair<FGameplayTag, TArray<FRuntimeCatalogEntry>>& Pair : CatalogEntriesByType)
	{
		if (Pair.Value.ContainsByPredicate(
			[Context](const FRuntimeCatalogEntry& Entry)
			{
				return Entry.RequiredContext == Context;
			}))
		{
			if (OutType.IsValid() && OutType != Pair.Key)
			{
				UE_LOG(LogOutlier, Warning, TEXT("[Audio] Context '%s' belongs to multiple Types."), *Context.ToString());
				OutType = FGameplayTag();
				return false;
			}
			OutType = Pair.Key;
		}
	}

	// 지연 로드 Bank 는 내용이 올라와야 어떤 Context 를 가졌는지 알 수 있다.
	bOutPending = BankMetadataLoadHandle.IsValid();
	for (const TPair<FGameplayTag, FDiscoveredBank>& Pair : DiscoveredBanksByType)
	{
		if (!Pair.Value.bContentLoaded)
		{
			LoadBankContent(Pair.Key);
			bOutPending = true;
		}
	}
	return OutType.IsValid() && !bOutPending;
}

void UOutlierAudioSubsystem::IngestBank(UOutlierAudioBank* Bank)
{
	if (!Bank || !Bank->TypeTag.IsValid())
	{
		return;
	}

	TArray<FRuntimeCatalogEntry>& Entries = CatalogEntriesByType.FindOrAdd(Bank->TypeTag);
	Entries.Reset();

	int32 FlatVariantIndex = 0;
	int32 IngestedCount = 0;
	for (const TSoftObjectPtr<UOutlierAudioEventDefinition>& DefinitionSoftPtr : Bank->Definitions)
	{
		UOutlierAudioEventDefinition* Definition = DefinitionSoftPtr.Get();
		if (!Definition)
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Bank '%s' has a Definition entry that failed to resolve. Path='%s' Null=%d Pending=%d"),
				*GetNameSafe(Bank), *DefinitionSoftPtr.ToSoftObjectPath().ToString(),
				DefinitionSoftPtr.IsNull(), DefinitionSoftPtr.IsPending());
			continue;
		}

		if (Definition->VolumeMultiplier < 0.0f
			|| Definition->PitchMultiplier < 0.0f)
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Definition '%s' has invalid multipliers and was skipped."),
				*GetNameSafe(Definition));
			continue;
		}

		for (int32 VariantIndex = 0; VariantIndex < Definition->Variants.Num(); ++VariantIndex)
		{
			const FOutlierAudioVariant& Variant = Definition->Variants[VariantIndex];
			if ((Variant.Sound.IsNull()
					&& Variant.PlaybackPolicy != EOutlierAudioPlaybackPolicy::StopLoop)
				|| Variant.Weight <= 0.0f)
			{
				UE_LOG(LogOutlier, Error,
					TEXT("[Audio] Definition '%s' variant %d is invalid and was skipped."),
					*GetNameSafe(Definition),
					VariantIndex);
				continue;
			}

			FRuntimeCatalogEntry Entry;
			Entry.SourceName = FString::Printf(
				TEXT("%s/%s[%d]"),
				*Bank->TypeTag.ToString(),
				*GetNameSafe(Definition),
				VariantIndex);
			Entry.RequiredContext = Variant.RequiredContext;
			Entry.Sound = Variant.Sound;
			Entry.PlaybackPolicy = Variant.PlaybackPolicy;
			Entry.LoopContextTag = Variant.LoopContextTag;
			Entry.VariantIndex = FlatVariantIndex++;
			Entry.Weight = Variant.Weight;
			Entry.VolumeMultiplier = Definition->VolumeMultiplier;
			Entry.PitchMultiplier = Definition->PitchMultiplier;
			Entry.VolumeType = Definition->VolumeType;

			Entries.Add(MoveTemp(Entry));
			++IngestedCount;
		}
	}

	/*UE_LOG(LogOutlier, Log,
		TEXT("[Audio] Ingested %d variants for Bank Type '%s'."),
		IngestedCount,
		*Bank->TypeTag.ToString());*/
}

void UOutlierAudioSubsystem::FlushPendingPlaysForType(FGameplayTag TypeTag)
{
	if (TypeTag == FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM"))) ResumeMusicPreloads();
	TArray<TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>> Pending;
	PendingPlaysByType.RemoveAndCopyValue(TypeTag, Pending);

	for (const TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>& Item : Pending)
	{
		if (Item.Key.OrderedContexts.IsEmpty() || Item.Key.OriginWorld.Get() == GetWorld())
			PlayAudio(Item.Key, Item.Value);
	}
	TArray<TPair<FOutlierResolvedAudioPlay, TWeakObjectPtr<UWorld>>> Delivered;
	PendingResolvedPlaysByType.RemoveAndCopyValue(TypeTag, Delivered);
	for (const auto& Item : Delivered)
	{
		if (Item.Value.IsValid() && Item.Value.Get() == GetWorld()) PlayResolvedAudioLocally(Item.Key);
	}
}

bool UOutlierAudioSubsystem::PlayLocal2D(const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::TwoD,
		EOutlierAudioAudience::Local,
		EOutlierAudioRequestAuthority::Local });
}

bool UOutlierAudioSubsystem::PlayLocalAtLocation(const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::AtLocation,
		EOutlierAudioAudience::Local,
		EOutlierAudioRequestAuthority::Local });
}

void UOutlierAudioSubsystem::GetSurfaceContextCandidates(FGameplayTag Context, EPhysicalSurface Surface,
	TArray<FGameplayTag>& OutContexts) const
{
	if (const FOutlierSurfaceContextMapping* Mapping = SurfaceContextMappings.Find(Context))
	{
		Mapping->GetCandidates(Surface, OutContexts);
		if (Context.IsValid()) OutContexts.AddUnique(Context);
		return;
	}
	OutContexts.Reset();
	if (Context.IsValid()) OutContexts.Add(Context);
}

bool UOutlierAudioSubsystem::PlaySurfaceAtLocationFromServer(AActor* Emitter, FGameplayTag Type,
	FGameplayTag Context, EPhysicalSurface Surface, const FVector& Location, USoundDefinition* LegacySound)
{
	if (!IsValid(Emitter) || !Emitter->HasAuthority() || Emitter->GetWorld() != GetWorld()) return false;
	FOutlierAudioPlayRequest Request;
	Request.EmitterActor = Emitter;
	Request.EventTag = Type;
	Request.Location = Location;
	Request.bHasLocation = true;
	Request.bExactContexts = true;
	Request.OriginWorld = GetWorld();
	Request.LegacySound = LegacySound;
	if (LegacySound) PendingLegacySounds.AddUnique(LegacySound);
	GetSurfaceContextCandidates(Context, Surface, Request.OrderedContexts);
	return PlayRelevantAtLocationFromServer(Request);
}

bool UOutlierAudioSubsystem::PlaySurfaceAtLocationLocally(AActor* Emitter, FGameplayTag Type,
	FGameplayTag Context, EPhysicalSurface Surface, const FVector& Location)
{
	if (!IsValid(Emitter) || Emitter->GetWorld() != GetWorld()) return false;
	FOutlierAudioPlayRequest Request;
	Request.EmitterActor = Emitter;
	Request.EventTag = Type;
	Request.Location = Location;
	Request.bHasLocation = true;
	Request.bExactContexts = true;
	Request.OriginWorld = GetWorld();
	GetSurfaceContextCandidates(Context, Surface, Request.OrderedContexts);
	return PlayLocalAtLocation(Request);
}

bool UOutlierAudioSubsystem::IsAnimationAudioSource(USkeletalMeshComponent* Mesh, bool bWorldMeshOnly) const
{
	if (!IsValid(Mesh) || Mesh->GetWorld() != GetWorld() || !GetWorld()
		|| GetWorld()->GetNetMode() == NM_DedicatedServer) return false;
	const ACharacter* Character = Cast<ACharacter>(Mesh->GetOwner());
	if (!Character) return false;
	if (bWorldMeshOnly || !Character->IsLocallyControlled()) return Mesh == Character->GetMesh();
	const AFirstPersonCharacter* FirstPerson = Cast<AFirstPersonCharacter>(Character);
	return Mesh == (FirstPerson ? FirstPerson->GetFirstPersonMesh() : Character->GetMesh());
}

bool UOutlierAudioSubsystem::PlayAnimationAudio(USkeletalMeshComponent* Mesh, FGameplayTag Type,
	FGameplayTag Context, FName SocketName, bool bQuerySurface, bool bWorldMeshOnly, float TraceDistance)
{
	// 발걸음은 숨겨진 로컬 월드 메시도 허용한다. 일반 행동은 시점 메시 하나만 통과한다.
	if (!Context.IsValid() || !IsAnimationAudioSource(Mesh, bQuerySurface || bWorldMeshOnly)) return false;
	ACharacter* Character = CastChecked<ACharacter>(Mesh->GetOwner());
	FVector Location = SocketName != NAME_None && Mesh->DoesSocketExist(SocketName)
		? Mesh->GetSocketLocation(SocketName) : Mesh->GetComponentLocation();
	if (!Type.IsValid())
	{
		bool bPending = false;
		TArray<FGameplayTag> TypeContexts;
		if (bQuerySurface) GetSurfaceContextCandidates(Context, SurfaceType_Default, TypeContexts);
		else TypeContexts.Add(Context);
		for (FGameplayTag TypeContext : TypeContexts)
		{
			if (ResolveTypeForContext(TypeContext, Type, bPending)) break;
			if (bPending) return false;
		}
		if (!Type.IsValid()) return false;
	}
	if (!bQuerySurface)
	{
		FOutlierAudioPlayRequest Request;
		Request.EmitterActor = Character;
		Request.EventTag = Type;
		Request.ContextTags.AddTag(Context);
		Request.Location = Location;
		Request.bHasLocation = true;
		return PlayLocalAtLocation(Request);
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Character);
	if (!Movement || !Movement->IsMovingOnGround() || Character->GetVelocity().SizeSquared2D() < 1.0f
		|| (Shooter && (Shooter->IsSliding() || Shooter->IsDead()))) return false;
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (SocketName == NAME_None || !Mesh->DoesSocketExist(SocketName)
		|| (!Mesh->WasRecentlyRendered() && Mesh->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones))
	{
		Location = Capsule->GetComponentLocation() - FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AnimationSurfaceAudio), false, Character);
	Params.bReturnPhysicalMaterial = true;
	if (const AFirstPersonCharacter* FirstPerson = Cast<AFirstPersonCharacter>(Character))
		Params.AddIgnoredActor(FirstPerson->GetCurrentWeapon());
	if (Shooter)
	{
		if (const UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent())
		{
			for (EWeaponSlot Slot : { EWeaponSlot::Primary, EWeaponSlot::Secondary, EWeaponSlot::Melee })
				Params.AddIgnoredActor(Inventory->GetWeaponInSlot(Slot));
		}
	}
	FHitResult Hit;
	const float Distance = FMath::Clamp(TraceDistance, 1.0f, 200.0f);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector::UpVector * 15.0f,
		Location - FVector::UpVector * Distance, ECC_Visibility, Params) || !Hit.bBlockingHit) return false;
	const EPhysicalSurface Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
	const bool bPlayed = PlaySurfaceAtLocationLocally(Character, Type, Context, Surface, Hit.ImpactPoint);
	if (UVisualEventSubsystem* Visual = GetSurfaceVisualSubsystem())
	{
		TArray<FGameplayTag> Contexts;
		GetSurfaceContextCandidates(Context, Surface, Contexts);
		Visual->PlayTaggedEffectsAtLocation(Contexts, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVisualEventSet());
	}
	return bPlayed;
}

UVisualEventSubsystem* UOutlierAudioSubsystem::GetSurfaceVisualSubsystem()
{
	UWorld* World = GetWorld();
	if (SurfaceVisualWorld.Get() != World || !SurfaceVisualSubsystem.IsValid())
	{
		SurfaceVisualWorld = World;
		SurfaceVisualSubsystem = World ? World->GetSubsystem<UVisualEventSubsystem>() : nullptr;
	}
	return SurfaceVisualSubsystem.Get();
}

void UOutlierAudioSubsystem::SetAnimationAudioLoop(USkeletalMeshComponent* Mesh, FGameplayTag Type,
	FGameplayTag Context, bool bEnabled, bool bWorldMeshOnly, bool bTwoD)
{
	if (!IsValid(Mesh) || !Context.IsValid()) return;
	if (bEnabled)
	{
		if (!Type.IsValid() || !IsAnimationAudioSource(Mesh, bWorldMeshOnly)) return;
		TSet<FGameplayTag>& Tokens = AnimationAudioTokens.FindOrAdd(Mesh);
		if (Tokens.Contains(Context)) return;
		Tokens.Add(Context);
		SetLocalEmitterLoop(Mesh->GetOwner(), Type, Context, true, bTwoD);
		return;
	}
	// 종료 시에는 현재 시점이 달라도 시작을 승인한 메시의 토큰으로 정리한다. 그림자 End는 토큰이 없다.
	TSet<FGameplayTag>* Tokens = AnimationAudioTokens.Find(Mesh);
	if (!Tokens || Tokens->Remove(Context) == 0) return;
	if (Tokens->IsEmpty()) AnimationAudioTokens.Remove(Mesh);
	for (const auto& Pair : AnimationAudioTokens)
	{
		const USkeletalMeshComponent* OtherMesh = Pair.Key.Get();
		if (OtherMesh && OtherMesh->GetOwner() == Mesh->GetOwner() && Pair.Value.Contains(Context)) return;
	}
	SetLocalEmitterLoop(Mesh->GetOwner(), Type, Context, false, bTwoD);
}

void UOutlierAudioSubsystem::StopAnimationAudioLoops(AActor* Emitter)
{
	for (auto It = AnimationAudioTokens.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* Mesh = It.Key().Get();
		if (!Mesh || Mesh->GetOwner() == Emitter)
		{
			if (Mesh)
				for (FGameplayTag Context : It.Value()) SetLocalEmitterLoop(Emitter, FGameplayTag(), Context, false);
			It.RemoveCurrent();
		}
	}
}

bool UOutlierAudioSubsystem::PlayOwner2DFromServer(const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::TwoD,
		EOutlierAudioAudience::Owner,
		EOutlierAudioRequestAuthority::Server });
}

bool UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
	AActor* EmitterActor,
	FGameplayTag TypeTag,
	FGameplayTag ContextTag)
{
	if (!IsValid(EmitterActor) || !TypeTag.IsValid() || !ContextTag.IsValid())
	{
		return false;
	}

	UGameInstance* GameInstance = EmitterActor->GetGameInstance();
	UOutlierAudioSubsystem* AudioSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!AudioSubsystem)
	{
		return false;
	}

	FOutlierAudioPlayRequest Request;
	Request.EventTag = TypeTag;
	Request.ContextTags.AddTag(ContextTag);
	Request.EmitterActor = EmitterActor;
	Request.Location = EmitterActor->GetActorLocation();
	Request.bHasLocation = true;
	return AudioSubsystem->PlayRelevantAtLocationFromServer(Request);
}

bool UOutlierAudioSubsystem::StopTaggedAtLocationFromServer(
	AActor* EmitterActor,
	FGameplayTag TypeTag,
	FGameplayTag ContextTag)
{
	if (!IsValid(EmitterActor) || !TypeTag.IsValid() || !ContextTag.IsValid())
	{
		return false;
	}

	UGameInstance* GameInstance = EmitterActor->GetGameInstance();
	UOutlierAudioSubsystem* AudioSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!AudioSubsystem)
	{
		return false;
	}

	FGameplayTagContainer ContextTags;
	ContextTags.AddTag(ContextTag);
	const int32 PendingTypeCount = AudioSubsystem->PendingPlaysByType.Contains(TypeTag)
		? AudioSubsystem->PendingPlaysByType.Find(TypeTag)->Num()
		: 0;
	/*UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[AudioSpatialDebug][StopRequest] NetMode=%d Type='%s' Context='%s' Emitter='%s' LoopCount=%d PendingTypeCount=%d"),
		static_cast<int32>(AudioSubsystem->GetWorld()->GetNetMode()),
		*TypeTag.ToString(),
		*ContextTag.ToString(),
		*GetNameSafe(EmitterActor),
		AudioSubsystem->LoopInstances.Num(),
		PendingTypeCount);*/
	const FRuntimeCatalogEntry* Entry = AudioSubsystem->ResolveBestEntry(TypeTag, ContextTags);
	if (!Entry)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[AudioCancelDebug] Stop failed: entry not resolved Type=%s Context=%s Emitter=%s"),
			*TypeTag.ToString(),
			*ContextTag.ToString(),
			*GetNameSafe(EmitterActor));
		return false;
	}

	// 정책은 DA가 정한다. OneShot 은 루프로 등록되지 않아 끌 수 없으므로, 코드 기대와 어긋난 DA를 알린다.
	if (Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::OneShot)
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[Audio] Stop requested for a OneShot definition; set its PlaybackPolicy to Loop. Source=%s Context=%s Emitter=%s"),
			*Entry->SourceName,
			*ContextTag.ToString(),
			*GetNameSafe(EmitterActor));
		return false;
	}

	const FGameplayTag LoopContextTag = Entry->LoopContextTag.IsValid()
		? Entry->LoopContextTag
		: Entry->RequiredContext;
	if (!LoopContextTag.IsValid())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[AudioCancelDebug] Stop failed: invalid loop context Type=%s Context=%s Emitter=%s"),
			*TypeTag.ToString(),
			*ContextTag.ToString(),
			*GetNameSafe(EmitterActor));
		return false;
	}

	const FLoopPlaybackKey LoopKey { EmitterActor, LoopContextTag };
	int32* AudioInstanceId = AudioSubsystem->LoopInstances.Find(LoopKey);
	if (!AudioInstanceId)
	{
	/*	UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[AudioCancelDebug] Stop failed: loop instance not found Type=%s Context=%s LoopContext=%s Emitter=%s"),
			*TypeTag.ToString(),
			*ContextTag.ToString(),
			*LoopContextTag.ToString(),
			*GetNameSafe(EmitterActor));*/
		return false;
	}

	const int32 InstanceId = *AudioInstanceId;
	/*UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[AudioSpatialDebug][LoopFoundForStop] Type='%s' Context='%s' LoopContext='%s' Emitter='%s' AudioInstanceId=%d"),
		*TypeTag.ToString(),
		*ContextTag.ToString(),
		*LoopContextTag.ToString(),
		*GetNameSafe(EmitterActor),
		InstanceId);*/
	const bool bStopped = AudioSubsystem->GetWorld()->GetNetMode() == NM_Standalone
		? AudioSubsystem->StopLoopAudioLocally(InstanceId)
		: AudioSubsystem->StopLoopAtLocationFromServer(InstanceId);
	AudioSubsystem->LoopInstances.Remove(LoopKey);
	/*UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[AudioSpatialDebug][StopResult] Type='%s' Context='%s' Emitter='%s' AudioInstanceId=%d Stopped=%d RemainingLoopCount=%d"),
		*TypeTag.ToString(),
		*ContextTag.ToString(),
		*GetNameSafe(EmitterActor),
		InstanceId,
		bStopped ? 1 : 0,
		AudioSubsystem->LoopInstances.Num());*/
	return bStopped;
}

bool UOutlierAudioSubsystem::StopLoopAtLocationFromServer(
	int32 AudioInstanceId)
{
	if (AudioInstanceId == 0 || !GetWorld())
	{
		return false;
	}

	if (GetWorld()->GetNetMode() == NM_Standalone)
	{
		return StopLoopAudioLocally(AudioInstanceId);
	}

	if (GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	bool bDelivered = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFirstPersonPlayerController* PlayerController = Cast<AFirstPersonPlayerController>(It->Get());
		if (!PlayerController)
		{
			continue;
		}

		//멀티 기반의 사운드가 종료되는 경우 클라를 통해서 notify시켜 전체가 안듣게 만든다.
		PlayerController->ClientStopResolvedAudio(AudioInstanceId);
		bDelivered = true;
	}

	return bDelivered;
}

bool UOutlierAudioSubsystem::PlayRelevantAtLocationFromOwningClient(
	const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::AtLocation,
		EOutlierAudioAudience::Relevant,
		EOutlierAudioRequestAuthority::OwningClient });
}

bool UOutlierAudioSubsystem::PlayRelevantAtLocationFromServer(
	const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::AtLocation,
		EOutlierAudioAudience::Relevant,
		EOutlierAudioRequestAuthority::Server });
}

bool UOutlierAudioSubsystem::PlayAudio(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierAudioExecutionPolicy& Policy)
{
	if (bStoppingAllLocalAudio) return false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (Request.bExactContexts && Request.OriginWorld.Get() != World) return false;

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::Local
		&& Policy.Audience != EOutlierAudioAudience::Local)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Local request authority only supports a Local audience. Type='%s'."),
			*Request.EventTag.ToString());
		return false;
	}

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::Server
		&& World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Server-authoritative Type '%s' was rejected on a client."),
			*Request.EventTag.ToString());
		return false;
	}

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::OwningClient
		&& World->GetNetMode() == NM_Client)
	{
		AFirstPersonPlayerController* RequestingController =
			ResolveLocalRequestController(Request.EmitterActor);
		if (!RequestingController)
		{
			UE_LOG(LogOutlier, Warning,
				TEXT("[Audio] OwningClient Type '%s' needs an EmitterActor owned by the local player. Controller-less emitters must start this request on the server."),
				*Request.EventTag.ToString());
			return false;
		}

		RequestingController->ServerRequestRelevantAudioAtLocation(Request);
		return true;
	}

	// These BGM tags own their loading and Start -> Loop transition on the listener.
	// Deliver the context before bank loading so End/Stop can cancel a pending Start.
	if (Policy.PlaybackMode == EOutlierAudioPlaybackMode::TwoD)
	{
		const FGameplayTag MusicContext = ResolveMusicContext(Request);
		if (MusicContext.IsValid())
		{
			FOutlierResolvedAudioPlay ResolvedPlay;
			ResolvedPlay.EventTag = Request.EventTag;
			ResolvedPlay.MusicContextTag = MusicContext;
			return RouteByAudience(Request, ResolvedPlay, Policy.Audience);
		}
	}

	// Type 콘텐츠가 아직 로드되지 않은 Deferred Bank( BGM/Voice 등 )라면, 재생 요청 자체가
	// 로드 트리거가 된다. 큐잉해두고 로드가 끝나면 FlushPendingPlaysForType 이 재시도한다.
	if (Request.bExactContexts && BankMetadataLoadHandle.IsValid())
	{
		PendingPlaysByType.FindOrAdd(Request.EventTag).Emplace(Request, Policy);
		return true;
	}
	if (!CatalogEntriesByType.Contains(Request.EventTag))
	{
		const FDiscoveredBank* Discovered = DiscoveredBanksByType.Find(Request.EventTag);
		if (!Discovered)
		{
			if (Request.bExactContexts)
			{
				if (!Request.LegacySound) return false;
				FOutlierResolvedAudioPlay Fallback;
				Fallback.LegacySound = Request.LegacySound;
				Fallback.bAtLocation = true;
				Fallback.Location = Request.Location;
				return RouteByAudience(Request, Fallback, Policy.Audience);
			}
			UE_LOG(LogOutlier, Warning,
				TEXT("[Audio] Unknown audio Type '%s'."),
				*Request.EventTag.ToString());
			return false;
		}

		if (!Discovered->bContentLoaded)
		{
			PendingPlaysByType.FindOrAdd(Request.EventTag).Emplace(Request, Policy);
			UE_LOG(LogOutlier, Verbose, TEXT("[Audio] Queued Type='%s' Context='%s' while Bank content loads."),
				*Request.EventTag.ToString(), *Request.ContextTags.ToString());
			LoadBankContent(Request.EventTag);
			return true;
		}
		// bContentLoaded 인데 카탈로그에 없다 = 그 Bank 에 유효한 Variant 가 하나도 없었던 경우.
		// ResolveBestEntry 에서 통상적인 실패 로그를 남기도록 그대로 진행한다.
	}

	const FRuntimeCatalogEntry* Entry = Request.bExactContexts
		? ResolveOrderedEntry(Request.EventTag, Request.OrderedContexts)
		: ResolveBestEntry(Request.EventTag, Request.ContextTags);
	if (!Entry)
	{
		if (Request.bExactContexts && Request.LegacySound)
		{
			FOutlierResolvedAudioPlay Fallback;
			Fallback.LegacySound = Request.LegacySound;
			Fallback.bAtLocation = true;
			Fallback.Location = Request.Location;
			return RouteByAudience(Request, Fallback, Policy.Audience);
		}
		return false;
	}

	if (Request.bExactContexts && !Entry->Sound.Get())
	{
		// 서버는 음원 로드 성공까지 기다린 뒤 한 후보만 전송한다. 실패한 경로는 다음 Default 후보로 넘긴다.
		const FSoftObjectPath Path = Entry->Sound.ToSoftObjectPath();
		PendingSurfaceSelectionsBySound.FindOrAdd(Path).Emplace(Request, Policy);
		if (ActiveLoadHandles.Contains(Path)) return true;
		TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			Path, FStreamableDelegate::CreateUObject(this, &ThisClass::HandleSoundLoaded, Path));
		if (Handle.IsValid())
		{
			ActiveLoadHandles.Add(Path, MoveTemp(Handle));
			return true;
		}
		HandleSoundLoaded(Path);
		return true;
	}
	const bool bLooping = Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::Loop;
	const FGameplayTag LoopContextTag = Entry->LoopContextTag.IsValid()
		? Entry->LoopContextTag
		: Entry->RequiredContext;
	const FLoopPlaybackKey LoopKey { Request.EmitterActor, LoopContextTag };

	const bool bStopsLoop = Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::StopLoopThenOneShot
		|| Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::StopLoop;
	if ((bLooping || bStopsLoop)
		&& !LoopContextTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Persistent playback Type='%s' Context='%s' has no loop identity."),
			*Request.EventTag.ToString(),
			*Entry->RequiredContext.ToString());
		return false;
	}

	int32* ExistingInstanceId = (bLooping || bStopsLoop)
		? LoopInstances.Find(LoopKey)
		: nullptr;
	if (ExistingInstanceId)
	{
		/*UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[AudioSpatialDebug][ReplaceLoop] Type='%s' Context='%s' LoopContext='%s' Emitter='%s' ExistingInstanceId=%d"),
			*Request.EventTag.ToString(),
			*Request.ContextTags.ToString(),
			*LoopContextTag.ToString(),
			*GetNameSafe(Request.EmitterActor),
			*ExistingInstanceId);*/
		if (World->GetNetMode() == NM_Client
			|| Policy.RequestAuthority == EOutlierAudioRequestAuthority::Local)
		{
			StopLoopAudioLocally(*ExistingInstanceId);
		}
		else
		{
			StopLoopAtLocationFromServer(*ExistingInstanceId);
		}
		LoopInstances.Remove(LoopKey);
	}
	else if (bStopsLoop)
	{
		// Stop tags are idempotent. They do not emit an orphaned Off/Fail sound when
		// the matching emitter loop was never started or has already been stopped.
		return true;
	}

	if (Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::StopLoop)
	{
		return true;
	}

	int32 AudioInstanceId = 0;
	if (bLooping)
	{
		AudioInstanceId = NextAudioInstanceId;
		NextAudioInstanceId = NextAudioInstanceId == MAX_int32
			? 1
			: NextAudioInstanceId + 1;
		LoopInstances.Add(LoopKey, AudioInstanceId);
		//UE_LOG(
		//	LogOutlier,
		//	Warning,
		//	TEXT("[AudioSpatialDebug][LoopRegistered] NetMode=%d Type='%s' Context='%s' LoopContext='%s' Emitter='%s' AudioInstanceId=%d LoopCount=%d"),
		//	static_cast<int32>(World->GetNetMode()),
		//	*Request.EventTag.ToString(),
		//	*Request.ContextTags.ToString(),
		//	*LoopContextTag.ToString(),
		//	*GetNameSafe(Request.EmitterActor),
		//	AudioInstanceId,
		//	LoopInstances.Num());
	}

	FOutlierResolvedAudioPlay ResolvedPlay;
	if (!BuildResolvedPlay(
		Request.EventTag,
		*Entry,
		Request,
		Policy.PlaybackMode,
		AudioInstanceId,
		ResolvedPlay))
	{
		if (bLooping)
		{
			LoopInstances.Remove(LoopKey);
		}
		return false;
	}

	/*UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][ResolvedRequest] NetMode=%d Type='%s' Context='%s' LoopContext='%s' Policy=%s Looping=%d AudioInstanceId=%d Emitter='%s' Playback=%s Audience=%s Authority=%s ExplicitLocation=%d RequestLocation=%s ResolvedAtLocation=%d ResolvedLocation=%s Variant=%d LoopRegistered=%d LoopCount=%d"),
		static_cast<int32>(World->GetNetMode()),
		*Request.EventTag.ToString(),
		*Request.ContextTags.ToString(),
		*LoopContextTag.ToString(),
		PlaybackPolicyName(Entry->PlaybackPolicy),
		bLooping ? 1 : 0,
		ResolvedPlay.AudioInstanceId,
		*GetNameSafe(Request.EmitterActor),
		PlaybackModeName(Policy.PlaybackMode),
		AudienceName(Policy.Audience),
		RequestAuthorityName(Policy.RequestAuthority),
		Request.bHasLocation,
		*Request.Location.ToCompactString(),
		ResolvedPlay.bAtLocation,
		*FVector(ResolvedPlay.Location).ToCompactString(),
		ResolvedPlay.VariantIndex,
		bLooping && LoopInstances.Contains(LoopKey) ? 1 : 0,
		LoopInstances.Num());*/

	if (World->GetNetMode() == NM_Standalone)
	{
		const bool bPlayed = PlayResolvedAudioLocally(ResolvedPlay);
		if (!bPlayed && bLooping)
		{
			LoopInstances.Remove(LoopKey);
		}
		return bPlayed;
	}

	const bool bRouted = RouteByAudience(Request, ResolvedPlay, Policy.Audience);
	/*UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[AudioSpatialDebug][RouteResult] Type='%s' Context='%s' Emitter='%s' AudioInstanceId=%d Routed=%d LoopRegistered=%d LoopCount=%d"),
		*Request.EventTag.ToString(),
		*Request.ContextTags.ToString(),
		*GetNameSafe(Request.EmitterActor),
		ResolvedPlay.AudioInstanceId,
		bRouted ? 1 : 0,
		bLooping && LoopInstances.Contains(LoopKey) ? 1 : 0,
		LoopInstances.Num());*/
	if (!bRouted && bLooping)
	{
		LoopInstances.Remove(LoopKey);
	}
	return bRouted;
}

bool UOutlierAudioSubsystem::HandleServerRelevantAtLocationRequest(
	AFirstPersonPlayerController* RequestingController,
	const FOutlierAudioPlayRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(RequestingController))
	{
		return false;
	}

	if (!IsEmitterOwnedByController(Request.EmitterActor, RequestingController))
	{
		/*UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Rejected client world-audio request. Controller='%s' Emitter='%s' Type='%s'."),
			*GetNameSafe(RequestingController),
			*GetNameSafe(Request.EmitterActor),
			*Request.EventTag.ToString());*/
		return false;
	}

	return PlayRelevantAtLocationFromOwningClient(Request);
}

bool UOutlierAudioSubsystem::PlayResolvedAudioLocally(
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	if (bStoppingAllLocalAudio) return false;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return false;
	if (ResolvedPlay.LegacySound)
	{
		// 서버가 태그 후보 부재를 확정한 경우만 호환음을 재생한다. 수신자는 다시 선택하지 않는다.
		if (UVisualEventSubsystem* Visual = World->GetSubsystem<UVisualEventSubsystem>())
		{
			Visual->PlaySoundAtLocation(ResolvedPlay.LegacySound, ResolvedPlay.Location);
			return true;
		}
		return false;
	}
	if (!ResolvedPlay.bAtLocation && ResolvedPlay.MusicContextTag.IsValid()
		&& ResolvedPlay.EventTag == FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM")))
	{
		HandleMusicContext(ResolvedPlay.MusicContextTag);
		return true;
	}

	const FRuntimeCatalogEntry* Entry =
		FindResolvedEntry(ResolvedPlay.EventTag, ResolvedPlay.VariantIndex);
	if (!Entry)
	{
		const FDiscoveredBank* Bank = DiscoveredBanksByType.Find(ResolvedPlay.EventTag);
		if (BankMetadataLoadHandle.IsValid() || (Bank && !Bank->bContentLoaded))
		{
			PendingResolvedPlaysByType.FindOrAdd(ResolvedPlay.EventTag).Emplace(ResolvedPlay, World);
			LoadBankContent(ResolvedPlay.EventTag);
			return true;
		}
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Could not resolve delivered Type '%s' variant %d."),
			*ResolvedPlay.EventTag.ToString(),
			ResolvedPlay.VariantIndex);
		return false;
	}

	FPendingPlay PendingPlay;
	PendingPlay.World = GetWorld();
	PendingPlay.bPlayWhenPaused = !ResolvedPlay.bAtLocation
		&& Entry->RequiredContext.ToString().StartsWith(TEXT("Audio.Context.UI.Menu."));
	PendingPlay.SourceName = Entry->SourceName;
	PendingPlay.bAtLocation = ResolvedPlay.bAtLocation;
	PendingPlay.Location = ResolvedPlay.Location;
	PendingPlay.VolumeMultiplier = Entry->VolumeMultiplier;
	PendingPlay.PitchMultiplier = Entry->PitchMultiplier;
	PendingPlay.StartTime = ResolvedPlay.StartTime;
	PendingPlay.VolumeType = Entry->VolumeType;
	PendingPlay.AudioInstanceId = ResolvedPlay.AudioInstanceId;
	PendingPlay.bLooping = ResolvedPlay.bLooping;

	return QueueOrPlay(*Entry, PendingPlay);
}

bool UOutlierAudioSubsystem::StopLoopAudioLocally(int32 AudioInstanceId, float FadeOutDuration)
{
	if (AudioInstanceId == 0)
	{
		return false;
	}

	RemoveInactiveAudioComponents();
	for (int32 Index = ActiveAudioPlaybacks.Num() - 1; Index >= 0; --Index)
	{
		FActiveAudioPlayback& ActivePlayback = ActiveAudioPlaybacks[Index];
		if ((!ActivePlayback.bLooping && AudioInstanceId >= 0) || ActivePlayback.AudioInstanceId != AudioInstanceId)
		{
			continue;
		}

		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			if (FadeOutDuration > 0.0f)
			{
				// 페이드가 끝나면 엔진이 재생을 끝내고 bAutoDestroy 로 컴포넌트를 정리한다( 풀로 돌려받지 않음 ).
				// 일회성 재생으로 남겨두어 페이드 중에도 옵션 볼륨 / StopAll 의 영향을 받게 한다.
				AudioComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				AudioComponent->bAutoDestroy = true;
				AudioComponent->FadeOut(FadeOutDuration, 0.0f);
				ActivePlayback.bLooping = false;
				ActivePlayback.AudioInstanceId = 0;
				return true;
			}

			AudioComponent->Stop();
			AudioComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			AudioComponent->SetSound(nullptr);
			if (AudioInstanceId < 0) AudioComponent->DestroyComponent();
			else LoopAudioComponentPool.Add(AudioComponent);
		}
		ActiveAudioPlaybacks.RemoveAtSwap(Index);
		return true;
	}

	bool bPendingLoad = false;
	for (const TPair<FSoftObjectPath, TArray<FPendingPlay>>& Pair : PendingPlaysBySound)
	{
		bPendingLoad = Pair.Value.ContainsByPredicate([AudioInstanceId](const FPendingPlay& PendingPlay)
		{
			return PendingPlay.AudioInstanceId == AudioInstanceId;
		});
		if (bPendingLoad)
		{
			break;
		}
	}
	if (bPendingLoad)
	{
		CancelledLoopAudioInstances.Add(AudioInstanceId);
	}
	return false;
}

void UOutlierAudioSubsystem::SetVolumeMultiplier(
	EOutlierAudioVolumeType VolumeType,
	float NewMultiplier)
{
	VolumeMultipliers.FindOrAdd(VolumeType) = FMath::Clamp(NewMultiplier, 0.0f, 1.0f);
	UpdateMusicVolume();
	UpdateZoneMusicVolumes();
	RefreshActiveAudioComponentVolumes(VolumeType);
}

float UOutlierAudioSubsystem::GetVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const
{
	if (const float* VolumeMultiplier = VolumeMultipliers.Find(VolumeType))
	{
		return *VolumeMultiplier;
	}

	return 1.0f;
}

void UOutlierAudioSubsystem::StopAllLocalAudio()
{
	if (bStoppingAllLocalAudio) return;
	// Stop/voice-finished callbacks must not enqueue replacement playback during cleanup.
	TGuardValue<bool> StoppingGuard(bStoppingAllLocalAudio, true);
	bFrontendMusic = false;
	bCombatMusic = false;
	RequestMusic(FGameplayTag());
	ClearZoneMusic();
	ClearLocalEmitterLoops();
	VoiceQueue.Reset();
	FinishActiveVoice(false);

	// Bank discovery/content loading may finish for future UI audio, but must not replay
	// gameplay requests that were waiting for those banks before GameOver.
	PendingPlaysByType.Empty();
	PendingPlaysBySound.Empty();
	PendingResolvedPlaysByType.Empty();
	PendingSurfaceSelectionsBySound.Empty();
	PendingLegacySounds.Empty();
	auto LoadHandlesToCancel = MoveTemp(ActiveLoadHandles);
	ActiveLoadHandles.Empty();
	for (auto& Pair : LoadHandlesToCancel)
	{
		if (Pair.Value.IsValid()) Pair.Value->CancelHandle();
	}

	auto PlaybacksToStop = MoveTemp(ActiveAudioPlaybacks);
	ActiveAudioPlaybacks.Empty();
	for (const FActiveAudioPlayback& ActivePlayback : PlaybacksToStop)
	{
		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->Stop();
			AudioComponent->DestroyComponent();
		}
	}
	for (UAudioComponent* AudioComponent : LoopAudioComponentPool)
	{
		if (IsValid(AudioComponent)) AudioComponent->DestroyComponent();
	}
	LoopAudioComponentPool.Empty();
	LoopInstances.Empty();
	CancelledLoopAudioInstances.Empty();
}

const UOutlierAudioSubsystem::FRuntimeCatalogEntry* UOutlierAudioSubsystem::ResolveOrderedEntry(
	FGameplayTag Type, const TArray<FGameplayTag>& Contexts) const
{
	const TArray<FRuntimeCatalogEntry>* Entries = CatalogEntriesByType.Find(Type);
	if (!Entries) return nullptr;
	// 표면 → 이 이벤트의 Default 순서만 허용한다. 부모 태그/빈 Context는 후보에 끼워 넣지 않는다.
	for (FGameplayTag Context : Contexts)
	{
		if (!Context.IsValid()) continue;
		TArray<const FRuntimeCatalogEntry*> Matches;
		float TotalWeight = 0.0f;
		for (const FRuntimeCatalogEntry& Entry : *Entries)
		{
			if (Entry.RequiredContext == Context && !Entry.Sound.IsNull() && Entry.Weight > 0.0f
				&& !FailedSurfaceSounds.Contains(Entry.Sound.ToSoftObjectPath()))
			{
				Matches.Add(&Entry);
				TotalWeight += Entry.Weight;
			}
		}
		if (Matches.IsEmpty()) continue;
		float Selection = FMath::FRandRange(0.0f, TotalWeight);
		for (const FRuntimeCatalogEntry* Match : Matches)
		{
			Selection -= Match->Weight;
			if (Selection <= 0.0f) return Match;
		}
		return Matches.Last();
	}
	return nullptr;
}

const UOutlierAudioSubsystem::FRuntimeCatalogEntry* UOutlierAudioSubsystem::ResolveBestEntry(
	FGameplayTag TypeTag,
	const FGameplayTagContainer& ContextTags) const
{
	if (!TypeTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Audio] Play request contains an invalid Type tag."));
		return nullptr;
	}

	const TArray<FRuntimeCatalogEntry>* Candidates = CatalogEntriesByType.Find(TypeTag);
	if (!Candidates)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] No catalog rows exist for Type '%s'."),
			*TypeTag.ToString());
		return nullptr;
	}

	int32 BestSpecificity = INDEX_NONE;
	TArray<const FRuntimeCatalogEntry*> BestMatches;

	for (const FRuntimeCatalogEntry& Candidate : *Candidates)
	{
		if (Candidate.RequiredContext.IsValid()
			&& !ContextTags.HasTag(Candidate.RequiredContext))
		{
			continue;
		}

		// Context 가 없는( Invalid ) 후보는 항상 통과하는 기본값 후보 ( Specificity 0 = 최하위 우선순위 ).
		const int32 Specificity = Candidate.RequiredContext.IsValid() ? 1 : 0;
		if (Specificity > BestSpecificity)
		{
			BestSpecificity = Specificity;
			BestMatches.Reset();
			BestMatches.Add(&Candidate);
		}
		else if (Specificity == BestSpecificity)
		{
			BestMatches.Add(&Candidate);
		}
	}

	if (BestMatches.IsEmpty())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Type '%s' has no row matching Context='%s'."),
			*TypeTag.ToString(), *ContextTags.ToString());
		return nullptr;
	}

	float TotalWeight = 0.0f;
	for (const FRuntimeCatalogEntry* Match : BestMatches)
	{
		TotalWeight += Match->Weight;
	}

	float Selection = FMath::FRandRange(0.0f, TotalWeight);
	for (const FRuntimeCatalogEntry* Match : BestMatches)
	{
		Selection -= Match->Weight;
		if (Selection <= 0.0f)
		{
			return Match;
		}
	}

	return BestMatches.Last();
}

const UOutlierAudioSubsystem::FRuntimeCatalogEntry* UOutlierAudioSubsystem::FindResolvedEntry(
	FGameplayTag TypeTag,
	int32 VariantIndex) const
{
	const TArray<FRuntimeCatalogEntry>* Entries = CatalogEntriesByType.Find(TypeTag);
	if (!Entries)
	{
		return nullptr;
	}

	return Entries->FindByPredicate([VariantIndex](const FRuntimeCatalogEntry& Entry)
	{
		return Entry.VariantIndex == VariantIndex;
	});
}

bool UOutlierAudioSubsystem::BuildResolvedPlay(
	FGameplayTag TypeTag,
	const FRuntimeCatalogEntry& Entry,
	const FOutlierAudioPlayRequest& Request,
	EOutlierAudioPlaybackMode PlaybackMode,
	int32 AudioInstanceId,
	FOutlierResolvedAudioPlay& OutResolvedPlay) const
{
	OutResolvedPlay.EventTag = TypeTag;
	OutResolvedPlay.VariantIndex = Entry.VariantIndex;
	OutResolvedPlay.StartTime = FMath::Max(0.0f, Request.StartTime);
	OutResolvedPlay.bLooping = Entry.PlaybackPolicy == EOutlierAudioPlaybackPolicy::Loop;
	OutResolvedPlay.AudioInstanceId = AudioInstanceId;

	if (PlaybackMode == EOutlierAudioPlaybackMode::TwoD)
	{
		OutResolvedPlay.bAtLocation = false;
		return true;
	}

	if (PlaybackMode != EOutlierAudioPlaybackMode::AtLocation)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Type '%s' uses an unsupported native PlaybackMode."),
			*TypeTag.ToString());
		return false;
	}

	OutResolvedPlay.bAtLocation = true;
	if (Request.bHasLocation)
	{
		OutResolvedPlay.Location = Request.Location;
		return true;
	}

	if (IsValid(Request.EmitterActor))
	{
		OutResolvedPlay.Location = Request.EmitterActor->GetActorLocation();
		return true;
	}

	UE_LOG(LogOutlier, Warning,
		TEXT("[Audio] AtLocation Type '%s' requires an explicit Location or an EmitterActor."),
		*TypeTag.ToString());
	return false;
}

bool UOutlierAudioSubsystem::RouteByAudience(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierResolvedAudioPlay& ResolvedPlay,
	EOutlierAudioAudience Audience)
{
	switch (Audience)
	{
	case EOutlierAudioAudience::Local:
		if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
		{
			return PlayResolvedAudioLocally(ResolvedPlay);
		}
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Local Type '%s' was requested on a dedicated server."),
			*ResolvedPlay.EventTag.ToString());
		return false;

	case EOutlierAudioAudience::Owner:
		return RouteOwner(Request, ResolvedPlay);

	case EOutlierAudioAudience::Relevant:
		return RouteRelevant(Request.EmitterActor, ResolvedPlay);

	default:
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Type '%s' uses an unsupported native Audience."),
			*ResolvedPlay.EventTag.ToString());
		return false;
	}
}

bool UOutlierAudioSubsystem::RouteOwner(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AActor* Recipient = IsValid(Request.RecipientActor)
		? Request.RecipientActor.Get()
		: Request.EmitterActor.Get();

	AFirstPersonPlayerController* TargetController =
		ResolveOwningPlayerController(Recipient);
	if (!TargetController)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Owner audience Type '%s' requires RecipientActor or EmitterActor with an owning player controller."),
			*ResolvedPlay.EventTag.ToString());
		return false;
	}

	TargetController->ClientPlayResolvedAudio(ResolvedPlay);
	return true;
}

bool UOutlierAudioSubsystem::RouteRelevant(
	AActor* EmitterActor,
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		return PlayResolvedAudioLocally(ResolvedPlay);
	}

	int32 DeliveryCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(It->Get());
		if (!PlayerController)
		{
			continue;
		}

		if (IsValid(EmitterActor))
		{
			const AActor* ViewTarget = PlayerController->GetViewTarget();
			if (!EmitterActor->IsNetRelevantFor(
				PlayerController,
				ViewTarget,
				PlayerController->GetFocalLocation()))
			{
				continue;
			}
		}

		PlayerController->ClientPlayResolvedAudio(ResolvedPlay);

		const float ApproximateDistance = ResolvedPlay.bAtLocation
			? FVector::Distance(PlayerController->GetFocalLocation(), ResolvedPlay.Location)
			: 0.0f;
		/*UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][RelevantDelivery] Type='%s' Controller='%s' AtLocation=%d Location=%s ApproxDistance=%.1f"),
			*ResolvedPlay.EventTag.ToString(),
			*GetNameSafe(PlayerController),
			ResolvedPlay.bAtLocation,
			*FVector(ResolvedPlay.Location).ToCompactString(),
			ApproximateDistance);*/
		// 0으로 남으면 PlayAudio 가 전달 실패로 보고 루프 등록을 지워, 이후 Stop 과 StopLoopThenOneShot 이 모두 무시된다.
		++DeliveryCount;
	}

	if (DeliveryCount == 0)
	{
		UE_LOG(LogOutlier, Verbose,
			TEXT("[Audio] Relevant Type '%s' had no player recipients."),
			*ResolvedPlay.EventTag.ToString());
	}

	return DeliveryCount > 0;
}

AFirstPersonPlayerController* UOutlierAudioSubsystem::ResolveOwningPlayerController(
	AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}

	if (AFirstPersonPlayerController* PlayerController =
		Cast<AFirstPersonPlayerController>(Actor))
	{
		return PlayerController;
	}

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(Pawn->GetController()))
		{
			return PlayerController;
		}
	}

	if (AFirstPersonPlayerController* InstigatorController =
		Cast<AFirstPersonPlayerController>(Actor->GetInstigatorController()))
	{
		return InstigatorController;
	}

	for (AActor* Owner = Actor->GetOwner(); IsValid(Owner); Owner = Owner->GetOwner())
	{
		if (AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(Owner))
		{
			return PlayerController;
		}

		if (APawn* OwnerPawn = Cast<APawn>(Owner))
		{
			if (AFirstPersonPlayerController* PlayerController =
				Cast<AFirstPersonPlayerController>(OwnerPawn->GetController()))
			{
				return PlayerController;
			}
		}
	}

	return nullptr;
}

AFirstPersonPlayerController* UOutlierAudioSubsystem::ResolveLocalRequestController(
	AActor* EmitterActor) const
{
	AFirstPersonPlayerController* Controller = ResolveOwningPlayerController(EmitterActor);
	return Controller && Controller->IsLocalController() ? Controller : nullptr;
}

bool UOutlierAudioSubsystem::IsEmitterOwnedByController(
	const AActor* EmitterActor,
	const AFirstPersonPlayerController* Controller) const
{
	if (!IsValid(EmitterActor) || !IsValid(Controller))
	{
		return false;
	}

	if (EmitterActor == Controller
		|| EmitterActor == Controller->GetPawn()
		|| EmitterActor->IsOwnedBy(Controller))
	{
		return true;
	}

	return EmitterActor->GetInstigatorController() == Controller;
}

bool UOutlierAudioSubsystem::QueueOrPlay(
	const FRuntimeCatalogEntry& Entry,
	const FPendingPlay& PendingPlay)
{
	if (bStoppingAllLocalAudio) return false;
	if (!PendingPlay.World.IsValid())
	{
		return false;
	}

	if (USoundBase* LoadedSound = Entry.Sound.Get())
	{
		ExecutePlay(LoadedSound, PendingPlay);
		return true;
	}

	const FSoftObjectPath SoundPath = Entry.Sound.ToSoftObjectPath();
	if (!SoundPath.IsValid())
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Variant '%s' contains an invalid Sound asset path."),
			*Entry.SourceName);
		return false;
	}

	PendingPlaysBySound.FindOrAdd(SoundPath).Add(PendingPlay);
	if (ActiveLoadHandles.Contains(SoundPath))
	{
		return true;
	}

	TSharedPtr<FStreamableHandle> LoadHandle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			SoundPath,
			FStreamableDelegate::CreateUObject(
				this,
				&UOutlierAudioSubsystem::HandleSoundLoaded,
				SoundPath));

	if (!LoadHandle.IsValid())
	{
		PendingPlaysBySound.Remove(SoundPath);
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Failed to start async load for '%s'."),
			*SoundPath.ToString());
		return false;
	}

	ActiveLoadHandles.Add(SoundPath, MoveTemp(LoadHandle));
	return true;
}

void UOutlierAudioSubsystem::HandleSoundLoaded(FSoftObjectPath SoundPath)
{
	TSharedPtr<FStreamableHandle> CompletedHandle;
	ActiveLoadHandles.RemoveAndCopyValue(SoundPath, CompletedHandle);

	TArray<FPendingPlay> PendingPlays;
	PendingPlaysBySound.RemoveAndCopyValue(SoundPath, PendingPlays);

	USoundBase* LoadedSound = Cast<USoundBase>(SoundPath.ResolveObject());
	TArray<TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>> SurfaceRequests;
	PendingSurfaceSelectionsBySound.RemoveAndCopyValue(SoundPath, SurfaceRequests);
	if (!LoadedSound && !SurfaceRequests.IsEmpty()) FailedSurfaceSounds.Add(SoundPath);
	for (const auto& Item : SurfaceRequests)
	{
		if (Item.Key.OriginWorld.IsValid() && Item.Key.OriginWorld.Get() == GetWorld()
			&& IsValid(Item.Key.EmitterActor) && !Item.Key.EmitterActor->IsActorBeingDestroyed())
			PlayAudio(Item.Key, Item.Value);
	}
	if (!LoadedSound)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Async load completed without a valid USoundBase for '%s'."),
			*SoundPath.ToString());
		if (PendingPlays.ContainsByPredicate([this](const FPendingPlay& Play)
			{ return Play.VoiceRequestId != 0 && Play.VoiceRequestId == ActiveVoice.Id; }))
		{
			FailActiveVoice();
			StartNextVoice();
		}
		return;
	}

	for (const FPendingPlay& PendingPlay : PendingPlays)
	{
		ExecutePlay(LoadedSound, PendingPlay);
	}
}

void UOutlierAudioSubsystem::ExecutePlay(
	USoundBase* Sound,
	const FPendingPlay& PendingPlay)
{
	UWorld* World = PendingPlay.World.Get();
	if (!World || World != GetWorld() || World->GetNetMode() == NM_DedicatedServer || !Sound)
	{
		return;
	}
	// 비동기 로딩 중 중단된 음성은 에셋이 도착해도 재생하지 않는다.
	if (PendingPlay.VoiceRequestId != 0 && PendingPlay.VoiceRequestId != ActiveVoice.Id)
	{
		return;
	}

	if (CancelledLoopAudioInstances.Remove(PendingPlay.AudioInstanceId) > 0)
	{
		return;
	}

	if (PendingPlay.bAttachToEmitter && !PendingPlay.AttachedEmitter.IsValid()) return;
	FVector ListenerLocation = FVector::ZeroVector;
	bool bHasLocalListener = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController && PlayerController->IsLocalController())
		{
			FRotator ListenerRotation;
			PlayerController->GetPlayerViewPoint(ListenerLocation, ListenerRotation);
			bHasLocalListener = true;
			break;
		}
	}

	const FSoundAttenuationSettings* Attenuation = Sound->GetAttenuationSettingsToApply();
	const float ListenerDistance = PendingPlay.bAtLocation && bHasLocalListener
		? FVector::Distance(ListenerLocation, PendingPlay.Location)
		: -1.0f;

	/*UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][Execute] NetMode=%d Method=%s Sound='%s' Location=%s Listener=%s Distance=%.1f AttenuationAsset='%s' HasSettings=%d VolumeAttenuation=%d Spatialization=%d InnerExtents=%s Falloff=%.1f SoundMaxDistance=%.1f"),
		static_cast<int32>(World->GetNetMode()),
		PendingPlay.bAtLocation ? TEXT("AtLocation") : TEXT("2D"),
		*GetNameSafe(Sound),
		*PendingPlay.Location.ToCompactString(),
		bHasLocalListener ? *ListenerLocation.ToCompactString() : TEXT("None"),
		ListenerDistance,
		*GetNameSafe(Sound->AttenuationSettings),
		Attenuation != nullptr,
		Attenuation ? Attenuation->bAttenuate : false,
		Attenuation ? Attenuation->bSpatialize : false,
		Attenuation ? *Attenuation->AttenuationShapeExtents.ToCompactString() : TEXT("None"),
		Attenuation ? Attenuation->FalloffDistance : 0.0f,
		Sound->GetMaxDistance());*/

	if (PendingPlay.bAtLocation && (!Attenuation || !Attenuation->bAttenuate))
	{
		/*UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][NoDistanceAttenuation] Sound='%s' is using PlaySoundAtLocation, but its Sound asset has no enabled volume attenuation. Distance will not reduce volume."),
			*GetNameSafe(Sound));*/
	}

	if (PendingPlay.bAtLocation)
	{
		UAudioComponent* AudioComponent = nullptr;
		if (PendingPlay.bLooping)
		{
			for (int32 Index = LoopAudioComponentPool.Num() - 1; Index >= 0; --Index)
			{
				UAudioComponent* Candidate = LoopAudioComponentPool[Index];
				if (!IsValid(Candidate))
				{
					LoopAudioComponentPool.RemoveAtSwap(Index);
					continue;
				}

				AudioComponent = Candidate;
				LoopAudioComponentPool.RemoveAtSwap(Index);
				break;
			}

			if (!AudioComponent)
			{
				AudioComponent = UGameplayStatics::SpawnSoundAtLocation(
					World,
					Sound,
					PendingPlay.Location,
					FRotator::ZeroRotator,
					PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType),
					PendingPlay.PitchMultiplier,
					PendingPlay.StartTime,
					Sound->AttenuationSettings,
					nullptr,
					false);
			}
			else
			{
				AudioComponent->SetSound(Sound);
				AudioComponent->SetWorldLocation(PendingPlay.Location);
				AudioComponent->SetPitchMultiplier(PendingPlay.PitchMultiplier);
				AudioComponent->SetVolumeMultiplier(
					PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType));
				AudioComponent->Play(PendingPlay.StartTime);
			}
		}
		else
		{
			AudioComponent = UGameplayStatics::SpawnSoundAtLocation(
				World,
				Sound,
				PendingPlay.Location,
				FRotator::ZeroRotator,
				PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType),
				PendingPlay.PitchMultiplier,
				PendingPlay.StartTime,
				Sound->AttenuationSettings,
				nullptr,
				true);
		}
		if (AudioComponent && PendingPlay.bAttachToEmitter)
		{
			AudioComponent->AttachToComponent(PendingPlay.AttachedEmitter->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		TrackActiveAudioComponent(
			AudioComponent,
			PendingPlay.VolumeMultiplier,
			PendingPlay.VolumeType,
			PendingPlay.AudioInstanceId,
			PendingPlay.bLooping);
		return;
	}

	UAudioComponent* AudioComponent = UGameplayStatics::SpawnSound2D(
		World,
		Sound,
		PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType),
		PendingPlay.PitchMultiplier,
		PendingPlay.StartTime,
		nullptr,
		false,
		!PendingPlay.bLooping);
	TrackActiveAudioComponent(
		AudioComponent,
		PendingPlay.VolumeMultiplier,
		PendingPlay.VolumeType, PendingPlay.AudioInstanceId, PendingPlay.bLooping, PendingPlay.bPlayWhenPaused);
	if (PendingPlay.VoiceRequestId != 0)
	{
		if (!AudioComponent)
		{
			FailActiveVoice();
			StartNextVoice();
			return;
		}
		VoiceAudioComponent = AudioComponent;
		AudioComponent->OnAudioFinishedNative.AddUObject(this, &ThisClass::HandleVoiceAudioFinished);
		OnVoiceStarted.Broadcast(ActiveVoice.Listener.Get(), ActiveVoice.RequestTag);
	}
}

void UOutlierAudioSubsystem::TrackActiveAudioComponent(
	UAudioComponent* AudioComponent,
	float BaseVolumeMultiplier,
	EOutlierAudioVolumeType VolumeType,
	int32 AudioInstanceId,
	bool bLooping,
	bool bPlayWhenPaused)
{
	if (!AudioComponent)
	{
		return;
	}

	RemoveInactiveAudioComponents();

	FActiveAudioPlayback& ActivePlayback = ActiveAudioPlaybacks.AddDefaulted_GetRef();
	ActivePlayback.Component = AudioComponent;
	ActivePlayback.BaseVolumeMultiplier = BaseVolumeMultiplier;
	ActivePlayback.VolumeType = VolumeType;
	ActivePlayback.AudioInstanceId = AudioInstanceId;
	ActivePlayback.bLooping = bLooping;
	ActivePlayback.bPlayWhenPaused = bPlayWhenPaused;
	if (!bPlayWhenPaused) SyncAudioPause(AudioComponent);
}

void UOutlierAudioSubsystem::SyncAudioPause(UAudioComponent* Component)
{
	if (!Component) return;
	const UWorld* World = Component->GetWorld();
	const bool bPaused = World && World->IsPaused();
	if (bPaused && !Component->bIsPaused && Component->IsPlaying())
	{
		WorldPausedAudioComponents.Add(Component);
		Component->SetPaused(true);
	}
	else if (!bPaused && WorldPausedAudioComponents.Remove(Component) > 0)
	{
		Component->SetPaused(false);
	}
}

void UOutlierAudioSubsystem::HandleAudioWorldTick(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	for (auto It = WorldPausedAudioComponents.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent();
	}
	for (const FActiveAudioPlayback& Playback : ActiveAudioPlaybacks)
	{
		if (!Playback.bPlayWhenPaused) SyncAudioPause(Playback.Component.Get());
	}
	SyncAudioPause(MusicComponent);
	for (const FZoneMusicTrack& Track : ZoneMusicTracks) SyncAudioPause(Track.Component.Get());
}

void UOutlierAudioSubsystem::RemoveInactiveAudioComponents()
{
	ActiveAudioPlaybacks.RemoveAll(
		[](const FActiveAudioPlayback& ActivePlayback)
		{
			const UAudioComponent* AudioComponent = ActivePlayback.Component.Get();
			return !AudioComponent || !AudioComponent->IsPlaying();
		});
}

void UOutlierAudioSubsystem::RefreshActiveAudioComponentVolumes(
	EOutlierAudioVolumeType ChangedVolumeType)
{
	RemoveInactiveAudioComponents();

	for (FActiveAudioPlayback& ActivePlayback : ActiveAudioPlaybacks)
	{
		if (ChangedVolumeType != EOutlierAudioVolumeType::Master
			&& ActivePlayback.VolumeType != ChangedVolumeType)
		{
			continue;
		}

		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->SetVolumeMultiplier(
				ActivePlayback.BaseVolumeMultiplier
				* GetCombinedVolumeMultiplier(ActivePlayback.VolumeType));
		}
	}
}

float UOutlierAudioSubsystem::GetCombinedVolumeMultiplier(
	EOutlierAudioVolumeType VolumeType) const
{
	const float MasterMultiplier =
		GetVolumeMultiplier(EOutlierAudioVolumeType::Master);
	if (VolumeType == EOutlierAudioVolumeType::Master)
	{
		return MasterMultiplier;
	}

	return MasterMultiplier * GetVolumeMultiplier(VolumeType);
}

void UOutlierAudioSubsystem::SetLocalEmitterLoop(AActor* Emitter, FGameplayTag Type,
	FGameplayTag Context, bool bEnabled, bool bTwoD, float FadeOutDuration)
{
	if (bStoppingAllLocalAudio) return;
	UWorld* World = Emitter ? Emitter->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Context.IsValid()) return;
	if (LocalLoopWorld.IsValid() && LocalLoopWorld.Get() != World) ClearLocalEmitterLoops();
	const FLoopPlaybackKey Key { Emitter, Context };
	if (!bEnabled)
	{
		FLocalEmitterLoop Removed;
		if (LocalEmitterLoops.RemoveAndCopyValue(Key, Removed))
		{
			StopLoopAudioLocally(Removed.InstanceId, FadeOutDuration);
			for (auto& Pair : PendingPlaysBySound)
				Pair.Value.RemoveAll([&Removed](const FPendingPlay& Play) { return Play.AudioInstanceId == Removed.InstanceId && Removed.InstanceId != 0; });
		}
		return;
	}
	if (!IsValid(Emitter) || !Type.IsValid()) return;
	if (!LocalEmitterLoops.Contains(Key))
	{
		FLocalEmitterLoop& Loop = LocalEmitterLoops.Add(Key);
		Loop.Type = Type;
		Loop.bTwoD = bTwoD;
	}
	LocalLoopWorld = World;
	if (!World->GetTimerManager().IsTimerActive(LocalLoopTimer))
		World->GetTimerManager().SetTimer(LocalLoopTimer, this, &ThisClass::RefreshLocalEmitterLoops, 0.2f, true);
	RefreshLocalEmitterLoops();
}

void UOutlierAudioSubsystem::StopLocalEmitterLoops(AActor* Emitter)
{
	StopAnimationAudioLoops(Emitter);
	for (auto It = LocalEmitterLoops.CreateIterator(); It; ++It)
	{
		if (It.Key().EmitterActor == Emitter)
		{
			StopLoopAudioLocally(It.Value().InstanceId);
			It.RemoveCurrent();
		}
	}
}

void UOutlierAudioSubsystem::ClearLocalEmitterLoops()
{
	AnimationAudioTokens.Empty();
	if (UWorld* World = LocalLoopWorld.Get()) World->GetTimerManager().ClearTimer(LocalLoopTimer);
	for (const auto& Pair : LocalEmitterLoops) StopLoopAudioLocally(Pair.Value.InstanceId);
	LocalEmitterLoops.Empty();
	LocalLoopWorld.Reset();
}

void UOutlierAudioSubsystem::RefreshLocalEmitterLoops()
{
	for (auto It = LocalEmitterLoops.CreateIterator(); It; ++It)
	{
		AActor* Emitter = It.Key().EmitterActor.Get();
		FLocalEmitterLoop& Loop = It.Value();
		if (!IsValid(Emitter) || Emitter->IsActorBeingDestroyed())
		{
			StopLoopAudioLocally(Loop.InstanceId);
			It.RemoveCurrent();
			continue;
		}
		if (Loop.bOneShotSubmitted) continue;
		if (Loop.InstanceId != 0)
		{
			const bool bPlaying = ActiveAudioPlaybacks.ContainsByPredicate([&Loop](const FActiveAudioPlayback& Play)
			{ return Play.AudioInstanceId == Loop.InstanceId && Play.Component.IsValid() && Play.Component->IsPlaying(); });
			bool bPending = false;
			for (const auto& Pair : PendingPlaysBySound)
				bPending |= Pair.Value.ContainsByPredicate([&Loop](const FPendingPlay& Play) { return Play.AudioInstanceId == Loop.InstanceId; });
			if (bPlaying || bPending) continue;
			StopLoopAudioLocally(Loop.InstanceId);
			Loop.InstanceId = 0;
		}
		if (!CatalogEntriesByType.Contains(Loop.Type))
		{
			LoadBankContent(Loop.Type);
			continue;
		}
		FGameplayTagContainer Contexts;
		Contexts.AddTag(It.Key().LoopContextTag);
		const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Loop.Type, Contexts);
		if (!Entry) continue;
		if (Entry->PlaybackPolicy == EOutlierAudioPlaybackPolicy::OneShot)
		{
			// 활성 상태는 유지하되 단발음은 진입당 한 번만 요청한다. 종료 후 재진입하면 다시 허용한다.
			if (Loop.bOneShotSubmitted) continue;
			Loop.bOneShotSubmitted = true;
			Loop.InstanceId = NextLocalLoopId;
			NextLocalLoopId = NextLocalLoopId == MIN_int32 ? -1 : NextLocalLoopId - 1;
			FPendingPlay OneShot;
			OneShot.World = Emitter->GetWorld();
			OneShot.SourceName = Entry->SourceName;
			OneShot.bAtLocation = !Loop.bTwoD;
			OneShot.Location = Emitter->GetActorLocation();
			OneShot.AttachedEmitter = Emitter;
			OneShot.bAttachToEmitter = !Loop.bTwoD;
			OneShot.VolumeMultiplier = Entry->VolumeMultiplier;
			OneShot.PitchMultiplier = Entry->PitchMultiplier;
			OneShot.VolumeType = Entry->VolumeType;
			OneShot.AudioInstanceId = Loop.InstanceId;
			QueueOrPlay(*Entry, OneShot);
			continue;
		}
		if (Entry->PlaybackPolicy != EOutlierAudioPlaybackPolicy::Loop) continue;
		Loop.InstanceId = NextLocalLoopId;
		NextLocalLoopId = NextLocalLoopId == MIN_int32 ? -1 : NextLocalLoopId - 1;
		FPendingPlay Pending;
		Pending.World = Emitter->GetWorld();
		Pending.SourceName = Entry->SourceName;
		Pending.bAtLocation = !Loop.bTwoD;
		Pending.Location = Emitter->GetActorLocation();
		Pending.AttachedEmitter = Emitter;
		Pending.bAttachToEmitter = !Loop.bTwoD;
		Pending.VolumeMultiplier = Entry->VolumeMultiplier;
		Pending.PitchMultiplier = Entry->PitchMultiplier;
		Pending.VolumeType = Entry->VolumeType;
		Pending.AudioInstanceId = Loop.InstanceId;
		Pending.bLooping = true;
		if (!QueueOrPlay(*Entry, Pending)) Loop.InstanceId = 0;
	}
	if (LocalEmitterLoops.IsEmpty())
		if (UWorld* World = LocalLoopWorld.Get()) World->GetTimerManager().ClearTimer(LocalLoopTimer);
}

void UOutlierAudioSubsystem::StopTransientLocalAudio()
{
	VoiceQueue.Reset();
	FinishActiveVoice(false);
	for (int32 Index = ActiveAudioPlaybacks.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveAudioPlaybacks[Index].bLooping) continue;
		if (UAudioComponent* Component = ActiveAudioPlaybacks[Index].Component.Get()) Component->Stop();
		ActiveAudioPlaybacks.RemoveAtSwap(Index);
	}
	for (auto& Pair : PendingPlaysBySound)
		Pair.Value.RemoveAll([](const FPendingPlay& Pending) { return !Pending.bLooping; });
}
