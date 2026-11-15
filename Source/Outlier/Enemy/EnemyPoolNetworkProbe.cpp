#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/VECDrone.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyPoolDefinition.h"
#include "Enemy/EnemyPoolSubsystem.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Camera/CameraActor.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace EnemyPoolNetworkProbe
{
constexpr int32 CaseCount = 6;
constexpr int32 GenerationBase = 7200;
FVector SpawnLocation(int32 Case) { return FVector(2000 + Case * 500, Case < 3 ? 0 : 2000, 1000); }
bool IsRapid(int32 Case) { return Case % 3 == 2; }

struct FProbe
{
	FString Role, Output;
	bool bReadParameters = false, bInitialized = false, bFinished = false, bReturned = false, bMoving = false;
	TWeakObjectPtr<UWorld> ActiveWorld;
	TWeakObjectPtr<AOutlierPostProcessVolume> Volume;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AVECDrone> Current;
	TArray<UClass*> Classes;
	TMap<TWeakObjectPtr<AVECDrone>, int32> LastCases;
	TMap<int32, double> FirstSeen;
	TSet<int32> Screenshots;
	int32 CaseIndex = -1;
	double ReadyAt = -1, CaseStart = 0;
	float LastDelta = 0;
	bool bCheckHackState = false, bHackStateDirtied = false;
	TMap<UClass*, FGameplayTagContainer> InitialHackTags;

	bool Enabled()
	{
		if (!bReadParameters)
		{
			bReadParameters = true;
			bCheckHackState = FParse::Param(FCommandLine::Get(), TEXT("OutlierPoolHackState"));
			FParse::Value(FCommandLine::Get(), TEXT("OutlierPoolNetworkProbe="), Role);
			FParse::Value(FCommandLine::Get(), TEXT("OutlierPoolProbeOutput="), Output);
			if (!Output.IsEmpty()) IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		}
		return !Output.IsEmpty() && (Role == TEXT("Server") || Role == TEXT("Client"));
	}

	void Write(const TSharedRef<FJsonObject>& Row)
	{
		Row->SetStringField(TEXT("role"), Role);
		FString Text;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Row, Writer);
		FFileHelper::SaveStringToFile(Text + LINE_TERMINATOR, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	void Event(const TCHAR* Name, UWorld* World)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), Name);
		Row->SetNumberField(TEXT("case"), CaseIndex);
		Row->SetNumberField(TEXT("time"), World->GetTimeSeconds());
		Write(Row);
		UE_LOG(LogTemp, Display, TEXT("[PoolNetworkProbe] Role=%s Event=%s Case=%d"), *Role, Name, CaseIndex);
	}

	void Initialize(UWorld* World)
	{
		ActiveWorld = World;
		UClass* VolumeClass = LoadClass<AOutlierPostProcessVolume>(nullptr,
			TEXT("/Game/Blueprints/PostProcess/BP_OutlierPostProcessVolume.BP_OutlierPostProcessVolume_C"));
		if (!VolumeClass) { Event(TEXT("VolumeLoadFailed"), World); bFinished = true; return; }
		Volume = World->SpawnActor<AOutlierPostProcessVolume>(VolumeClass);
		if (Role == TEXT("Server"))
		{
			Classes.Add(LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C")));
			Classes.Add(LoadClass<AVECDrone>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C")));
			UEnemyPoolDefinition* Definition = NewObject<UEnemyPoolDefinition>(World);
			for (UClass* Class : Classes)
			{
				if (!Class) { Event(TEXT("ClassLoadFailed"), World); bFinished = true; return; }
				auto& Entry = Definition->Entries.AddDefaulted_GetRef();
				Entry.EnemyClass = Class;
				Entry.PrewarmCount = Entry.MaxCount = 1;
			}
			if (!World->GetSubsystem<UEnemyPoolSubsystem>()->PrewarmPool(Definition))
			{
				Event(TEXT("PrewarmFailed"), World); bFinished = true; return;
			}
			for (TActorIterator<AVECDrone> It(World); It; ++It)
			{
				if (It->IsPoolManaged()) { It->bAlwaysRelevant = true; It->SetReplicateMovement(true); }
			}
		}
		else Camera = World->SpawnActor<ACameraActor>();
		bInitialized = true;
		Event(TEXT("Initialized"), World);
	}

	void StartCase(UWorld* World)
	{
		++CaseIndex;
		if (CaseIndex == CaseCount)
		{
			Event(TEXT("Complete"), World); bFinished = true; FPlatformMisc::RequestExit(false); return;
		}
		// 실제 소환 볼륨의 시간/완료 콜백을 사용한다. 짧은 경우는 중간 상태 복제 생략도 검사한다.
		if (auto* Duration = FindFProperty<FFloatProperty>(AOutlierPostProcessVolume::StaticClass(), TEXT("EnemySpawnPresentationDuration")))
			Duration->SetPropertyValue_InContainer(Volume.Get(), IsRapid(CaseIndex) ? 0.03f : 1.5f);
		FEnemyPoolLeaseContext Context;
		Context.GameplayGeneration = GenerationBase + CaseIndex;
		Current = Cast<AVECDrone>(World->GetSubsystem<UEnemyPoolSubsystem>()->LeaseEnemy(
			Classes[CaseIndex / 3], FTransform(FRotator(0, CaseIndex * 20, 0), SpawnLocation(CaseIndex)), Context));
		if (!Current.IsValid()) { Event(TEXT("LeaseFailed"), World); bFinished = true; return; }
		Current->SetNetUpdateFrequency(CaseIndex % 3 == 1 ? 5 : 30);
		Current->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		if (bCheckHackState)
		{
			const FGameplayTagContainer& Tags = Current->GetHackableComponent()->HackTags;
			if (const FGameplayTagContainer* Initial = InitialHackTags.Find(Current->GetClass()))
			{
				if (Tags != *Initial) { Event(TEXT("HackResetFailed"), World); bFinished = true; return; }
			}
			else InitialHackTags.Add(Current->GetClass(), Tags);
		}
		CaseStart = World->GetTimeSeconds(); bReturned = false; bMoving = false; bHackStateDirtied = false;
		Event(TEXT("Leased"), World);
	}

	bool AcceptWorld(UWorld* World)
	{
		return Enabled() && !bFinished && World && World->IsGameWorld() && World->HasBegunPlay()
			&& (Role == TEXT("Server") ? World->GetNetMode() == NM_DedicatedServer : World->GetNetMode() == NM_Client);
	}

	void BeforeTick(UWorld* World, ELevelTick, float Delta)
	{
		if (!AcceptWorld(World)) return;
		if (!bInitialized) Initialize(World);
		if (bFinished || ActiveWorld.Get() != World) return;
		LastDelta = Delta;
		if (Role == TEXT("Server"))
		{
			if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.IsEmpty()) return;
			if (ReadyAt < 0) ReadyAt = World->GetTimeSeconds();
			if (World->GetTimeSeconds() - ReadyAt < 5) return;
			if (CaseIndex < 0 || World->GetTimeSeconds() - CaseStart > 3.8) StartCase(World);
			if (bFinished || !Current.IsValid()) return;
			const double Age = World->GetTimeSeconds() - CaseStart;
			if (bCheckHackState && !bHackStateDirtied && Age >= 2.0 && !bReturned
				&& Current->GetEnemyPoolState() == EEnemyPoolState::CombatActive)
			{
				// 네트워크 검증 전용 오염이다. 실제 해킹/빙의 성공 경로는 PoolHackState 자동화가 검증한다.
				UHackableComponent* Hackable = Current->GetHackableComponent();
				Hackable->MarkAsHackedOnce();
				Hackable->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
				Hackable->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
				Hackable->HackTags.AddTag(OutlierGameplayTags::State::Locked());
				Current->ForceNetUpdate();
				bHackStateDirtied = true;
				Event(TEXT("HackStateDirtied"), World);
			}
			if (!bReturned && Age >= 2.8)
			{
				Event(TEXT("Returning"), World);
				bReturned = World->GetSubsystem<UEnemyPoolSubsystem>()->ReturnEnemy(Current.Get(),
					Current->GetPoolGameplayGeneration(), Current->GetPoolLeaseSerial());
				if (!bReturned) { Event(TEXT("ReturnFailed"), World); bFinished = true; }
				else if (bCheckHackState && CaseIndex % 3 == 1)
				{
					// 같은 프레임에 재대여하여 클라이언트가 Idle을 건너뛰어도 새 태그로 수렴하는지 확인한다.
					StartCase(World);
				}
			}
			else if (!bReturned && Current->GetEnemyPoolState() == EEnemyPoolState::CombatActive)
			{
				// 이전 비행 기록을 실제 이동과 복제로 만든다. StateTree 전투 판단은 이번 검사에서 제외한다.
				Current->GetStateTreeComponent()->StopLogic(TEXT("Pool network presentation measurement"));
				if (auto* AI = Cast<AEnemyAIController>(Current->GetController())) { AI->StopMovement(); AI->SetEnemyPerceptionEnabled(false); }
				auto* Movement = Current->GetCharacterMovement();
				Movement->SetMovementMode(MOVE_Flying); Movement->MaxFlySpeed = 600;
				Movement->Velocity = FVector(350, 140, 0);
				if (!bMoving) { bMoving = true; Event(TEXT("Moving"), World); }
			}
		}
		else
		{
			for (TActorIterator<AVECDrone> It(World); It; ++It)
			{
				const int32 Case = It->GetPoolGameplayGeneration() - GenerationBase;
				if (Case < 0 || Case >= CaseCount || It->GetEnemyPoolState() == EEnemyPoolState::Idle) continue;
				if (auto* PC = World->GetFirstPlayerController(); PC && Camera.IsValid())
				{
					const FVector Center = It->GetActorLocation();
					const FVector Eye = SpawnLocation(Case) + FVector(-430, -260, 100);
					Camera->SetActorLocationAndRotation(Eye, (Center - Eye).Rotation()); PC->SetViewTarget(Camera.Get());
				}
			}
		}
		Sample(World, TEXT("PreTick"));
	}

	void Sample(UWorld* World, const TCHAR* Phase)
	{
		if (!AcceptWorld(World) || !bInitialized || ActiveWorld.Get() != World) return;
		for (TActorIterator<AVECDrone> It(World); It; ++It)
		{
			AVECDrone* Drone = *It;
			int32 Case = Drone->GetPoolGameplayGeneration() - GenerationBase;
			if (Case >= 0 && Case < CaseCount) LastCases.Add(Drone, Case);
			else if (const int32* Previous = LastCases.Find(Drone)) Case = *Previous;
			else continue;
			auto* Movement = Drone->GetCharacterMovement(); auto* Mesh = Drone->GetMesh();
			const FVector Offset = Mesh->GetRelativeLocation() - Drone->GetBaseTranslationOffset();
			const AVECDrone* Defaults = Drone->GetClass()->GetDefaultObject<AVECDrone>();
			const FRotator Tilt = (Drone->GetThirdPersonTiltRoot()->GetRelativeRotation() - Defaults->GetThirdPersonTiltRoot()->GetRelativeRotation()).GetNormalized();
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("event"), TEXT("Sample")); Row->SetStringField(TEXT("phase"), Phase);
			Row->SetStringField(TEXT("actor"), Drone->GetName()); Row->SetStringField(TEXT("class"), Drone->GetClass()->GetName());
			Row->SetNumberField(TEXT("case"), Case); Row->SetNumberField(TEXT("time"), World->GetTimeSeconds());
			Row->SetNumberField(TEXT("netMode"), int32(World->GetNetMode())); Row->SetBoolField(TEXT("authority"), Drone->HasAuthority());
			Row->SetNumberField(TEXT("generation"), Drone->GetPoolGameplayGeneration()); Row->SetNumberField(TEXT("lease"), Drone->GetPoolLeaseSerial());
			Row->SetNumberField(TEXT("state"), int32(Drone->GetEnemyPoolState())); Row->SetBoolField(TEXT("hidden"), Drone->IsHidden());
			if (bCheckHackState)
			{
				const UHackableComponent* Hackable = Drone->GetHackableComponent();
				Row->SetBoolField(TEXT("hackedOnce"), Hackable->HasHackTag(OutlierGameplayTags::State::HackedOnce()));
				Row->SetBoolField(TEXT("locked"), Hackable->HasHackTag(OutlierGameplayTags::State::Locked()));
				Row->SetBoolField(TEXT("possessable"), Hackable->HasHackTag(HackGameplayTags::Target::Possessable()));
				Row->SetBoolField(TEXT("nonPossessable"), Hackable->HasHackTag(HackGameplayTags::Target::NonPossessable()));
			}
			Row->SetNumberField(TEXT("meshOffset"), Offset.Size()); Row->SetNumberField(TEXT("tiltPitch"), Tilt.Pitch); Row->SetNumberField(TEXT("tiltRoll"), Tilt.Roll);
			Row->SetNumberField(TEXT("capsuleError"), FVector::Distance(Drone->GetActorLocation(), SpawnLocation(Case)));
			Row->SetNumberField(TEXT("speed"), Drone->GetVelocity().Size()); Row->SetNumberField(TEXT("delta"), LastDelta);
			Row->SetBoolField(TEXT("rendered"), Drone->WasRecentlyRendered()); Row->SetNumberField(TEXT("smoothingMode"), int32(Movement->NetworkSmoothingMode));
			Row->SetNumberField(TEXT("smoothingOffset"), Role == TEXT("Client") ? Movement->GetPredictionData_Client_Character()->MeshTranslationOffset.Size() : 0);
			Write(Row);
			if (Role == TEXT("Client") && Drone->GetEnemyPoolState() == EEnemyPoolState::SpawnPresentation)
			{
				if (!FirstSeen.Contains(Case)) FirstSeen.Add(Case, World->GetTimeSeconds());
				if (!Screenshots.Contains(Case) && World->GetTimeSeconds() - FirstSeen[Case] > 0.5)
				{
					Screenshots.Add(Case);
					FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Output) / FString::Printf(TEXT("Spawn_%d.png"), Case), false, false);
				}
			}
		}
	}
	void AfterTick(UWorld* World, ELevelTick, float) { Sample(World, TEXT("PostTick")); }
	void EndFrame() { Sample(ActiveWorld.Get(), TEXT("EndFrame")); }
};
FProbe Probe;
struct FRegister
{
	FRegister()
	{
		FWorldDelegates::OnWorldTickStart.AddRaw(&Probe, &FProbe::BeforeTick);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::AfterTick);
		FCoreDelegates::OnEndFrame.AddRaw(&Probe, &FProbe::EndFrame);
	}
} Register;
}
#endif
