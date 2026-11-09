#include "Drone/Partner/HackTargetPointNetworkProbe.h"

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "OutlierPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Enemy/AutoTurret.h"
#include "Engine/NetDriver.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interface/HackableInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/HackCandidateLayerWidget.h"
#include "UI/HackCandidateMarkerWidget.h"
#endif

namespace HackTargetProbe
{
	bool Enabled()
	{
#if WITH_DEV_AUTOMATION_TESTS
		FString Role;
		return FParse::Value(FCommandLine::Get(), TEXT("OutlierHackTargetProbe="), Role)
			&& (Role == TEXT("Server") || Role == TEXT("Owner") || Role == TEXT("Observer"));
#else
		return false;
#endif
	}

#if WITH_DEV_AUTOMATION_TESTS
	constexpr float Range = 600;
	constexpr double PositionTolerance = 0.01; // 안정된 동일 Transform 비교 허용오차: 0.01cm.
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Blueprint;
		bool bCandidate = true;
		bool bServerAccepts = true;
	};
	const TCHAR* Switch = TEXT("Interaction/BP_DoorSwitch");
	const FCase Cases[] = {
		{TEXT("SCS_Possessable"), TEXT("Partner/BP_HackTestActor")},
		// 메시가 없는 BP도 전용 Query 충돌체로 수집되고 같은 목표점에 UI가 표시되어야 한다.
		{TEXT("SCS_NonPossessable"), TEXT("Partner/BP_HackTestActor2")},
		{TEXT("Enemy"), TEXT("Enemy/VECDrone/BP_VECDrone_Gun")},
		{TEXT("StatMachine"), TEXT("Interaction/BP_InteractionStatMachine")},
		{TEXT("Jumper"), TEXT("Object/Hack/BP_Jumper")},
		{TEXT("MagneticTower"), TEXT("Object/Hack/BP_MagneticTower")},
		{TEXT("TaggedSwitch"), Switch},
		{TEXT("UntaggedSwitch"), Switch, false, false},
		{TEXT("RangeInside"), Switch},
		{TEXT("RangeOutside"), Switch, false, false},
		{TEXT("AngleInside"), Switch},
		{TEXT("AngleOutside"), Switch, false, false},
		{TEXT("WallOccluded"), Switch, false, false},
		{TEXT("TargetHitLOS"), Switch},
		{TEXT("PointInsideCollisionOutside"), Switch, false, true},
		{TEXT("OffsetRotatedScaled"), Switch},
		{TEXT("MissingPointFallback"), Switch},
		{TEXT("LaserTurretNeck"), TEXT("Enemy/Turret/BP_LaserTurret")},
		{TEXT("RifleTurretNeck"), TEXT("Enemy/Turret/BP_RifleTurret")},
		{TEXT("ServerMovedAfterCandidate"), Switch, true, false},
		{TEXT("ServerLockedAfterCandidate"), Switch, true, false},
		{TEXT("DestroyWhileActive"), Switch},
		{TEXT("CheckpointRestore"), Switch}
	};

	UClass* LoadBlueprint(const TCHAR* Path)
	{
		const FString Package = FString(TEXT("/Game/Blueprints/")) + Path;
		return LoadClass<AActor>(nullptr, *(Package + TEXT(".") + FPaths::GetCleanFilename(Path) + TEXT("_C")));
	}

	UHackableComponent* Hackable(AActor* Actor)
	{
		IHackableInterface* Interface = Cast<IHackableInterface>(Actor);
		return Interface ? Interface->GetHackableComponent() : nullptr;
	}

	void ConfigureTarget(AActor* Actor, int32 Index)
	{
		UHackableComponent* Component = Hackable(Actor);
		if (!Component) return;
		// 배치된 에셋은 저장하지 않는다. 격리 인스턴스만 미니게임 시작/취소를 반복할 수 있게 설정한다.
		Component->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
		Component->HackTags.RemoveTag(OutlierGameplayTags::State::HackedOnce());
		Component->HackTags.RemoveTag(HackGameplayTags::MiniGame::None());
		Component->HackTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Hack.MiniGame.ClickCircle")));
		Component->HackTags.AddTag(HackGameplayTags::Use::Multiple());
		if (Index == 7)
		{
			Component->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
			Component->HackTags.RemoveTag(HackGameplayTags::Target::NonPossessable());
		}
		else if (!Component->IsHackTargetType()) Component->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
		if (Index == 22)
		{
			Component->CheckpointProgressId = TEXT("HackTargetPointNetworkProbeCheckpoint");
			// 복원은 실제 완료/등록 경로를 사용하되, 이 fixture에 없는 문 연결이나 폭발 효과는 실행하지 않는다.
			Component->SuccessEffectTags.Reset();
		}
	}
#endif
}

void AHackTargetPointProbeController::ServerProbeReady_Implementation()
{
	if (HackTargetProbe::Enabled()) bProbeReady = true;
}

AHackTargetPointProbeGameMode::AHackTargetPointProbeGameMode()
{
	if (HackTargetProbe::Enabled())
	{
		PlayerControllerClass = AHackTargetPointProbeController::StaticClass();
		PlayerStateClass = AOutlierPlayerState::StaticClass();
		DefaultPawnClass = nullptr;
		bStartPlayersAsSpectators = true;
	}
}

void AHackTargetPointProbeGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
#if WITH_DEV_AUTOMATION_TESTS
	if (!HackTargetProbe::Enabled() || GetNumPlayers() > 1) return;
	UClass* PartnerClass = HackTargetProbe::LoadBlueprint(TEXT("Partner/BP_PartnerCharacter"));
	UClass* ShooterClass = HackTargetProbe::LoadBlueprint(TEXT("Shooter/BP_ShooterCharacter"));
	if (!PartnerClass || !ShooterClass) return;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APartnerCharacter* Partner = GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
	AShooterCharacter* Shooter = GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, FVector(-200, 0, 1000), FRotator::ZeroRotator, Spawn);
	AOutlierPlayerState* ShooterState = GetWorld()->SpawnActor<AOutlierPlayerState>();
	AOutlierPlayerState* PartnerState = NewPlayer->GetPlayerState<AOutlierPlayerState>();
	if (!Partner || !Shooter || !ShooterState || !PartnerState) return;
	Shooter->SetPlayerState(ShooterState);
	ShooterState->SetPlayerRole(EOutlierPlayerRole::Shooter);
	ShooterState->SetPairId(0);
	ShooterState->SetShooterCharacter(Shooter);
	ShooterState->SetPartnerCharacter(Partner);
	ShooterState->SetAcquiredSuit(true);
	PartnerState->SetPlayerRole(EOutlierPlayerRole::Partner);
	PartnerState->SetPairId(0);
	PartnerState->SetShooterCharacter(Shooter);
	PartnerState->SetPartnerCharacter(Partner);
	NewPlayer->Possess(Partner);
	Partner->GetCharacterMovement()->DisableMovement();
	Shooter->GetCharacterMovement()->DisableMovement();
	UPartnerHackComponent* HackComponent = Partner->FindComponentByClass<UPartnerHackComponent>();
	HackComponent->RefreshCharacterRefsFromPlayerState();
	// 격리 검증의 대기를 줄이는 수치만 변경한다. 능력 부여와 발동/충전 검사는 실제 GAS 경로를 사용한다.
	UOutlierAbilitySystemComponent* ASC = Partner->GetOutlierAbilitySystemComponent();
	auto Config = ASC->GetPartnerAbilityConfig();
	Config.HackEffectiveRange = HackTargetProbe::Range;
	Config.HackCooldown = 0.1f;
	ASC->UpdatePartnerAbilityConfig(Config);
	FPartnerHackAbilityData Data;
	Data.EffectiveRange = HackTargetProbe::Range;
	HackComponent->CacheAbilityData(Data);
	HackComponent->CandidateHalfAngleDegrees = 20;
	AHackTargetPointNetworkProbe* Probe = GetWorld()->SpawnActor<AHackTargetPointNetworkProbe>();
	Probe->SetOwner(NewPlayer);
	Probe->Initialize(Partner);
#endif
}

AHackTargetPointNetworkProbe::AHackTargetPointNetworkProbe()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30);
	PrimaryActorTick.bCanEverTick = HackTargetProbe::Enabled();
}

void AHackTargetPointNetworkProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHackTargetPointNetworkProbe, Partner);
	DOREPLIFETIME(AHackTargetPointNetworkProbe, Target);
	DOREPLIFETIME(AHackTargetPointNetworkProbe, CaseIndex);
	DOREPLIFETIME(AHackTargetPointNetworkProbe, Phase);
}

void AHackTargetPointNetworkProbe::Initialize(APartnerCharacter* InPartner)
{
	Partner = InPartner;
}

void AHackTargetPointNetworkProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
	if (!HackTargetProbe::Enabled()) return;
	if (Output.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("OutlierHackTargetOutput="), Output);
		FParse::Value(FCommandLine::Get(), TEXT("OutlierHackTargetProbe="), Role);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	}
	if (!Partner) return;
	if (!HackComponent) HackComponent = Partner->FindComponentByClass<UPartnerHackComponent>();
	if (HasAuthority()) ServerTick();
	else ClientTick();
	SampleTurretDeployment();
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void AHackTargetPointNetworkProbe::Write(const TSharedRef<FJsonObject>& Row)
{
	Row->SetStringField(TEXT("role"), Role);
	Row->SetNumberField(TEXT("netMode"), int32(GetNetMode()));
	Row->SetBoolField(TEXT("authority"), HasAuthority());
	Row->SetNumberField(TEXT("case"), CaseIndex);
	Row->SetNumberField(TEXT("worldTime"), GetWorld()->GetTimeSeconds());
	if (CaseIndex >= 0 && CaseIndex < UE_ARRAY_COUNT(HackTargetProbe::Cases))
		Row->SetStringField(TEXT("name"), HackTargetProbe::Cases[CaseIndex].Name);
	FString Json;
	FJsonSerializer::Serialize(Row, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

void AHackTargetPointNetworkProbe::ApplyFixedPointFixture()
{
	using namespace HackTargetProbe;
	UHackableComponent* Component = Hackable(Target);
	if (!Component) return;
	// 참조·상대 위치는 복제값이 아니다. 양쪽에서 같은 고정 fixture를 적용하며 런타임 동기화 기능으로 해석하지 않는다.
	if (CaseIndex == 16) Component->HackTargetPointReference.PathToComponent = TEXT("MissingProbePoint");
	if (CaseIndex == 16) Component->HackTargetPointReference.ComponentProperty = NAME_None;
	if (USceneComponent* Point = Cast<USceneComponent>(Component->HackTargetPointReference.GetComponent(Target)))
	{
		if (CaseIndex == 14) Point->SetRelativeLocation(FVector(-200, 0, 0));
		if (CaseIndex == 15) Point->SetRelativeLocation(FVector(30, 40, 60));
	}
	Component->RefreshHackTargetPoint();
	if (CaseIndex == 12 && !HasAuthority() && !Wall)
	{
		// 고정 차폐물은 양쪽 월드에 같은 fixture로 만든다. 해킹 목표점에 충돌을 붙이지 않는다.
		Wall = GetWorld()->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(5, 200, 200));
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Wall->SetActorLocation(Target->GetActorLocation() - FVector(200, 0, 0));
	}
}

void AHackTargetPointNetworkProbe::StartCase()
{
	using namespace HackTargetProbe;
	if (Target) Target->Destroy();
	if (Wall) { Wall->Destroy(); Wall = nullptr; }
	OwnerObservation.Reset();
	bClientCleaned = false;
	bRestoreObserved = false;
	++CaseIndex;
	Phase = 0;
	PhaseStart = GetWorld()->GetTimeSeconds();
	if (CaseIndex >= UE_ARRAY_COUNT(Cases))
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("Complete"));
		Row->SetNumberField(TEXT("expectedCases"), UE_ARRAY_COUNT(Cases));
		Row->SetBoolField(TEXT("passed"), bAllPassed);
		Write(Row);
		SetActorTickEnabled(false);
		FPlatformMisc::RequestExit(false);
		return;
	}
	UClass* Class = LoadBlueprint(Cases[CaseIndex].Blueprint);
	if (!Class) { FinishResult(false, TEXT("Class load failed")); return; }
	APlayerController* PC = Cast<APlayerController>(Partner->GetController());
	PC->SetControlRotation(FRotator::ZeroRotator);
	FVector View;
	FRotator Rotation;
	PC->GetPlayerViewPoint(View, Rotation);
	FVector Offset(400, 0, 0);
	if (CaseIndex == 8) Offset.X = Range - 1;
	if (CaseIndex == 9) Offset.X = Range + 1;
	if (CaseIndex == 10 || CaseIndex == 11)
		Offset = FRotator(0, CaseIndex == 10 ? 19.5f : 20.5f, 0).Vector() * 400;
	if (CaseIndex == 14) Offset.X = Range + 150;
	// 기본 이동 복제의 정수 위치/Byte 회전으로도 정확히 표현되는 배치를 쓴다.
	// 목표점 비교 오차를 키워 네트워크 양자화나 사거리 경계 차이를 숨기지 않는다.
	const FVector Location = (View + Offset).GridSnap(1);
	FTransform Transform(Location);
	if (CaseIndex == 15) Transform = FTransform(FRotator(0, 90, 0), Location, FVector(1.3f, 1.5f, 1.2f));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.bDeferConstruction = true;
	Target = GetWorld()->SpawnActor<AActor>(Class, Transform, Spawn);
	if (!Target) { FinishResult(false, TEXT("Target spawn failed")); return; }
	Target->bAlwaysRelevant = true;
	Target->SetReplicates(true);
	Target->SetReplicateMovement(true);
	if (APawn* Pawn = Cast<APawn>(Target)) Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
	ConfigureTarget(Target, CaseIndex);
	Target->FinishSpawning(Transform);
	if (AAutoTurret* Turret = Cast<AAutoTurret>(Target))
	{
		// 닫힌 터렛은 기존 정책상 잠겨 있다. 전개 경로를 거친 활성 상태에서 목표점을 검사한다.
		Turret->PrepareForRoomWaveActivation();
		Turret->BeginRoomWaveDeployment(1);
		// 실제 전개를 재생한다. 이 격리 맵에는 Room Wave 승인이 없으므로 재생 후 ServerTick에서만 완료한다.
	}
	if (ACharacter* Character = Cast<ACharacter>(Target)) Character->GetCharacterMovement()->DisableMovement();
	if (UStateTreeComponent* Tree = Target->FindComponentByClass<UStateTreeComponent>()) Tree->StopLogic(TEXT("해킹 목표점 격리 검증"));
	ApplyFixedPointFixture();
	OriginalTargetPath = Target->GetPathName();
	if (CaseIndex == 12)
	{
		Wall = GetWorld()->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(5, 200, 200));
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Wall->SetActorLocation(View + FVector(200, 0, 0));
	}
	ForceNetUpdate();
}

TSharedRef<FJsonObject> AHackTargetPointNetworkProbe::Observe()
{
	using namespace HackTargetProbe;
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("Observation"));
	Row->SetStringField(TEXT("targetPath"), IsValid(Target) ? Target->GetPathName() : OriginalTargetPath);
	const FVector Point = IHackableInterface::ResolveHackTargetLocation(Target);
	Row->SetNumberField(TEXT("pointX"), Point.X);
	Row->SetNumberField(TEXT("pointY"), Point.Y);
	Row->SetNumberField(TEXT("pointZ"), Point.Z);
	Row->SetStringField(TEXT("targetClass"), GetPathNameSafe(Target ? Target->GetClass() : nullptr));
	UHackableComponent* Component = Hackable(Target);
	USceneComponent* Scene = Component ? Cast<USceneComponent>(Component->HackTargetPointReference.GetComponent(Target)) : nullptr;
	Row->SetStringField(TEXT("reference"), Component ? Component->HackTargetPointReference.ComponentProperty.ToString() : TEXT(""));
	Row->SetStringField(TEXT("parent"), Scene ? GetNameSafe(Scene->GetAttachParent()) : TEXT(""));
	Row->SetBoolField(TEXT("rootParent"), Scene && Scene->GetAttachParent() == Target->GetRootComponent());
	Row->SetStringField(TEXT("socket"), Scene ? Scene->GetAttachSocketName().ToString() : TEXT(""));
	Row->SetBoolField(TEXT("hackedOnce"), Component && Component->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));
	Row->SetStringField(TEXT("tags"), Component ? Component->HackTags.ToStringSimple() : TEXT(""));
	UPartnerHackComponent* Hack = HackComponent;
	const bool bOwner = Partner->IsLocallyControlled();
	const bool bCandidate = bOwner && Hack->GetHackCandidateActors().Contains(Target);
	Row->SetBoolField(TEXT("candidate"), bCandidate);
	Row->SetBoolField(TEXT("uiExpected"), bOwner && Cases[CaseIndex].bCandidate);
	double UIError = -1;
	if (bCandidate && Hack->CandidateLayerWidget)
	{
		UHackCandidateLayerWidget* Layer = Hack->CandidateLayerWidget;
		if (const auto* Marker = Layer->MarkerWidgets.Find(Target))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>((*Marker)->Slot))
			{
				FVector2D Projected;
				APlayerController* PC = Cast<APlayerController>(Partner->GetController());
				float ExpectedScale;
				// 슬롯은 이제 중심점이 아닌 좌상단을 보관한다. 거리 배율·화면 경계 보정까지 포함해 비교한다.
				if (Layer->HudPresentation.Project(PC, Point, Layer->MarkerSize, Projected, ExpectedScale))
				{
					UIError = FMath::Max(FVector2D::Distance(Slot->GetPosition(), Projected),
						double(FMath::Abs((*Marker)->GetRenderTransform().Scale.X - ExpectedScale)));
				}
			}
		}
	}
	Row->SetNumberField(TEXT("uiError"), UIError);
	if (bOwner)
	{
		const FHackQueryContext Context = Hack->BuildQueryContext();
		// 후보 탈락이 목표점 판정 때문인지, 기존 충돌체 수집 단계 때문인지 나눠 기록한다.
		FVector2D Screen;
		Row->SetBoolField(TEXT("pointConditionsValid"), Hack->IsCandidateActorValid(Target, Component, Context, Screen));
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		FCollisionQueryParams Query;
		Query.AddIgnoredActor(Partner);
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Context.ViewLocation, FQuat::Identity, Objects,
			FCollisionShape::MakeSphere(HackTargetProbe::Range), Query);
		Row->SetBoolField(TEXT("collisionCollected"), Overlaps.ContainsByPredicate([this](const FOverlapResult& Hit) { return Hit.GetActor() == Target; }));
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Target);
		TArray<TSharedPtr<FJsonValue>> CollisionRows;
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			auto Collision = MakeShared<FJsonObject>();
			Collision->SetStringField(TEXT("component"), Primitive->GetName());
			Collision->SetNumberField(TEXT("objectType"), int32(Primitive->GetCollisionObjectType()));
			Collision->SetBoolField(TEXT("queryEnabled"), Primitive->IsQueryCollisionEnabled());
			CollisionRows.Add(MakeShared<FJsonValueObject>(Collision));
		}
		Row->SetArrayField(TEXT("collisionComponents"), CollisionRows);
		Row->SetNumberField(TEXT("viewX"), Context.ViewLocation.X);
		Row->SetNumberField(TEXT("viewY"), Context.ViewLocation.Y);
		Row->SetNumberField(TEXT("viewZ"), Context.ViewLocation.Z);
		Row->SetBoolField(TEXT("searching"), Hack->IsHackCandidateSearchActive());
		Row->SetBoolField(TEXT("inputAllowed"), Partner->CanAcceptInput());
	}
	return Row;
}

void AHackTargetPointNetworkProbe::SampleTurretDeployment()
{
	if (Phase != 0 || (CaseIndex != 17 && CaseIndex != 18)) return;
	AAutoTurret* Turret = Cast<AAutoTurret>(Target);
	if (!Turret || !Turret->DeployMontage) return;
	USceneComponent* Point = Cast<USceneComponent>(HackTargetProbe::Hackable(Target)->HackTargetPointReference.GetComponent(Target));
	UAnimInstance* AnimInstance = Turret->GetMesh()->GetAnimInstance();
	if (!Point || !AnimInstance) return;
	if (DeploymentSampleCase != CaseIndex)
	{
		DeploymentSampleCase = CaseIndex;
		DeploymentSampleStage = -1;
	}
	const float Position = AnimInstance->Montage_GetPosition(Turret->DeployMontage);
	const int32 Stage = Turret->IsCombatActive() ? 4 : FMath::Clamp(FMath::FloorToInt(Position), 0, 3);
	if (Stage <= DeploymentSampleStage) return;
	DeploymentSampleStage = Stage;
	// 각 프로세스의 실제 본 평가 결과로 검사한다. 서버 좌표를 클라이언트 목표점에 덮어쓰지 않는다.
	const FVector Expected = Turret->GetMesh()->GetSocketTransform(TEXT("TurretNeck")).TransformPosition(Point->GetRelativeLocation());
	const FVector Actual = IHackableInterface::ResolveHackTargetLocation(Target);
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("Deployment"));
	Row->SetNumberField(TEXT("stage"), Stage);
	Row->SetNumberField(TEXT("montagePosition"), Position);
	Row->SetNumberField(TEXT("pointZ"), Actual.Z);
	Row->SetNumberField(TEXT("neckError"), FVector::Distance(Expected, Actual));
	Row->SetBoolField(TEXT("neckAttached"), Point->GetAttachParent() == Turret->GetMesh()
		&& Point->GetAttachSocketName() == FName(TEXT("TurretNeck")));
	Write(Row);
}

void AHackTargetPointNetworkProbe::ClientTick()
{
	if (!bConnected)
	{
		AHackTargetPointProbeController* PC = Cast<AHackTargetPointProbeController>(GetWorld()->GetFirstPlayerController());
		if (!PC || !PC->IsLocalController()) return;
		PC->ServerProbeReady();
		bConnected = true;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("Connected"));
		Write(Row);
	}
	if (CaseIndex < 0 || CaseIndex >= UE_ARRAY_COUNT(HackTargetProbe::Cases) || (!Target && Phase != 2)) return;
	if (LocalCase != CaseIndex)
	{
		if (Wall) { Wall->Destroy(); Wall = nullptr; }
		LocalCase = CaseIndex;
		LocalPhase = -1;
		LocalStart = GetWorld()->GetTimeSeconds();
		bSearchRequested = false;
		bObservationSent = false;
		ApplyFixedPointFixture();
		HackComponent->CandidateHalfAngleDegrees = 20;
	}
	if (LocalPhase != Phase)
	{
		LocalPhase = Phase;
		LocalStart = GetWorld()->GetTimeSeconds();
	}
	const float Age = GetWorld()->GetTimeSeconds() - LocalStart;
	const bool bOwner = Partner->IsLocallyControlled();
	// 전개 중 각 클라이언트의 몽타주 시간은 다를 수 있다. 좌표·UI 비교는 전개가 끝나 안정된 뒤 수행한다.
	if (Phase == 0 && (CaseIndex == 17 || CaseIndex == 18))
	{
		const AAutoTurret* Turret = Cast<AAutoTurret>(Target);
		if (!Turret || !Turret->IsCombatActive() || (!bSearchRequested && Age < 5.5f)) return;
	}
	if (Phase == 3 && Age > 0.8f && !bRestoreObserved)
	{
		auto Row = Observe();
		Row->SetStringField(TEXT("event"), TEXT("Restored"));
		Row->SetBoolField(TEXT("uiExpected"), false);
		Write(Row);
		bRestoreObserved = true;
		if (bOwner)
		{
			FString Json;
			FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
			ServerObserve(CaseIndex, Json);
		}
		return;
	}
	if (bOwner && Phase == 2 && Age > 0.8f && bObservationSent)
	{
		// 손실 환경에서는 취소 RPC와 종료 응답이 재전송될 수 있다. 한 시점의 미완료를 영구 실패로 고정하지 않는다.
		const bool bClean = !HackComponent->IsHackInteractionActive();
		if (!bClean && Age < 6) return;
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("Cleanup"));
		Row->SetBoolField(TEXT("clean"), bClean);
		Row->SetNumberField(TEXT("elapsed"), Age);
		Write(Row);
		FString Json;
		FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
		ServerObserve(CaseIndex, Json);
		bObservationSent = false;
		return;
	}
	if (!bOwner)
	{
		if (Age > 0.8f && !bObservationSent)
		{
			Write(Observe());
			bObservationSent = true;
		}
		return;
	}
	UPartnerHackComponent* Hack = HackComponent;
	if (!bSearchRequested && Age > 0.8f)
	{
		Hack->TryHack();
		bSearchRequested = true;
		// 전개 대기 시간과 검색 후 UI 갱신 대기 시간을 분리한다. 생성 프레임의 Canvas 좌표를 읽지 않는다.
		LocalStart = GetWorld()->GetTimeSeconds();
		return;
	}
	if (Phase == 0 && bSearchRequested && Hack->IsHackCandidateSearchActive() && Age > 1.8f && !bObservationSent)
	{
		Hack->RefreshHackCandidates();
		auto Row = Observe();
		Write(Row);
		FString Json;
		FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
		ServerObserve(CaseIndex, Json);
		bObservationSent = true;
	}
}

void AHackTargetPointNetworkProbe::ServerTick()
{
	using namespace HackTargetProbe;
	// 0: 후보/UI 관측 → 1: 실제 시작 RPC 결과 → 2: 취소·성공·파괴 후 정리 → 3: 진행 상태 재생성 복원.
	if (!bStarted)
	{
		// 접속 수만으로 시작하면 늦게 로드된 관전자가 첫 대상을 놓친다. 실제 Probe/Partner 수신을 양쪽에서 확인한다.
		int32 ReadyClients = 0;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (const AHackTargetPointProbeController* PC = Cast<AHackTargetPointProbeController>(It->Get()); PC && PC->bProbeReady) ++ReadyClients;
		if (ReadyClients < 2) return;
		bStarted = true;
		StartCase();
		return;
	}
	if (CaseIndex >= UE_ARRAY_COUNT(Cases)) return;
	const float Age = GetWorld()->GetTimeSeconds() - PhaseStart;
	UPartnerHackComponent* Hack = HackComponent;
	if (Phase == 0 && Age > 4.5f)
	{
		if (AAutoTurret* Turret = Cast<AAutoTurret>(Target); Turret && Turret->IsDeploying())
			Turret->CompleteTurretDeployment();
	}
	if (Phase == 0 && Age > 25)
	{
		FinishResult(false, TEXT("소유 클라이언트 후보 관측 시간 초과"));
		Hack->CancelForReboot();
		StartCase();
	}
	else if (Phase == 1 && Age > 1.5f)
	{
		const bool bAccepted = IsValid(Hack->ActiveHackableComponent) && Hack->ActiveHackableComponent == Hackable(Target);
		const FVector Point = IHackableInterface::ResolveHackTargetLocation(Target);
		const FVector Observed(OwnerObservation->GetNumberField(TEXT("pointX")), OwnerObservation->GetNumberField(TEXT("pointY")), OwnerObservation->GetNumberField(TEXT("pointZ")));
		bResultPassed = OwnerObservation->GetBoolField(TEXT("candidate")) == Cases[CaseIndex].bCandidate
			&& bAccepted == Cases[CaseIndex].bServerAccepts;
		// 서버 조건을 의도적으로 바꾼 케이스는 이전 관측 좌표와 같을 필요가 없다.
		if (CaseIndex != 19) bResultPassed &= Point.Equals(Observed, PositionTolerance);
		if (Cases[CaseIndex].bCandidate) bResultPassed &= OwnerObservation->GetNumberField(TEXT("uiError")) >= 0 && OwnerObservation->GetNumberField(TEXT("uiError")) <= 1;
		if (CaseIndex == 14)
			bResultPassed &= OwnerObservation->GetBoolField(TEXT("pointConditionsValid")) && !OwnerObservation->GetBoolField(TEXT("collisionCollected"));
		if (CaseIndex == 17 || CaseIndex == 18)
		{
			USceneComponent* Scene = Cast<USceneComponent>(Hackable(Target)->HackTargetPointReference.GetComponent(Target));
			AAutoTurret* Turret = Cast<AAutoTurret>(Target);
			const FVector Before = Scene->GetComponentLocation();
			Turret->TurretHeadPivot->AddLocalRotation(FRotator(0, 50, 0));
			Turret->TurretHeadPitchPivot->AddLocalRotation(FRotator(25, 0, 0));
			Turret->GetWeaponMuzzleComponent(false)->AddLocalRotation(FRotator(25, 50, 0));
			bResultPassed &= Scene->GetAttachParent() == Turret->GetMesh()
				&& Scene->GetAttachSocketName() == FName(TEXT("TurretNeck"))
				&& Before.Equals(Scene->GetComponentLocation(), PositionTolerance);
		}
		if (CaseIndex == 21) Target->Destroy();
		else ClientFinishHack(CaseIndex == 22);
		Phase = 2;
		PhaseStart = GetWorld()->GetTimeSeconds();
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("event"), TEXT("ServerDecision"));
		Row->SetBoolField(TEXT("accepted"), bAccepted);
		Row->SetNumberField(TEXT("pointX"), Point.X);
		Row->SetNumberField(TEXT("pointY"), Point.Y);
		Row->SetNumberField(TEXT("pointZ"), Point.Z);
		Row->SetStringField(TEXT("targetPath"), OriginalTargetPath);
		Write(Row);
	}
	else if (Phase == 2 && Age > 2 && (bClientCleaned || Age > 8))
	{
		bResultPassed &= !Hack->IsHackInteractionActive() && bClientCleaned;
		if (CaseIndex == 22)
		{
			bResultPassed &= Hackable(Target)->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce());
			const FTransform Transform = Target->GetActorTransform();
			UClass* Class = Target->GetClass();
			Target->Destroy();
			FActorSpawnParameters Spawn;
			Spawn.bDeferConstruction = true;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Target = GetWorld()->SpawnActor<AActor>(Class, Transform, Spawn);
			ConfigureTarget(Target, CaseIndex);
			Target->bAlwaysRelevant = true;
			Target->FinishSpawning(Transform);
			bResultPassed &= Hackable(Target)->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce());
			Phase = 3;
			PhaseStart = GetWorld()->GetTimeSeconds();
			return;
		}
		FinishResult(bResultPassed, TEXT("후보·UI 좌표·서버 시작 판정·종료 정리 비교"));
		Hack->CancelForReboot();
		StartCase();
	}
	else if (Phase == 3 && Age > 2)
	{
		FinishResult(bResultPassed && bRestoreObserved, TEXT("동일 진행 ID 재생성 후 서버·소유 클라이언트 HackedOnce 복원"));
		StartCase();
	}
}

void AHackTargetPointNetworkProbe::FinishResult(bool bPassed, const FString& Detail)
{
	bAllPassed &= bPassed;
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("event"), TEXT("CaseResult"));
	Row->SetStringField(TEXT("detail"), Detail);
	Row->SetBoolField(TEXT("passed"), bPassed);
	Write(Row);
}
#endif

void AHackTargetPointNetworkProbe::ServerObserve_Implementation(int32 ObservedCase, const FString& Observation)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!HackTargetProbe::Enabled() || ObservedCase != CaseIndex) return;
	TSharedPtr<FJsonObject> Row;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Observation), Row)) return;
	if (Phase == 2 && Row->GetStringField(TEXT("event")) == TEXT("Cleanup"))
	{
		bClientCleaned = Row->GetBoolField(TEXT("clean"));
		return;
	}
	if (Phase == 3 && Row->GetStringField(TEXT("event")) == TEXT("Restored"))
	{
		bRestoreObserved = Row->GetBoolField(TEXT("hackedOnce"));
		return;
	}
	if (Phase != 0) return;
	OwnerObservation = Row;
	// 클라이언트 관측값은 테스트 비교에만 쓴다. 실제 시작 허용 여부는 기존 서버 RPC가 다시 계산한다.
	if (CaseIndex == 19) Target->AddActorWorldOffset(FVector(HackTargetProbe::Range * 2, 0, 0));
	if (CaseIndex == 20) HackTargetProbe::Hackable(Target)->HackTags.AddTag(OutlierGameplayTags::State::Locked());
	Phase = 1;
	PhaseStart = GetWorld()->GetTimeSeconds();
	ClientRequestHack(CaseIndex);
#endif
}

void AHackTargetPointNetworkProbe::ClientRequestHack_Implementation(int32 RequestedCase)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (HackTargetProbe::Enabled() && RequestedCase == CaseIndex && Partner && Target && Partner->IsLocallyControlled())
		HackComponent->ServerTryStartHack(Target);
#endif
}

void AHackTargetPointNetworkProbe::ClientFinishHack_Implementation(bool bSuccess)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!HackTargetProbe::Enabled() || !Partner || !Partner->IsLocallyControlled()) return;
	UPartnerHackComponent* Hack = HackComponent;
	if (Hack->ActiveHackableComponent)
	{
		FHackResultContext Result;
		Result.TargetActor = Target;
		Result.Result = bSuccess ? EHackResult::Success : EHackResult::Cancelled;
		Hack->ServerCompleteHack(Result);
	}
	else if (Hack->IsHackCandidateSearchActive()) Hack->TryHack();
#endif
}
