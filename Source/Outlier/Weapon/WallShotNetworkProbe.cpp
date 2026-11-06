#if WITH_DEV_AUTOMATION_TESTS

#include "Weapon/WallShotProbe.h"
#include "Weapon/RangedWeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Shooter/ShooterCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace WallShotNetworkProbe
{
constexpr int32 CaseCount = 8;
constexpr float LaneSpacing = 2000;

struct FProbe
{
	FString Role;
	FString Output;
	bool bReadArguments = false;
	bool bFinished = false;
	bool bFired = false;
	bool bRendered = false;
	bool bListen = false;
	bool bRecord = false;
	int32 CaptureFrame = 0;
	float LastCaptureAt = -1;
	TWeakObjectPtr<ACameraActor> ObserverCamera;
	int32 Case = -1;
	float StartedAt = 0;
	float FiredAt = 0;
	float ClientsReadyAt = -1;
	float ConnectedAt = -1;
	TWeakObjectPtr<UWorld> ActiveWorld;
	TWeakObjectPtr<AShooterCharacter> Shooter;
	TWeakObjectPtr<ARangedWeaponBase> Weapon;
	TArray<TWeakObjectPtr<APlayerController>> Controllers;
	TSet<TWeakObjectPtr<ARangedWeaponBase>> SeenWeapons;
	uint32 LastCorrectionSequence = 0;
	// Probe는 UObject가 아니므로 raw 클래스 참조를 GC가 추적하지 않는다.
	// Rifle 첫 case까지 긴 지연/손실 검증이 진행돼도 아직 사용하지 않은 BP 클래스를 보존한다.
	TStrongObjectPtr<UClass> ShooterClass;
	TStrongObjectPtr<UClass> WeaponClasses[2];

	bool Enabled()
	{
		if (!bReadArguments)
		{
			bReadArguments = true;
			FParse::Value(FCommandLine::Get(), TEXT("OutlierWallShotProbe="), Role);
			FParse::Value(FCommandLine::Get(), TEXT("OutlierWallShotOutput="), Output);
			bRendered = FParse::Param(FCommandLine::Get(), TEXT("OutlierWallShotRendered"));
			bListen = FParse::Param(FCommandLine::Get(), TEXT("WallShotListen"));
			bRecord = bRendered && FParse::Param(FCommandLine::Get(), TEXT("OutlierWallShotRecord"))
				&& !FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
		}
		return !Output.IsEmpty() && (Role == TEXT("Server") || Role == TEXT("Owner") || Role == TEXT("Observer"));
	}

	TSharedRef<FJsonObject> Row(const TCHAR* Event, UWorld* World, int32 Index) const
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("event"), Event);
		Result->SetStringField(TEXT("role"), Role);
		Result->SetBoolField(TEXT("listen"), bListen);
		Result->SetBoolField(TEXT("rendered"), bRendered);
		Result->SetBoolField(TEXT("recording"), bRecord);
		Result->SetNumberField(TEXT("case"), Index);
		Result->SetStringField(TEXT("caseName"), *FString::Printf(TEXT("%s_%s"), Index < 4 ? TEXT("Pistol") : TEXT("Rifle"),
			Index % 4 == 0 ? TEXT("Open") : Index % 4 == 1 ? TEXT("Front") : Index % 4 == 2 ? TEXT("Side") : TEXT("Inside")));
		Result->SetNumberField(TEXT("worldTime"), World->GetTimeSeconds());
		Result->SetNumberField(TEXT("netMode"), int32(World->GetNetMode()));
		return Result;
	}

	void Write(const TSharedRef<FJsonObject>& Object) const
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		FString Text;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		Text += LINE_TERMINATOR;
		FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	void Error(UWorld* World, const TCHAR* Detail)
	{
		auto Object = Row(TEXT("Error"), World, Case);
		Object->SetStringField(TEXT("detail"), Detail);
		Write(Object);
		bFinished = true;
		UE_LOG(LogTemp, Error, TEXT("[WallShotProbe] %s"), Detail);
		FPlatformMisc::RequestExit(false);
	}

	static void Vector(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, const FVector& Value)
	{
		Object->SetArrayField(Key, {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y), MakeShared<FJsonValueNumber>(Value.Z)});
	}

	void Initialize(UWorld* World)
	{
		ActiveWorld = World;
		if (bRendered)
		{
			// Entry 맵에는 조명이 없으므로 캡처 전용 광원만 추가한다.
			AActor* LightActor = World->SpawnActor<AActor>();
			UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(LightActor);
			LightActor->SetRootComponent(Light);
			Light->SetIntensity(5);
			Light->RegisterComponent();
			Light->SetWorldRotation(FRotator(-35, 0, 0));
		}
		// 각 프로세스에 같은 통제 벽을 만든다. 게임 레벨/에셋은 저장하거나 수정하지 않는다.
		UStaticMesh* Cube = bRendered ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")) : nullptr;
		for (int32 Index = 0; Index < CaseCount; ++Index)
		{
			AActor* WallActor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(WallActor);
			WallActor->SetRootComponent(Box);
			const bool bSide = Index % 4 == 2;
			const FVector Extent = bSide ? FVector(150, 2, 500) : FVector(2, 150, 500);
			// BP 카메라는 캡슐보다 약 9cm 앞에 있다. 근접 벽은 실제 시점 앞에 놓아
			// 이미 벽을 지난 카메라를 "총구 차폐" 사례로 잘못 검사하지 않는다.
			const FVector Location(Index % 4 == 3 ? 15 : 45, Index * LaneSpacing + (bSide ? 35 : 0), 1064);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->SetCollisionEnabled(Index % 4 == 0 ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
			Box->RegisterComponent();
			Box->SetWorldLocation(Location);
			if (Cube && Index % 4 != 0)
			{
				UStaticMeshComponent* Visible = NewObject<UStaticMeshComponent>(WallActor);
				Visible->SetupAttachment(Box);
				Visible->SetStaticMesh(Cube);
				Visible->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Visible->SetRelativeScale3D(Extent / 50);
				Visible->RegisterComponent();
			}
		}
		if (Role == TEXT("Server"))
		{
			ShooterClass.Reset(LoadClass<AShooterCharacter>(nullptr, TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C")));
			WeaponClasses[0].Reset(LoadClass<ARangedWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C")));
			WeaponClasses[1].Reset(LoadClass<ARangedWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C")));
			if (!ShooterClass.IsValid() || !WeaponClasses[0].IsValid() || !WeaponClasses[1].IsValid()) { Error(World, TEXT("Production class load failed")); return; }
		}
		Write(Row(TEXT("Initialized"), World, -1));
	}

	void StartCase(UWorld* World)
	{
		if (Controllers.Num() != 2 || !Controllers[0].IsValid() || !Controllers[1].IsValid())
		{
			Error(World, TEXT("Case controller disconnected"));
			return;
		}
		if (Weapon.IsValid()) Weapon->Destroy();
		if (Shooter.IsValid()) Shooter->Destroy();
		++Case;
		StartedAt = World->GetTimeSeconds();
		bFired = false;
		ClientsReadyAt = -1;
		if (Case >= CaseCount)
		{
			Write(Row(TEXT("Complete"), World, Case));
			bFinished = true;
			FPlatformMisc::RequestExit(false);
			return;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AShooterCharacter* NewShooter = World->SpawnActor<AShooterCharacter>(ShooterClass.Get(),
			FVector(-160, Case * LaneSpacing, 1000), FRotator::ZeroRotator, Spawn);
		ARangedWeaponBase* NewWeapon = World->SpawnActor<ARangedWeaponBase>(WeaponClasses[Case / 4].Get(), FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		if (!NewShooter || !NewWeapon) { Error(World, TEXT("Production spawn failed")); return; }
		Shooter = NewShooter;
		Weapon = NewWeapon;
		NewShooter->bAlwaysRelevant = true;
		NewWeapon->bAlwaysRelevant = true;
		NewShooter->GetCharacterMovement()->DisableMovement();
		Controllers[0]->Possess(NewShooter);
		NewShooter->SetSuitPresentation(true);
		NewShooter->AFirstPersonCharacter::EquipWeapon(NewWeapon);
		NewShooter->RefreshWeaponMode();
		FWallShotProbeAccess::ConfigureControlledShot(NewWeapon);
		if (bListen)
		{
			NewShooter->GetFirstPersonMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			NewWeapon->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			auto Ready = Row(TEXT("Ready"), World, Case);
			Ready->SetBoolField(TEXT("local"), NewShooter->IsLocallyControlled());
			Ready->SetBoolField(TEXT("authority"), NewShooter->HasAuthority());
			Ready->SetNumberField(TEXT("remoteConnections"), World->GetNetDriver()->ClientConnections.Num());
			Write(Ready);
		}
		for (const auto& Controller : Controllers) if (Controller.IsValid()) Controller->ClientSetViewTarget(NewShooter);
		NewShooter->ForceNetUpdate();
		NewWeapon->ForceNetUpdate();
		Write(Row(TEXT("CaseStarted"), World, Case));
	}

	bool ClientsReady() const
	{
		// 로컬 다중 프로세스 검증의 시작 장벽이다. 게임 RPC/복제 규칙은 바꾸지 않는다.
		const FString Root = FPaths::GetPath(FPaths::GetPath(Output));
		const FString Marker = FString::Printf(TEXT("Ready_%d.txt"), Case);
		return (bListen || IFileManager::Get().FileExists(*(Root / TEXT("Owner") / Marker)))
			&& IFileManager::Get().FileExists(*(Root / TEXT("Observer") / Marker));
	}

	void RecordFrame(UWorld* World)
	{
		if (!bRecord || World->GetNetMode() == NM_DedicatedServer || !Shooter.IsValid() || !Weapon.IsValid()) return;
		if (LastCaptureAt >= 0 && World->GetTimeSeconds() - LastCaptureAt < 0.1f) return;
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->IsLocalController() || (Role == TEXT("Observer") && !ObserverCamera.IsValid())) return;
		const int32 Index = FMath::RoundToInt(Shooter->GetActorLocation().Y / LaneSpacing);
		if (Index < 0 || Index >= CaseCount) return;
		LastCaptureAt = World->GetTimeSeconds();
		const FString Path = FPaths::GetPath(Output) / FString::Printf(TEXT("frame_%06d.png"), CaptureFrame);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		auto Object = Row(TEXT("CaptureFrame"), World, Index);
		Object->SetNumberField(TEXT("frame"), CaptureFrame++);
		Object->SetStringField(TEXT("captureView"), Shooter->IsLocallyControlled() ? TEXT("FP") : TEXT("TP"));
		Object->SetStringField(TEXT("screenshotRequested"), Path);
		Object->SetStringField(TEXT("visualReview"), TEXT("Pending: recording request is not W11 verification"));
		Write(Object);
	}

	void Tick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
	{
		if (!Enabled() || bFinished || !World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
		if (Role == TEXT("Server") ? World->GetNetMode() != (bListen ? NM_ListenServer : NM_DedicatedServer) : World->GetNetMode() != NM_Client) return;
		if (!ActiveWorld.IsValid()) Initialize(World);
		if (ActiveWorld.Get() != World || bFinished) return;
		if (Role == TEXT("Server"))
		{
			if (Controllers.Num() == 2 && (!Controllers[0].IsValid() || !Controllers[1].IsValid()))
			{
				Error(World, TEXT("Probe controller disconnected"));
				return;
			}
			if (Controllers.Num() != 2)
			{
				if (!World->GetNetDriver() || World->GetNetDriver()->ClientConnections.Num() < (bListen ? 1 : 2)) return;
				Controllers.Reset();
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
					if (It->IsValid() && (It->Get()->GetNetConnection() || (bListen && It->Get()->IsLocalController())))
					{
						if (bListen && It->Get()->IsLocalController()) Controllers.Insert(*It, 0);
						else Controllers.Add(*It);
					}
				if (Controllers.Num() != 2) return;
				ConnectedAt = World->GetTimeSeconds();
			}
			if (Case < 0) { if (World->GetTimeSeconds() - ConnectedAt > 2) StartCase(World); return; }
			const float Elapsed = World->GetTimeSeconds() - StartedAt;
			if (bFired && World->GetTimeSeconds() - FiredAt > 2) { StartCase(World); return; }
			if (!Shooter.IsValid() || !Weapon.IsValid()) { Error(World, TEXT("Case actor disappeared")); return; }
			// 접근 -> 정지/발사 -> 이탈. 서버의 실제 movement replication을 통해 소유자에게 전달한다.
			const float SinceFire = World->GetTimeSeconds() - FiredAt;
			const float X = Elapsed < 1.5f ? FMath::Lerp(-160.0f, 0.0f, Elapsed / 1.5f)
				: bFired && SinceFire > 1 ? FMath::Lerp(0.0f, -160.0f, SinceFire - 1) : 0;
			Shooter->SetActorLocation(FVector(X, Case * LaneSpacing, 1000), false, nullptr, ETeleportType::TeleportPhysics);
			Controllers[0]->SetControlRotation(FRotator::ZeroRotator);
			Controllers[0]->ServerUpdateCamera_Implementation(Shooter->GetPawnViewLocation(), 0);
			if (!bFired && ClientsReadyAt < 0 && ClientsReady()) ClientsReadyAt = World->GetTimeSeconds();
			// 클라이언트 생성 확인 뒤 채널 ACK와 렌더 프레임이 진행될 여유를 둔다.
			if (!bFired && Elapsed > 3 && ClientsReadyAt >= 0 && World->GetTimeSeconds() - ClientsReadyAt > 1)
			{
				bFired = true;
				FiredAt = World->GetTimeSeconds();
				const int32 Before = Weapon->GetCurrentAmmo();
				FWallShotProbeAccess::AttackOnce(Weapon.Get());
				auto Object = Row(TEXT("Cost"), World, Case);
				Object->SetNumberField(TEXT("ammoBefore"), Before);
				Object->SetNumberField(TEXT("ammoAfter"), Weapon->GetCurrentAmmo());
				Write(Object);
			}
		}
		else
		{
			// 이전 ClientSetViewTarget RPC가 늦게 도착해도 관전자 캡처는 TP 카메라를 유지한다.
			if (bRendered && Role == TEXT("Observer") && ObserverCamera.IsValid())
				if (APlayerController* PC = World->GetFirstPlayerController())
					if (PC->GetViewTarget() != ObserverCamera.Get()) PC->SetViewTarget(ObserverCamera.Get());
			if (Role == TEXT("Owner") && Shooter.IsValid()
				&& Shooter->GetLastWallCorrectionSequence() != LastCorrectionSequence)
			{
				LastCorrectionSequence = Shooter->GetLastWallCorrectionSequence();
				if (LastCorrectionSequence > 0)
				{
					const int32 Index = FMath::RoundToInt(Shooter->GetActorLocation().Y / LaneSpacing);
					auto Object = Row(TEXT("Correction"), World, Index);
					Object->SetNumberField(TEXT("sequence"), LastCorrectionSequence);
					Object->SetNumberField(TEXT("serverTime"), Shooter->GetLastWallCorrectionTime());
					Object->SetNumberField(TEXT("equipmentRevision"), Shooter->GetWallShotEquipmentRevision());
					Object->SetNumberField(TEXT("wallAlpha"), Shooter->GetWeaponWallAvoidanceState().GetShotBlendAlpha());
					Object->SetBoolField(TEXT("authority"), Shooter->HasAuthority());
					Object->SetBoolField(TEXT("local"), Shooter->IsLocallyControlled());
					Write(Object);
				}
			}
			// 조회는 테스트에서 새 무기를 찾을 때만 사용한다. 찾은 에셋의 총구/pose를 바꾸지는 않는다.
			for (TActorIterator<ARangedWeaponBase> It(World); It; ++It)
			{
				ARangedWeaponBase* Candidate = *It;
				AShooterCharacter* Owner = Cast<AShooterCharacter>(Candidate->GetOwner());
				if (!Owner || Owner->GetCurrentWeapon() != Candidate || SeenWeapons.Contains(Candidate)) continue;
				if (Role == TEXT("Owner") && !Owner->IsLocallyControlled()) continue;
				SeenWeapons.Add(Candidate);
				Shooter = Owner;
				Weapon = Candidate;
				if (bRendered && Role == TEXT("Observer"))
				{
					if (ObserverCamera.IsValid()) ObserverCamera->Destroy();
					const FVector Focus(0, Owner->GetActorLocation().Y, 1064);
					const FVector CameraLocation = Focus + FVector(-220, -260, 110);
					ObserverCamera = World->SpawnActor<ACameraActor>(CameraLocation, (Focus - CameraLocation).Rotation());
					if (APlayerController* PC = World->GetFirstPlayerController())
						if (ObserverCamera.IsValid()) PC->SetViewTarget(ObserverCamera.Get());
				}
				if (Role == TEXT("Owner"))
				{
					Shooter = Owner;
					LastCorrectionSequence = 0;
					Owner->GetFirstPersonMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
					Candidate->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				auto Object = Row(TEXT("Ready"), World, FMath::RoundToInt(Owner->GetActorLocation().Y / LaneSpacing));
				Object->SetBoolField(TEXT("local"), Owner->IsLocallyControlled());
				Object->SetBoolField(TEXT("authority"), Owner->HasAuthority());
				Write(Object);
				FFileHelper::SaveStringToFile(TEXT("Ready"), *(FPaths::GetPath(Output)
					/ FString::Printf(TEXT("Ready_%d.txt"), FMath::RoundToInt(Owner->GetActorLocation().Y / LaneSpacing))));
			}
		}
		RecordFrame(World);
	}

	void Observe(const FWallShotObservation& Shot)
	{
		if (!Enabled() || !Shot.Weapon || Shot.Weapon->GetWorld() != ActiveWorld.Get()) return;
		if (Shot.bPresentation ? (Role == TEXT("Server") && !bListen) : Role != TEXT("Server")) return;
		AShooterCharacter* Owner = Cast<AShooterCharacter>(Shot.Weapon->GetOwner());
		if (!Owner || (Role == TEXT("Server") && Shot.bPresentation && !Owner->IsLocallyControlled())) return;
		const int32 Index = FMath::RoundToInt(Owner->GetActorLocation().Y / LaneSpacing);
		if (Index < 0 || Index >= CaseCount) return;
		auto Object = Row(Shot.bPresentation ? TEXT("Presentation") : TEXT("Shot"), Owner->GetWorld(), Index);
		// 수신 순서 대신 서버가 확정한 발사 식별로 연결한다. 유실된 탄이 다음 탄과 섞이지 않는다.
		Object->SetNumberField(TEXT("shot"), Shot.ShotId);
		Object->SetNumberField(TEXT("equipmentRevision"), Shot.EquipmentRevision);
		Object->SetNumberField(TEXT("serverTime"), Shot.ServerTime);
		Object->SetBoolField(TEXT("occluded"), Shot.bOccluded);
		Object->SetBoolField(TEXT("worldEffects"), Shot.bWorldEffects);
		Object->SetBoolField(TEXT("authority"), Owner->HasAuthority());
		Object->SetBoolField(TEXT("local"), Owner->IsLocallyControlled());
		Object->SetBoolField(TEXT("hit"), Shot.bHit);
		Object->SetStringField(TEXT("weaponClass"), Shot.Weapon->GetClass()->GetPathName());
		Vector(Object, TEXT("ownerLocation"), Owner->GetActorLocation());
		if (APlayerController* PC = Cast<APlayerController>(Owner->GetController()))
		{
			FVector View; FRotator Rotation;
			PC->GetPlayerViewPoint(View, Rotation);
			Vector(Object, TEXT("viewLocation"), View);
		}
		Vector(Object, TEXT("end"), Shot.End);
		Vector(Object, TEXT("start"), Shot.Start);
		Vector(Object, TEXT("direction"), Shot.Direction);
		Vector(Object, TEXT("baseDirection"), Shot.BaseDirection);
		Object->SetNumberField(TEXT("currentWallAlpha"), Owner->GetWeaponWallAvoidanceState().GetShotBlendAlpha());
		if (!Shot.bPresentation)
		{
			Object->SetStringField(TEXT("hitActor"), Shot.HitActor);
			Object->SetStringField(TEXT("hitComponent"), Shot.HitComponent);
			Object->SetBoolField(TEXT("startPenetrating"), Shot.bStartPenetrating);
			const bool bBlockedCase = Index % 4 == 1 || Index % 4 == 3;
			if (bBlockedCase)
			{
				auto Requirement = MakeShared<FJsonObject>();
				Requirement->SetStringField(TEXT("id"), TEXT("W05"));
				Requirement->SetBoolField(TEXT("passed"), Shot.bHit && Shot.End.X <= (Index % 4 == 3 ? 17.1 : 47.1));
				Requirement->SetStringField(TEXT("detail"), TEXT("Controlled front/inside wall must terminate this shot"));
				Object->SetArrayField(TEXT("requirements"), {MakeShared<FJsonValueObject>(Requirement)});
			}
		}
		else
		{
			const bool bFP = Owner->IsLocallyControlled();
			if (bFP)
			{
				Object->SetNumberField(TEXT("lastCorrectionTime"), Owner->GetLastWallCorrectionTime());
				Object->SetNumberField(TEXT("lastCorrectionSequence"), Owner->GetLastWallCorrectionSequence());
				Object->SetNumberField(TEXT("currentEquipmentRevision"), Owner->GetWallShotEquipmentRevision());
				FTransform Prediction;
				const bool bHasPrediction = Owner->TryGetWallShotTransform(Prediction);
				Object->SetBoolField(TEXT("hasCurrentPrediction"), bHasPrediction);
				if (bHasPrediction)
				{
					// 도착 시점의 예측과 과거 서버 발사는 같은 시각이 아니다. 오차는 진단용으로만 남긴다.
					Vector(Object, TEXT("currentPredictedStart"), Prediction.GetLocation());
					Vector(Object, TEXT("currentPredictedDirection"), Prediction.GetUnitAxis(EAxis::X));
					Object->SetNumberField(TEXT("arrivalPredictionDistanceCm"), FVector::Distance(Prediction.GetLocation(), Shot.Start));
					const double Dot = FMath::Clamp(FVector::DotProduct(Prediction.GetUnitAxis(EAxis::X), Shot.BaseDirection.GetSafeNormal()), -1.0, 1.0);
					Object->SetNumberField(TEXT("arrivalPredictionAngleDegrees"), FMath::RadiansToDegrees(FMath::Acos(Dot)));
				}
			}
			USkeletalMeshComponent* Mesh = bFP ? Shot.Weapon->GetFirstPersonWeaponMesh() : Shot.Weapon->GetThirdPersonWeaponMesh();
			const FName Socket = Shot.Socket.IsNone() ? Shot.Weapon->GetMuzzleSocketName() : Shot.Socket;
			Object->SetBoolField(TEXT("firstPerson"), bFP);
			Object->SetBoolField(TEXT("socketExists"), Mesh && Mesh->DoesSocketExist(Socket));
			if (Mesh && Mesh->DoesSocketExist(Socket))
			{
				const FTransform Muzzle = Mesh->GetSocketTransform(Socket, RTS_World);
				Vector(Object, TEXT("localMuzzle"), Muzzle.GetLocation());
				Vector(Object, TEXT("localForward"), Muzzle.GetUnitAxis(EAxis::X));
				if (bRendered)
				{
					APlayerController* PC = Owner->GetWorld()->GetFirstPlayerController();
					if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
					{
						// FP FOV/Scale은 표시 좌표에만 적용한다. 서버 판정 선분은 월드 좌표로 투영한다.
						const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
						const FVector DisplayMuzzle = bFP && View.bUseFirstPersonParameters
							? View.TransformWorldToFirstPerson(Muzzle.GetLocation(), false) : Muzzle.GetLocation();
						FVector2D MuzzlePixel = FVector2D::ZeroVector, EndPixel = FVector2D::ZeroVector;
						Object->SetBoolField(TEXT("muzzleInFront"), PC->ProjectWorldLocationToScreen(DisplayMuzzle, MuzzlePixel));
						Object->SetBoolField(TEXT("endInFront"), PC->ProjectWorldLocationToScreen(Shot.End, EndPixel));
						Vector(Object, TEXT("muzzlePixel"), FVector(MuzzlePixel.X, MuzzlePixel.Y, 0));
						Vector(Object, TEXT("endPixel"), FVector(EndPixel.X, EndPixel.Y, 0));
					}
				}
			}
			if (UAnimInstance* Anim = Owner->GetFirstPersonMesh()->GetAnimInstance())
			{
				if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(Anim->GetClass(), TEXT("WallOffsetAlpha")))
					Object->SetNumberField(TEXT("wallAlpha"), Property->GetPropertyValue_InContainer(Anim));
			}
			if (bRendered && !bRecord)
			{
				const FString Path = FPaths::GetPath(Output) / FString::Printf(TEXT("case_%d.png"), Index);
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				Object->SetStringField(TEXT("screenshotRequested"), Path);
				Object->SetStringField(TEXT("captureView"), bFP ? TEXT("FP") : TEXT("TP"));
				Object->SetStringField(TEXT("visualReview"), TEXT("Pending: screenshot request is not W11 verification"));
			}
		}
		Write(Object);
	}
};

FProbe Probe;
struct FRegistration
{
	FRegistration()
	{
		FWorldDelegates::OnWorldPostActorTick.AddRaw(&Probe, &FProbe::Tick);
		WallShotProbe::Observed().AddRaw(&Probe, &FProbe::Observe);
	}
} Registration;
}
#endif
