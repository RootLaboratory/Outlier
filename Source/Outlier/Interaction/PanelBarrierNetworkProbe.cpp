#include "Interaction/PanelBarrierNetworkProbe.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/EnemyBase.h"
#include "Engine/NetDriver.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interaction/PanelMover.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Room/RoomCombatBarrier.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Room/RoomVolume.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Shooter/ShooterCharacter.h"
#include "UObject/UnrealType.h"

namespace PanelBarrierProbe
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(RoomTag, "Room.PanelBarrierProbe.1.1");
	constexpr int32 CaseCount = 3;
	const TCHAR* Names[] = {TEXT("Panel01BothRoles"), TEXT("Panel02BothRoles"), TEXT("BarrierBothRoles")};
	bool Enabled()
	{
#if WITH_DEV_AUTOMATION_TESTS
		FString Role;
		return FParse::Value(FCommandLine::Get(), TEXT("OutlierPanelBarrierProbe="), Role)
			&& (Role == TEXT("Server") || Role == TEXT("Shooter") || Role == TEXT("Partner"));
#else
		return false;
#endif
	}
	APanelBarrierNetworkProbe* Find(UWorld* World)
	{
		for (TActorIterator<APanelBarrierNetworkProbe> It(World); It; ++It) return *It;
		return nullptr;
	}
	template<class T> void SetTag(T* Actor)
	{
		*FindFProperty<FStructProperty>(T::StaticClass(), TEXT("RoomTag"))->template ContainerPtrToValuePtr<FGameplayTag>(Actor) = RoomTag;
	}
}

void APanelBarrierProbeController::ServerPanelBarrierReady_Implementation()
{
	if (PanelBarrierProbe::Enabled()) bProbeReady = true;
}

void APanelBarrierProbeController::ServerPanelBarrierObserved_Implementation(int32 Index, bool bPassed)
{
	if (PanelBarrierProbe::Enabled())
		if (auto* Probe = PanelBarrierProbe::Find(GetWorld())) Probe->ReceiveObservation(this, Index, bPassed);
}

APanelBarrierProbeGameMode::APanelBarrierProbeGameMode()
{
	if (!PanelBarrierProbe::Enabled()) return;
	PlayerControllerClass = APanelBarrierProbeController::StaticClass();
	PlayerStateClass = AOutlierPlayerState::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}

void APanelBarrierProbeGameMode::PostLogin(APlayerController* Player)
{
	Super::PostLogin(Player);
	if (!PanelBarrierProbe::Enabled()) return;
	auto* Probe = PanelBarrierProbe::Find(GetWorld());
	if (!Probe) Probe = GetWorld()->SpawnActor<APanelBarrierNetworkProbe>();
	const bool bShooter = !Probe->Shooter;
	if (!bShooter && Probe->Partner) return;
	UClass* Class = LoadClass<ACharacter>(nullptr, bShooter
		? TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C")
		: TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Pawn = Class ? GetWorld()->SpawnActor<ACharacter>(Class, FVector(-3000, bShooter ? 0 : 500, 300), FRotator::ZeroRotator, Params) : nullptr;
	if (!Pawn) return;
	if (bShooter) Probe->Shooter = CastChecked<AShooterCharacter>(Pawn);
	else Probe->Partner = CastChecked<APartnerCharacter>(Pawn);
	auto* State = Player->GetPlayerState<AOutlierPlayerState>();
	State->SetPairId(0);
	State->SetPlayerRole(bShooter ? EOutlierPlayerRole::Shooter : EOutlierPlayerRole::Partner);
	Player->Possess(Pawn);
	if (Probe->Shooter && Probe->Partner)
	{
		Probe->Shooter->SetPartnerCharacter(Probe->Partner);
		Probe->Partner->SetShooterCharacter(Probe->Shooter);
		for (AFirstPersonCharacter* Character : {static_cast<AFirstPersonCharacter*>(Probe->Shooter), static_cast<AFirstPersonCharacter*>(Probe->Partner)})
		{
			auto* PS = Character->GetPlayerState<AOutlierPlayerState>();
			PS->SetShooterCharacter(Probe->Shooter);
			PS->SetPartnerCharacter(Probe->Partner);
			PS->SetAcquiredSuit(true);
		}
	}
	Probe->ForceNetUpdate();
}

APanelBarrierNetworkProbe::APanelBarrierNetworkProbe()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30);
	PrimaryActorTick.bCanEverTick = PanelBarrierProbe::Enabled();
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Floor = CreateDefaultSubobject<UBoxComponent>(TEXT("Floor"));
	Floor->SetupAttachment(RootComponent);
	Floor->SetBoxExtent(FVector(5000, 5000, 20));
	Floor->SetRelativeLocation(FVector(0, 0, -20));
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
}

void APanelBarrierNetworkProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APanelBarrierNetworkProbe, Shooter);
	DOREPLIFETIME(APanelBarrierNetworkProbe, Partner);
	DOREPLIFETIME(APanelBarrierNetworkProbe, Panel);
	DOREPLIFETIME(APanelBarrierNetworkProbe, Barrier);
	DOREPLIFETIME(APanelBarrierNetworkProbe, CaseIndex);
	DOREPLIFETIME(APanelBarrierNetworkProbe, Phase);
	DOREPLIFETIME(APanelBarrierNetworkProbe, ExpectedShooter);
	DOREPLIFETIME(APanelBarrierNetworkProbe, ExpectedPartner);
	DOREPLIFETIME(APanelBarrierNetworkProbe, ExpectedPanel);
	DOREPLIFETIME(APanelBarrierNetworkProbe, bExpectedBarrier);
	DOREPLIFETIME(APanelBarrierNetworkProbe, bAllPassed);
}

void APanelBarrierNetworkProbe::Write(const TSharedRef<FJsonObject>& Row) const
{
	FString Output, ProbeRole;
	FParse::Value(FCommandLine::Get(), TEXT("OutlierPanelBarrierOutput="), Output);
	FParse::Value(FCommandLine::Get(), TEXT("OutlierPanelBarrierProbe="), ProbeRole);
	const FString Absolute = FPaths::ConvertRelativePathToFull(Output);
	if (Output.IsEmpty() || !FPaths::IsUnderDirectory(Absolute, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()))) return;
	Row->SetStringField(TEXT("role"), ProbeRole);
	Row->SetNumberField(TEXT("netMode"), GetNetMode());
	Row->SetBoolField(TEXT("authority"), HasAuthority());
	Row->SetNumberField(TEXT("case"), CaseIndex);
	if (CaseIndex >= 0 && CaseIndex < PanelBarrierProbe::CaseCount) Row->SetStringField(TEXT("name"), PanelBarrierProbe::Names[CaseIndex]);
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = GetWorld()->GetNetDriver())
	{
		Row->SetNumberField(TEXT("lag"), Driver->PacketSimulationSettings.PktLag);
		Row->SetNumberField(TEXT("loss"), Driver->PacketSimulationSettings.PktLoss);
	}
#endif
	FString Json;
	FJsonSerializer::Serialize(Row, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Absolute), true);
	if (!FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *Absolute,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead))
	{
		UE_LOG(LogTemp, Error, TEXT("[PanelBarrierProbe] Failed to append result: %s"), *Absolute);
	}
}

TSharedRef<FJsonObject> APanelBarrierNetworkProbe::Observe() const
{
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("Observation"));
	for (const auto& Item : {TPair<FString, ACharacter*>(TEXT("shooter"), Shooter), TPair<FString, ACharacter*>(TEXT("partner"), Partner)})
	{
		const FVector Location = Item.Value->GetActorLocation();
		Row->SetNumberField(Item.Key + TEXT("X"), Location.X);
		Row->SetNumberField(Item.Key + TEXT("Y"), Location.Y);
		Row->SetNumberField(Item.Key + TEXT("Z"), Location.Z);
		Row->SetNumberField(Item.Key + TEXT("Mode"), Item.Value->GetCharacterMovement()->MovementMode);
		Row->SetBoolField(Item.Key + TEXT("Owner"), Item.Value->IsLocallyControlled());
	}
	Row->SetBoolField(TEXT("blocked"), Barrier && Barrier->IsBlocked());
	Row->SetNumberField(TEXT("motionSamples"), MotionSamples);
	Row->SetNumberField(TEXT("maxPenetrationSeconds"), MaxPenetrationSeconds);
	if (Panel) Row->SetStringField(TEXT("panelClass"), Panel->GetClass()->GetPathName());
	return Row;
}

void APanelBarrierNetworkProbe::SetupCase()
{
	Observers.Reset();
	bActionAccepted = false;
	Phase = 0;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ActionWaitSeconds = 4;
	if (Panel) Panel->Destroy();
	if (Target) Target->Destroy();
	Panel = nullptr;
	Target = nullptr;
	Shooter->GetCharacterMovement()->StopMovementImmediately();
	Partner->GetCharacterMovement()->StopMovementImmediately();
	Shooter->SetBase(nullptr);
	if (CaseIndex < 2)
	{
		const TCHAR* Path = CaseIndex == 0
			? TEXT("/Game/Blueprints/Interaction/BP_MovingPanel01.BP_MovingPanel01_C")
			: TEXT("/Game/Blueprints/Interaction/BP_MovingPanel02.BP_MovingPanel02_C");
		UClass* Class = LoadClass<APanelMover>(nullptr, Path);
		Panel = Class ? GetWorld()->SpawnActor<APanelMover>(Class, FVector(0, 0, 500), FRotator::ZeroRotator) : nullptr;
		if (Panel)
		{
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Panel);
			for (const UStaticMeshComponent* Mesh : Meshes)
			{
				auto Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("event"), TEXT("AssetCollision"));
				Row->SetStringField(TEXT("panelClass"), Class->GetPathName());
				Row->SetStringField(TEXT("component"), Mesh->GetName());
				Row->SetStringField(TEXT("profile"), Mesh->GetCollisionProfileName().ToString());
				Row->SetNumberField(TEXT("enabled"), Mesh->GetCollisionEnabled());
				Row->SetNumberField(TEXT("objectType"), Mesh->GetCollisionObjectType());
				Row->SetNumberField(TEXT("pawnResponse"), Mesh->GetCollisionResponseToChannel(ECC_Pawn));
				Row->SetNumberField(TEXT("partnerObjectType"), Partner->GetCapsuleComponent()->GetCollisionObjectType());
				Row->SetNumberField(TEXT("partnerResponse"), Partner->GetCapsuleComponent()->GetCollisionResponseToChannel(Mesh->GetCollisionObjectType()));
				Row->SetStringField(TEXT("bounds"), Mesh->Bounds.GetBox().ToString());
				Write(Row);
			}
			Target = GetWorld()->SpawnActor<AActor>();
			// 목표 Actor는 위치가 필요하므로 검증 전용 SceneRoot를 등록한다.
			auto* Root = NewObject<USceneComponent>(Target);
			Target->SetRootComponent(Root);
			Root->RegisterComponent();
			Target->SetActorLocation(Panel->GetActorLocation() + FVector(0, 0, 300));
			*FindFProperty<FObjectProperty>(APanelMover::StaticClass(), TEXT("TargetLocationActor"))->ContainerPtrToValuePtr<TObjectPtr<AActor>>(Panel) = Target;
			// 에셋의 속도·커브는 서버와 클라이언트가 함께 사용한다. 검증 편의로 서버 값만 바꾸지 않는다.
			ActionWaitSeconds = FMath::Max(4.0f,
				*FindFProperty<FFloatProperty>(APanelMover::StaticClass(), TEXT("MoveDuration"))->ContainerPtrToValuePtr<float>(Panel) + 2);

		}
	}
	else
	{
		auto* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
#if WITH_DEV_AUTOMATION_TESTS
		auto* Definition = NewObject<URoomCombatDefinition>(Combat);
		auto& RoomDefinition = Definition->RoomDefinitions.AddDefaulted_GetRef();
		RoomDefinition.RoomTag = PanelBarrierProbe::RoomTag;
		RoomDefinition.CombatPhases.AddDefaulted_GetRef().Waves.AddDefaulted();
		Combat->SetCombatDefinitionForTesting(Definition);
#endif
		Room = GetWorld()->SpawnActorDeferred<ARoomVolume>(ARoomVolume::StaticClass(), FTransform::Identity);
		PanelBarrierProbe::SetTag(Room.Get());
		CastChecked<UBoxComponent>(Room->GetRootComponent())->SetBoxExtent(FVector(1000, 1000, 1000));
		Room->FinishSpawning(FTransform::Identity);
		UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Default.BP_VECDrone_Default_C"));
		if (EnemyClass)
		{
			auto* Enemy = GetWorld()->SpawnActorDeferred<AEnemyBase>(EnemyClass, FTransform(FVector(-20000, 0, 300)));
			Enemy->GetRoomTagComp()->AssignDefaultRoomTag(PanelBarrierProbe::RoomTag);
			Enemy->FinishSpawning(FTransform(FVector(-20000, 0, 300)));
			Combat->RegisterPreplacedEnemy(Enemy);
		}
		UClass* Class = LoadClass<ARoomCombatBarrier>(nullptr, TEXT("/Game/Blueprints/Room/BP_RoomCombatBarrier.BP_RoomCombatBarrier_C"));
		Barrier = Class ? GetWorld()->SpawnActorDeferred<ARoomCombatBarrier>(Class, FTransform(FVector(900, 0, 300))) : nullptr;
		if (Barrier)
		{
			PanelBarrierProbe::SetTag(Barrier.Get());
			Barrier->FinishSpawning(FTransform(FVector(900, 0, 300)));
			// 원본 BP의 충돌 크기를 그대로 사용하고, 출입구 하단만 검증 바닥에 맞춘다.
			const float HalfHeight = CastChecked<UBoxComponent>(Barrier->GetRootComponent())->GetScaledBoxExtent().Z;
			Barrier->SetActorLocation(FVector(900, 0, HalfHeight));
		}
		Shooter->SetActorLocation(FVector(900, -100, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
		Partner->SetActorLocation(FVector(900, 100, 150));
		Room->RefreshOverlappingRoomAssignments();
	}
	ForceNetUpdate();
}

void APanelBarrierNetworkProbe::PerformAction()
{
	if (CaseIndex < 2 && Panel)
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Panel);
		// 장식/지지대까지 합친 Bounds 중심에는 실제 발판이 없을 수 있다.
		// 각 BP의 PanelMesh 실제 상면을 샘플링하여 두 캡슐을 분리해 배치한다.
		UStaticMeshComponent* Surface = Panel->FindComponentByClass<UStaticMeshComponent>();
		for (UStaticMeshComponent* Mesh : Meshes)
			if (Mesh->GetFName() == TEXT("PanelMesh")) Surface = Mesh;
		const FBox Bounds = Surface->Bounds.GetBox();
		TArray<FVector> Placements;
		for (ACharacter* Character : {static_cast<ACharacter*>(Shooter), static_cast<ACharacter*>(Partner)})
		{
			bool bPlaced = false;
			for (float X : {0.0f, -0.25f, 0.25f, -0.5f, 0.5f, -0.75f, 0.75f})
			{
				for (float Y : {0.0f, -0.25f, 0.25f, -0.5f, 0.5f, -0.75f, 0.75f})
				{
					const FVector XY = Bounds.GetCenter() + Bounds.GetExtent() * FVector(X, Y, 0);
					FHitResult Hit;
					FCollisionQueryParams Query(SCENE_QUERY_STAT(PanelProbeSurface), false, this);
					if (!Surface->LineTraceComponent(Hit, FVector(XY.X, XY.Y, Bounds.Max.Z + 100),
						FVector(XY.X, XY.Y, Bounds.Min.Z - 100), Query) || Hit.ImpactNormal.Z < 0.5f) continue;
					const auto* Capsule = Character->GetCapsuleComponent();
					const FVector Location = Hit.ImpactPoint + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2);
					Query.AddIgnoredActor(Shooter);
					Query.AddIgnoredActor(Partner);
					// 상면 Trace만 성공해도 옆의 지지대가 캡슐 몸통에 겹칠 수 있다.
					if (GetWorld()->OverlapBlockingTestByChannel(Location, Capsule->GetComponentQuat(),
						Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-0.1f), Query,
						FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) continue;
					if (!Placements.IsEmpty() && FVector::Dist2D(Location, Placements[0]) <
						Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius() + Capsule->GetScaledCapsuleRadius() + 10) continue;
					Character->SetActorLocation(Location);
					Placements.Add(Location);
					bPlaced = true;
					break;
				}
				if (bPlaced) break;
			}
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("event"), TEXT("SurfacePlacement"));
			Row->SetBoolField(TEXT("passed"), bPlaced);
			Row->SetStringField(TEXT("pawn"), Character->GetName());
			Row->SetStringField(TEXT("location"), Character->GetActorLocation().ToString());
			Write(Row);
		}
	}
	BeforeShooter = Shooter->GetActorLocation();
	BeforePartner = Partner->GetActorLocation();
	BeforePanel = Panel ? Panel->GetActorLocation() : FVector::ZeroVector;
	if (CaseIndex < 2) bActionAccepted = Panel && Panel->MoveToTarget();
	else
	{
		FRoomCombatPreparationContext Context;
		auto* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		bActionAccepted = Barrier && Combat->BeginInitialDetectionPreparation(PanelBarrierProbe::RoomTag, Shooter, Context);
		if (bActionAccepted) Combat->CompleteInitialDetectionPreparation(Context);
	}
	Phase = 1;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

void APanelBarrierNetworkProbe::CaptureExpected()
{
	ExpectedShooter = Shooter->GetActorLocation();
	ExpectedPartner = Partner->GetActorLocation();
	ExpectedPanel = Panel ? Panel->GetActorLocation() : FVector::ZeroVector;
	bExpectedBarrier = Barrier && Barrier->IsBlocked();
	bServerPassed = bActionAccepted && Partner->GetCharacterMovement()->MovementMode == MOVE_Flying;
	if (CaseIndex < 2)
		bServerPassed &= FMath::IsNearlyEqual(ExpectedPanel.Z - BeforePanel.Z, 300, 1)
			&& FMath::IsNearlyEqual(ExpectedShooter.Z - BeforeShooter.Z, 300, 5)
			&& FMath::IsNearlyEqual(ExpectedPartner.Z - BeforePartner.Z, 300, 5)
			&& MotionSamples >= 5 && MaxPenetrationSeconds < 0.75f;
	else
	{
		bServerPassed &= bExpectedBarrier && ExpectedShooter.X < BeforeShooter.X - 10 && ExpectedPartner.X < BeforePartner.X - 10;
		for (ACharacter* Character : {static_cast<ACharacter*>(Shooter), static_cast<ACharacter*>(Partner)})
		{
			const auto* Capsule = Character->GetCapsuleComponent();
			bServerPassed &= !Barrier->OverlapsJoinCapsule(Character->GetActorLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		}
	}
	auto Row = Observe();
	Row->SetStringField(TEXT("event"), TEXT("ServerDecision"));
	Row->SetBoolField(TEXT("passed"), bServerPassed);
	Write(Row);
	Phase = 2;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

void APanelBarrierNetworkProbe::ReceiveObservation(APanelBarrierProbeController* Sender, int32 Index, bool bPassed)
{
	if (!PanelBarrierProbe::Enabled() || !HasAuthority() || Phase != 2 || Index != CaseIndex || !Sender || Observers.Contains(Sender)) return;
	Observers.Add(Sender);
	bAllPassed &= bPassed;
}

void APanelBarrierNetworkProbe::ObserveLocal()
{
	auto* PC = Cast<APanelBarrierProbeController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !PC->IsLocalController() || !PC->GetPawn()) return;
	if (!bConnected)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("Connected"));
		Write(Row);
		PC->ServerPanelBarrierReady();
		bConnected = true;
	}
	if (!Shooter || !Partner || Phase != 2 || ObservedCase == CaseIndex
		|| (CaseIndex < 2 && !Panel) || (CaseIndex == 2 && !Barrier)) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bSame = Shooter->GetActorLocation().Equals(ExpectedShooter, 5) && Partner->GetActorLocation().Equals(ExpectedPartner, 5)
		&& (!Panel || Panel->GetActorLocation().Equals(ExpectedPanel, 2)) && (Barrier && Barrier->IsBlocked()) == bExpectedBarrier
		&& Partner->GetCharacterMovement()->MovementMode == MOVE_Flying;
	if (!bSame) { StableSince = -1; return; }
	if (StableSince < 0) StableSince = Now;
	if (Now - StableSince < 0.5f) return;
	auto Row = Observe();
	const bool bMotionPassed = CaseIndex >= 2 || (MotionSamples >= 5 && MaxPenetrationSeconds < 0.75f);
	Row->SetBoolField(TEXT("passed"), bMotionPassed);
	Write(Row);
	PC->ServerPanelBarrierObserved(CaseIndex, bMotionPassed);
	ObservedCase = CaseIndex;
	StableSince = -1;
}

void APanelBarrierNetworkProbe::SampleMotion()
{
	if (MotionSampleCase != CaseIndex)
	{
		MotionSampleCase = CaseIndex;
		MotionSamples = 0;
		LastMotionSample = -1;
		PenetrationSince = -1;
		MaxPenetrationSeconds = 0;
	}
	if (CaseIndex < 0 || CaseIndex >= 2 || Phase != 1 || !Panel || !Shooter || !Partner) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (LastMotionSample >= 0 && Now - LastMotionSample < 0.1f) return;
	LastMotionSample = Now;
	++MotionSamples;
	bool bAnyPenetration = false;
	auto Row = Observe();
	Row->SetStringField(TEXT("event"), TEXT("MotionSample"));
	Row->SetNumberField(TEXT("worldTime"), Now);
	Row->SetNumberField(TEXT("panelZ"), Panel->GetActorLocation().Z);
	for (const auto& Item : {TPair<FString, ACharacter*>(TEXT("shooter"), Shooter), TPair<FString, ACharacter*>(TEXT("partner"), Partner)})
	{
		const auto* Capsule = Item.Value->GetCapsuleComponent();
		FCollisionQueryParams Query(SCENE_QUERY_STAT(PanelProbePenetration), false, this);
		Query.AddIgnoredActor(Shooter);
		Query.AddIgnoredActor(Partner);
		TArray<FOverlapResult> Overlaps;
		// 접촉 오차 3cm는 허용하되 실제 자식 충돌체 내부에 캡슐이 계속 남는지 관측한다.
		const float Radius = FMath::Max(1.0f, Capsule->GetScaledCapsuleRadius() - 3);
		const float Height = FMath::Max(Radius, Capsule->GetScaledCapsuleHalfHeight() - 3);
		GetWorld()->OverlapMultiByChannel(Overlaps, Item.Value->GetActorLocation(), Capsule->GetComponentQuat(), ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, Height), Query);
		bool bPenetrating = false;
		for (const FOverlapResult& Hit : Overlaps) bPenetrating |= Hit.bBlockingHit && Hit.GetActor() == Panel;
		bAnyPenetration |= bPenetrating;
		Row->SetBoolField(Item.Key + TEXT("PenetratingPanel"), bPenetrating);
		Row->SetNumberField(Item.Key + TEXT("RelativeBottomZ"), Item.Value->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight() - Panel->GetActorLocation().Z);
	}
	if (bAnyPenetration)
	{
		if (PenetrationSince < 0) PenetrationSince = Now;
		MaxPenetrationSeconds = FMath::Max(MaxPenetrationSeconds, Now - PenetrationSince);
	}
	else PenetrationSince = -1;
	Write(Row);
}

void APanelBarrierNetworkProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!PanelBarrierProbe::Enabled() || bFinished) return;
	SampleMotion();
	// Listen 호스트도 실제 로컬 소유 Pawn을 관측한다. Dedicated에는 로컬 Controller가 없다.
	ObserveLocal();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority())
	{
		if (Phase == 3)
		{
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("Complete"));
			Row->SetBoolField(TEXT("passed"), bAllPassed); Row->SetNumberField(TEXT("expectedCases"), PanelBarrierProbe::CaseCount); Write(Row);
			bFinished = true;
		}
		return;
	}
	if (!Shooter || !Partner) return;
	if (CaseIndex < 0)
	{
		int32 Ready = 0;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (auto* PC = Cast<APanelBarrierProbeController>(It->Get()); PC && PC->bProbeReady) ++Ready;
		if (Ready != 2) return;
		if (GetNetMode() == NM_DedicatedServer)
		{
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("Connected")); Write(Row);
		}
		CaseIndex = 0;
		SetupCase();
		return;
	}
	if (Phase == 0 && Now - PhaseStart > 2) PerformAction();
	else if (Phase == 1 && Now - PhaseStart > ActionWaitSeconds) CaptureExpected();
	else if (Phase == 2 && (Observers.Num() == 2 || Now - PhaseStart > 15))
	{
		const bool bPassed = bServerPassed && Observers.Num() == 2;
		bAllPassed &= bPassed;
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("event"), TEXT("CaseResult"));
		Row->SetBoolField(TEXT("passed"), bPassed); Row->SetNumberField(TEXT("observers"), Observers.Num()); Write(Row);
		if (++CaseIndex == PanelBarrierProbe::CaseCount)
		{
			Phase = 3; PhaseStart = Now; ForceNetUpdate();
			auto Complete = MakeShared<FJsonObject>(); Complete->SetStringField(TEXT("event"), TEXT("Complete"));
			Complete->SetBoolField(TEXT("passed"), bAllPassed); Complete->SetNumberField(TEXT("expectedCases"), PanelBarrierProbe::CaseCount); Write(Complete);
		}
		else SetupCase();
	}
	else if (Phase == 3 && Now - PhaseStart > 3)
	{
		bFinished = true;
		FPlatformMisc::RequestExitWithStatus(false, bAllPassed ? 0 : 1);
	}
}
