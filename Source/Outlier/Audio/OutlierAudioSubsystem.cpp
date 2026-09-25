#include "Audio/OutlierAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Outlier.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

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
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Master, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::BGM, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::SFX, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Voice, 1.0f);
	ReloadCatalog();
}

void UOutlierAudioSubsystem::Deinitialize()
{
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
	CatalogEntriesByType.Empty();
	DiscoveredBanksByType.Empty();
	LoadedBanks.Empty();

	Super::Deinitialize();
}

bool UOutlierAudioSubsystem::ReloadCatalog()
{
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

	DiscoverAudioBanks();

	// 발견/로드는 비동기로 진행된다. 완료 여부는 IsCatalogReady() 로 확인한다.
	return true;
}

void UOutlierAudioSubsystem::DiscoverAudioBanks()
{
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
		return;
	}

	IngestBank(Bank);
	FlushPendingPlaysForType(TypeTag);
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
				TEXT("[Audio] Bank '%s' has a Definition entry that failed to resolve."),
				*GetNameSafe(Bank));
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
	TArray<TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>> Pending;
	PendingPlaysByType.RemoveAndCopyValue(TypeTag, Pending);

	for (const TPair<FOutlierAudioPlayRequest, FOutlierAudioExecutionPolicy>& Item : Pending)
	{
		PlayAudio(Item.Key, Item.Value);
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
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

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

	// Type 콘텐츠가 아직 로드되지 않은 Deferred Bank( BGM/Voice 등 )라면, 재생 요청 자체가
	// 로드 트리거가 된다. 큐잉해두고 로드가 끝나면 FlushPendingPlaysForType 이 재시도한다.
	if (!CatalogEntriesByType.Contains(Request.EventTag))
	{
		const FDiscoveredBank* Discovered = DiscoveredBanksByType.Find(Request.EventTag);
		if (!Discovered)
		{
			UE_LOG(LogOutlier, Warning,
				TEXT("[Audio] Unknown audio Type '%s'."),
				*Request.EventTag.ToString());
			return false;
		}

		if (!Discovered->bContentLoaded)
		{
		/*	PendingPlaysByType.FindOrAdd(Request.EventTag).Emplace(Request, Policy);
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[AudioSpatialDebug][QueuedRequest] NetMode=%d Type='%s' Context='%s' Emitter='%s' PendingCount=%d Reason=BankContentLoading"),
				static_cast<int32>(World->GetNetMode()),
				*Request.EventTag.ToString(),
				*Request.ContextTags.ToString(),
				*GetNameSafe(Request.EmitterActor),
				PendingPlaysByType.Find(Request.EventTag)->Num());
			LoadBankContent(Request.EventTag);*/
			return true;
		}
		// bContentLoaded 인데 카탈로그에 없다 = 그 Bank 에 유효한 Variant 가 하나도 없었던 경우.
		// ResolveBestEntry 에서 통상적인 실패 로그를 남기도록 그대로 진행한다.
	}

	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Request.EventTag, Request.ContextTags);
	if (!Entry)
	{
		return false;
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
	const FRuntimeCatalogEntry* Entry =
		FindResolvedEntry(ResolvedPlay.EventTag, ResolvedPlay.VariantIndex);
	if (!Entry)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Could not resolve delivered Type '%s' variant %d."),
			*ResolvedPlay.EventTag.ToString(),
			ResolvedPlay.VariantIndex);
		return false;
	}

	FPendingPlay PendingPlay;
	PendingPlay.World = GetWorld();
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

bool UOutlierAudioSubsystem::StopLoopAudioLocally(int32 AudioInstanceId)
{
	if (AudioInstanceId == 0)
	{
		return false;
	}

	RemoveInactiveAudioComponents();
	for (int32 Index = ActiveAudioPlaybacks.Num() - 1; Index >= 0; --Index)
	{
		FActiveAudioPlayback& ActivePlayback = ActiveAudioPlaybacks[Index];
		if (!ActivePlayback.bLooping || ActivePlayback.AudioInstanceId != AudioInstanceId)
		{
			continue;
		}

		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->Stop();
			AudioComponent->SetSound(nullptr);
			LoopAudioComponentPool.Add(AudioComponent);
		}
		ActiveAudioPlaybacks.RemoveAtSwap(Index);
		return true;
	}

	bool bPendingLoad = false;
	for (const TPair<FSoftObjectPath, TArray<FPendingPlay>>& Pair : PendingPlaysBySound)
	{
		bPendingLoad = Pair.Value.ContainsByPredicate([AudioInstanceId](const FPendingPlay& PendingPlay)
		{
			return PendingPlay.bLooping && PendingPlay.AudioInstanceId == AudioInstanceId;
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
	for (FActiveAudioPlayback& ActivePlayback : ActiveAudioPlaybacks)
	{
		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->Stop();
		}
	}

	ActiveAudioPlaybacks.Empty();
	PendingPlaysBySound.Empty();
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
			TEXT("[Audio] Type '%s' has no row matching the supplied context tags."),
			*TypeTag.ToString());
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
			ApproximateDistance);
		++DeliveryCount;*/
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
	if (!LoadedSound)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Async load completed without a valid USoundBase for '%s'."),
			*SoundPath.ToString());
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
	if (!World || !Sound)
	{
		return;
	}

	if (PendingPlay.bLooping
		&& CancelledLoopAudioInstances.Remove(PendingPlay.AudioInstanceId) > 0)
	{
		return;
	}

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
		true);
	TrackActiveAudioComponent(
		AudioComponent,
		PendingPlay.VolumeMultiplier,
		PendingPlay.VolumeType);
}

void UOutlierAudioSubsystem::TrackActiveAudioComponent(
	UAudioComponent* AudioComponent,
	float BaseVolumeMultiplier,
	EOutlierAudioVolumeType VolumeType,
	int32 AudioInstanceId,
	bool bLooping)
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
