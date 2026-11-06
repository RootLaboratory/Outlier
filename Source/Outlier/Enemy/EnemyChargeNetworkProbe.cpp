#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/SelfDestructDrone.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace DroneNetworkProbe
{
// This harness is dormant unless explicitly requested. It observes genuine world
// ticks and replication; charge is driven directly to isolate network presentation
// from the separately tested StateTree scheduler and pool lifecycle.
struct FCase
{
	const TCHAR* Name;
	float Speed;
	bool bTilt;
	bool bSlowReplication;
	bool bServerCollisionGap;
	bool bClientCollisionGap;
	bool bNoSmoothing;
	bool bClientHitch = false;
};
const FCase Cases[] = {
	{TEXT("Normal"), 750, false, false, false, false, false},
	{TEXT("TiltedParents"), 750, true, false, false, false, false},
	{TEXT("SlowReplicationTilted"), 750, true, true, false, false, false},
	{TEXT("FastTilted"), 3000, true, false, false, false, false},
	{TEXT("ServerCollisionGapControl"), 3000, false, false, true, false, false},
	{TEXT("ClientCollisionGapControl"), 750, false, false, false, true, false},
	{TEXT("TiltedNoSmoothingControl"), 750, true, false, false, false, true},
	{TEXT("NormalNoSmoothingControl"), 750, false, false, false, false, true},
	{TEXT("SlowReplicationFlat"), 750, false, true, false, false, false},
	{TEXT("FastFlat"), 3000, false, false, false, false, false},
	{TEXT("FastFlatNoSmoothingControl"), 3000, false, false, false, false, true},
	{TEXT("NormalWarmRepeat"), 750, false, false, false, false, false},
	{TEXT("NormalClientHitch"), 750, false, false, false, false, false, true},
	{TEXT("NoSmoothingClientHitch"), 750, false, false, false, false, true, true}
};

struct FProbe
{
	FString Role;
	FString Output;
	TWeakObjectPtr<UWorld> ActiveWorld;
	TWeakObjectPtr<ASelfDestructDrone> Drone;
	TWeakObjectPtr<AActor> Target;
	TArray<TWeakObjectPtr<UBoxComponent>> Walls;
	TMap<int32, float> FirstObserved;
	TSet<int32> ClientHitchCases;
	int32 CaseIndex = -1;
	float CaseStart = 0;
	float ConnectedAt = -1;
	bool bCommitted = false;
	bool bFinished = false;
	bool bParametersRead = false;

	bool Enabled()
	{
		if (!bParametersRead)
		{
			bParametersRead = true;
			FParse::Value(FCommandLine::Get(), TEXT("OutlierDroneNetworkProbe="), Role);
			FParse::Value(FCommandLine::Get(), TEXT("OutlierDroneProbeOutput="), Output);
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
		Text += LINE_TERMINATOR;
		FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	void Event(const TCHAR* Name, UWorld* World)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), Name);
		Row->SetNumberField(TEXT("worldTime"), World->GetTimeSeconds());
		Row->SetNumberField(TEXT("netMode"), int32(World->GetNetMode()));
		Row->SetNumberField(TEXT("case"), CaseIndex);
		Write(Row);
		UE_LOG(LogTemp, Display, TEXT("[DroneNetworkProbe] Role=%s Event=%s Case=%d NetMode=%d"),
			*Role, Name, CaseIndex, int32(World->GetNetMode()));
	}

	void Initialize(UWorld* World)
	{
		ActiveWorld = World;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
		{
			AActor* Wall = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
			Wall->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1.5f, 500, 500));
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(300, Index * 2000, 1000));
			Walls.Add(Box);
		}
		Event(TEXT("Initialized"), World);
	}

	void StartCase(UWorld* World)
	{
		if (Drone.IsValid()) { Drone->CancelCommittedSelfDestruct(); Drone->Destroy(); }
		if (Target.IsValid()) Target->Destroy();
		++CaseIndex;
		if (CaseIndex >= UE_ARRAY_COUNT(Cases))
		{
			bFinished = true;
			Event(TEXT("Complete"), World);
			FPlatformMisc::RequestExit(false);
			return;
		}
		CaseStart = World->GetTimeSeconds();
		bCommitted = false;
		UClass* Class = LoadClass<ASelfDestructDrone>(nullptr,
			TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Explosion.BP_VECDrone_Explosion_C"));
		if (!Class) { Event(TEXT("ClassLoadFailed"), World); bFinished = true; return; }
		FActorSpawnParameters Spawn;
		Spawn.Name = FName(*FString::Printf(TEXT("NetProbe_Drone_%d"), CaseIndex));
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.bDeferConstruction = true;
		const FTransform Transform(FVector(0, CaseIndex * 2000, 1000));
		ASelfDestructDrone* NewDrone = World->SpawnActor<ASelfDestructDrone>(Class, Transform, Spawn);
		if (!NewDrone) { Event(TEXT("SpawnFailed"), World); bFinished = true; return; }
		NewDrone->AutoPossessAI = EAutoPossessAI::Disabled;
		NewDrone->bAlwaysRelevant = true;
		NewDrone->SetReplicateMovement(true);
		NewDrone->FinishSpawning(Transform);
		if (UStateTreeComponent* Tree = NewDrone->FindComponentByClass<UStateTreeComponent>()) Tree->StopLogic(TEXT("Network probe controlled charge"));
		NewDrone->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		if (Cases[CaseIndex].bSlowReplication) NewDrone->SetNetUpdateFrequency(5);
		Drone = NewDrone;
		AActor* NewTarget = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(NewTarget);
		NewTarget->SetRootComponent(Root);
		Root->RegisterComponent();
		NewTarget->SetActorLocation(FVector(1000, CaseIndex * 2000, 1000));
		Target = NewTarget;
		Event(TEXT("CaseStarted"), World);
	}

	void BeforeTick(UWorld* World, ELevelTick TickType, float DeltaTime)
	{
		if (!Enabled() || bFinished || !World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
		const bool bServer = Role == TEXT("Server");
		if (bServer ? World->GetNetMode() != NM_DedicatedServer : World->GetNetMode() != NM_Client) return;
		if (!ActiveWorld.IsValid()) Initialize(World);
		if (ActiveWorld.Get() != World) return;
		if (bServer)
		{
			if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.IsEmpty()) return;
			if (ConnectedAt < 0) { ConnectedAt = World->GetTimeSeconds(); Event(TEXT("ClientConnectionPresent"), World); }
			if (World->GetTimeSeconds() - ConnectedAt < 3) return;
			if (CaseIndex < 0 || World->GetTimeSeconds() - CaseStart >= 4.0f) StartCase(World);
			if (bFinished || !Drone.IsValid()) return;
			const float Age = World->GetTimeSeconds() - CaseStart;
			Walls[CaseIndex]->SetCollisionEnabled(Cases[CaseIndex].bServerCollisionGap && Age < 2.0f
				? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
			if (!bCommitted && Age >= 0.5f)
			{
				bCommitted = Drone->BeginCommittedSelfDestruct(Target.Get(), 10, Cases[CaseIndex].Speed);
				Event(bCommitted ? TEXT("Committed") : TEXT("CommitFailed"), World);
			}
			if (bCommitted) Drone->UpdateCommittedSelfDestructMovement(DeltaTime, Cases[CaseIndex].Speed);
		}
		else
		{
			for (TActorIterator<ASelfDestructDrone> It(World); It; ++It)
			{
				ASelfDestructDrone* Observed = *It;
				// Dynamic replicated actors may receive different client object names.
				// Cases occupy disjoint Y lanes, which travel in replicated movement.
				const int32 Index = FMath::RoundToInt(Observed->GetActorLocation().Y / 2000.0f);
				if (Index < 0 || Index >= UE_ARRAY_COUNT(Cases)) continue;
				if (!FirstObserved.Contains(Index)) FirstObserved.Add(Index, World->GetTimeSeconds());
				const bool bGap = Cases[Index].bClientCollisionGap && World->GetTimeSeconds() - FirstObserved[Index] < 2;
				Walls[Index]->SetCollisionEnabled(bGap ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
				Observed->GetAIFacingPitchRoot()->SetRelativeRotation(Cases[Index].bTilt ? FRotator(55, 0, 0) : FRotator::ZeroRotator);
				Observed->GetThirdPersonTiltRoot()->SetRelativeRotation(Cases[Index].bTilt ? FRotator(25, 0, 25) : FRotator::ZeroRotator);
				if (Cases[Index].bNoSmoothing) Observed->GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
				// A single client-only frame stall reproduces the measured first-spawn
				// hitch after assets are warm. The dedicated server keeps ticking.
				if (Cases[Index].bClientHitch && !ClientHitchCases.Contains(Index)
					&& Observed->IsSelfDestructCommitted() && Observed->GetActorLocation().X >= 40)
				{
					ClientHitchCases.Add(Index);
					const double Start = FPlatformTime::Seconds();
					FPlatformProcess::Sleep(0.4f);
					auto Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("event"), TEXT("ClientHitch"));
					Row->SetNumberField(TEXT("case"), Index);
					Row->SetNumberField(TEXT("worldTime"), World->GetTimeSeconds());
					Row->SetNumberField(TEXT("platformTime"), Start);
					Row->SetNumberField(TEXT("elapsedSeconds"), FPlatformTime::Seconds() - Start);
					Row->SetNumberField(TEXT("capsuleX"), Observed->GetActorLocation().X);
					Row->SetBoolField(TEXT("committed"), Observed->IsSelfDestructCommitted());
					Row->SetNumberField(TEXT("smoothingMode"), int32(Observed->GetCharacterMovement()->NetworkSmoothingMode));
					Write(Row);
				}
			}
		}
	}

	void AfterTick(UWorld* World, ELevelTick TickType, float DeltaTime)
	{
		if (!Enabled() || ActiveWorld.Get() != World || bFinished) return;
		for (TActorIterator<ASelfDestructDrone> It(World); It; ++It)
		{
			ASelfDestructDrone* Observed = *It;
			const int32 Index = FMath::RoundToInt(Observed->GetActorLocation().Y / 2000.0f);
			if (Index < 0 || Index >= UE_ARRAY_COUNT(Cases)) continue;
			UCapsuleComponent* Capsule = Observed->GetCapsuleComponent();
			USkeletalMeshComponent* Mesh = Observed->GetMesh();
			UCharacterMovementComponent* Movement = Observed->GetCharacterMovement();
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("event"), TEXT("Sample"));
			Row->SetNumberField(TEXT("case"), Index);
			Row->SetStringField(TEXT("caseName"), Cases[Index].Name);
			Row->SetStringField(TEXT("actor"), Observed->GetName());
			Row->SetNumberField(TEXT("worldTime"), World->GetTimeSeconds());
			Row->SetNumberField(TEXT("platformTime"), FPlatformTime::Seconds());
			Row->SetNumberField(TEXT("deltaTime"), DeltaTime);
			Row->SetNumberField(TEXT("netMode"), int32(World->GetNetMode()));
			Row->SetBoolField(TEXT("authority"), Observed->HasAuthority());
			Row->SetNumberField(TEXT("actorX"), Observed->GetActorLocation().X);
			Row->SetNumberField(TEXT("actorY"), Observed->GetActorLocation().Y);
			Row->SetNumberField(TEXT("actorZ"), Observed->GetActorLocation().Z);
			Row->SetNumberField(TEXT("capsuleX"), Capsule->GetComponentLocation().X);
			Row->SetNumberField(TEXT("capsuleY"), Capsule->GetComponentLocation().Y);
			Row->SetNumberField(TEXT("capsuleZ"), Capsule->GetComponentLocation().Z);
			Row->SetNumberField(TEXT("capsuleRadius"), Capsule->GetScaledCapsuleRadius());
			Row->SetNumberField(TEXT("capsuleCollision"), int32(Capsule->GetCollisionEnabled()));
			Row->SetBoolField(TEXT("actorCollision"), Observed->GetActorEnableCollision());
			Row->SetNumberField(TEXT("wallCollision"), int32(Walls[Index]->GetCollisionEnabled()));
			Row->SetNumberField(TEXT("poolState"), int32(Observed->GetEnemyPoolState()));
			Row->SetNumberField(TEXT("poolLease"), Observed->GetPoolLeaseSerial());
			Row->SetBoolField(TEXT("committed"), Observed->IsSelfDestructCommitted());
			Row->SetNumberField(TEXT("movementMode"), int32(Movement->MovementMode));
			Row->SetNumberField(TEXT("velocityX"), Movement->Velocity.X);
			Row->SetNumberField(TEXT("velocityY"), Movement->Velocity.Y);
			Row->SetNumberField(TEXT("velocityZ"), Movement->Velocity.Z);
			Row->SetNumberField(TEXT("meshX"), Mesh->GetComponentLocation().X);
			Row->SetNumberField(TEXT("meshY"), Mesh->GetComponentLocation().Y);
			Row->SetNumberField(TEXT("meshZ"), Mesh->GetComponentLocation().Z);
			Row->SetNumberField(TEXT("meshBoundsMaxX"), Mesh->Bounds.Origin.X + Mesh->Bounds.BoxExtent.X);
			Row->SetNumberField(TEXT("meshRelativeX"), Mesh->GetRelativeLocation().X);
			Row->SetNumberField(TEXT("pitchRootPitch"), Observed->GetAIFacingPitchRoot()->GetRelativeRotation().Pitch);
			Row->SetNumberField(TEXT("tiltRootPitch"), Observed->GetThirdPersonTiltRoot()->GetRelativeRotation().Pitch);
			Row->SetNumberField(TEXT("smoothingMode"), int32(Movement->NetworkSmoothingMode));
			Row->SetNumberField(TEXT("netUpdateFrequency"), Observed->GetNetUpdateFrequency());
			if (Role == TEXT("Client"))
			{
				const FNetworkPredictionData_Client_Character* Prediction = Movement->GetPredictionData_Client_Character();
				Row->SetNumberField(TEXT("smoothingOffsetX"), Prediction ? Prediction->MeshTranslationOffset.X : 0);
			}
			Write(Row);
		}
	}
};

FProbe Probe;
struct FRegister
{
	FRegister()
	{
		FWorldDelegates::OnWorldTickStart.AddRaw(&Probe, &FProbe::BeforeTick);
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::AfterTick);
	}
} Register;
}

#endif
