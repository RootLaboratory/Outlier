#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SceneView.h"

namespace
{
constexpr float ViewportProbeStep = 1.0f / 60.0f;

struct FMeleeViewportWorld
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	APlayerController* Controller = nullptr;
	AShooterCharacter* Shooter = nullptr;
	AMeleeWeaponBase* Hammer = nullptr;

	bool Initialize(FAutomationTestBase& Test, bool bSuit)
	{
		if (!GEngine) return false;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MeleeViewport")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Viewport probe world"), World)) return false;
		World->AddToRoot();
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();

		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		UClass* HammerClass = LoadClass<AMeleeWeaponBase>(nullptr,
			TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
		if (!Test.TestNotNull(TEXT("Production viewport Shooter BP"), ShooterClass)
			|| !Test.TestNotNull(TEXT("Production viewport Hammer BP"), HammerClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
		Hammer = World->SpawnActor<AMeleeWeaponBase>(HammerClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Local viewport Shooter"), Shooter)
			|| !Test.TestNotNull(TEXT("Local viewport Hammer"), Hammer)
			|| !Test.TestNotNull(TEXT("Local viewport controller"), Controller)) return false;
		Controller->Possess(Shooter);
		Controller->Player = NewObject<ULocalPlayer>(GEngine);
		Controller->SetAsLocalPlayerController();
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Controller->SetViewTarget(Shooter);
		if (Controller->PlayerCameraManager) Controller->PlayerCameraManager->bUseClientSideCameraUpdates = false;
		Shooter->GetCharacterMovement()->DisableMovement();
		if (!Test.TestTrue(TEXT("Viewport fixture uses a local standalone owner"),
			World->GetNetMode() == NM_Standalone && Shooter->IsLocallyControlled())) return false;
		if (!Test.TestTrue(TEXT("Viewport fixture applies production presentation"), Shooter->SetSuitPresentation(bSuit))) return false;
		USkeletalMeshComponent* Arms = Shooter->GetFirstPersonMesh();
		if (Arms && !Arms->GetAnimInstance()) Arms->InitAnim(true);
		if (!Test.TestNotNull(TEXT("Production FP arms animation instance"), Arms ? Arms->GetAnimInstance() : nullptr)) return false;
		Shooter->AFirstPersonCharacter::EquipWeapon(Hammer);
		Shooter->RefreshWeaponMode();
		// headless 검증에서 렌더 가시성에 의한 pose 생략만 해제한다. 무기/손 위치나 애니메이션 시간을 강제하지 않는다.
		Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Hammer->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Shooter->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		for (int32 Frame = 0; Frame < 60; ++Frame) Tick();
		return true;
	}

	void Tick() const
	{
		++GFrameCounter;
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Controller->UpdateCameraManager(ViewportProbeStep);
		World->Tick(LEVELTICK_All, ViewportProbeStep);
	}

	~FMeleeViewportWorld()
	{
		if (!World) return;
		if (Controller) Controller->Player = nullptr;
		GEngine->ShutdownWorldNetDriver(World);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};

struct FProjectedMeleeSocket
{
	bool bInFront = false;
	FVector2D Normalized = FVector2D::ZeroVector;
};

FProjectedMeleeSocket ProjectMeleeSocket(const FMinimalViewInfo& CameraView,
	EAspectRatioAxisConstraint AxisConstraint, const FIntPoint& Size, const FVector& SocketLocation)
{
	FMinimalViewInfo View = CameraView;
	const FIntRect ViewRect(0, 0, Size.X, Size.Y);
	FIntRect ConstrainedRect = ViewRect;
	if (View.bConstrainAspectRatio && View.AspectRatio > 0)
	{
		const int32 Width = FMath::Min(Size.X, FMath::RoundToInt(Size.Y * View.AspectRatio));
		const int32 Height = FMath::Min(Size.Y, FMath::RoundToInt(Size.X / View.AspectRatio));
		ConstrainedRect = FIntRect((Size.X - Width) / 2, (Size.Y - Height) / 2,
			(Size.X + Width) / 2, (Size.Y + Height) / 2);
	}
	FSceneViewProjectionData Projection;
	Projection.SetViewRectangle(ViewRect);
	Projection.ViewOrigin = View.Location;
	Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(
		FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, AxisConstraint, ConstrainedRect, Projection);
	// 렌더러와 같은 FP FOV/Scale 보정을 실제 월드 소켓에 적용한다. TP 월드 좌표는 건드리지 않는다.
	const FVector RenderPosition = View.bUseFirstPersonParameters
		? View.TransformWorldToFirstPerson(SocketLocation, false) : SocketLocation;
	FVector2D Pixel = FVector2D::ZeroVector;
	FProjectedMeleeSocket Result;
	Result.bInFront = FSceneView::ProjectWorldToScreen(RenderPosition,
		Projection.GetConstrainedViewRect(), Projection.ComputeViewProjectionMatrix(), Pixel);
	Result.Normalized = FVector2D(Pixel.X / Size.X - 0.5, Pixel.Y / Size.Y - 0.5);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeViewportProjectionTest,
	"Outlier.Weapon.Melee.ViewportProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeViewportProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	const EAspectRatioAxisConstraint AxisConstraint = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;
	const TArray<FIntPoint> Sizes = { FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(1024, 768), FIntPoint(2560, 1080) };
	TArray<FString> Rows;
	Rows.Add(TEXT("suit,frame,phase,montage_time,width,height,start_in_front,start_x,start_y,end_in_front,end_x,end_y"));
	AddInfo(TEXT("[MeleeViewport] Headless engine projection probe; no rendered fullscreen/window-mode verification is claimed."));
	for (const TCHAR* Name : { TEXT("outlier.FPViewModelEyeAlign"), TEXT("outlier.FPViewModelOffsetX"),
		TEXT("outlier.FPViewModelOffsetY"), TEXT("outlier.FPViewModelOffsetZ"), TEXT("outlier.FPAnimForceProceduralOff") })
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		AddInfo(FString::Printf(TEXT("[MeleeViewport] CVar=%s Value=%s"), Name, Variable ? *Variable->GetString() : TEXT("Unregistered")));
	}
	for (bool bSuit : { false, true })
	{
		FMeleeViewportWorld Fixture;
		if (!Fixture.Initialize(*this, bSuit)) return false;
		USkeletalMeshComponent* WeaponMesh = Fixture.Hammer->GetFirstPersonWeaponMesh();
		UAnimInstance* ArmsAnim = Fixture.Shooter->GetFirstPersonMesh()->GetAnimInstance();
		UAnimMontage* Montage = Fixture.Shooter->GetFirstPersonMeleeAttackMontage();
		UCameraComponent* Camera = Fixture.Shooter->GetFirstPersonCameraComponent();
		if (!TestNotNull(TEXT("Production FP melee montage"), Montage)
			|| !TestNotNull(TEXT("Production FP camera"), Camera)) return false;
		if (!TestTrue(TEXT("Actual FP hammer has trace start socket"), WeaponMesh->DoesSocketExist(TEXT("MeleeTraceStart")))
			|| !TestTrue(TEXT("Actual FP hammer has trace end socket"), WeaponMesh->DoesSocketExist(TEXT("MeleeTraceEnd"))))
		{
			AddError(FString::Printf(TEXT("FP socket probe cannot identify the hammer trace points: %s"), *GetPathNameSafe(WeaponMesh->GetSkeletalMeshAsset())));
			return false;
		}
		Fixture.Hammer->StartAttack();
		Fixture.Hammer->ReleaseAttack();
		if (!TestTrue(TEXT("Actual local attack plays its FP montage"), ArmsAnim->Montage_IsPlaying(Montage))) return false;
		int32 ProjectedFrames = 0;
		const int32 MaxFrames = FMath::CeilToInt((Montage->GetPlayLength() + 1.0f) * 60);
		for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
		{
			Fixture.Tick();
			FMinimalViewInfo View;
			Camera->GetCameraView(ViewportProbeStep, View);
			if (Frame == 0)
			{
				AddInfo(FString::Printf(TEXT("[MeleeViewport] Suit=%d WorldFOV=%.3f FPFOV=%.3f FPScale=%.3f UseFP=%d Aspect=%.5f Constrain=%d Axis=%d AxisOverride=%d Camera=%s Arms=%s Weapon=%s Montage=%s"),
					bSuit, View.FOV, View.FirstPersonFOV, View.FirstPersonScale, View.bUseFirstPersonParameters,
					View.AspectRatio, View.bConstrainAspectRatio, static_cast<int32>(AxisConstraint),
					View.AspectRatioAxisConstraint.IsSet(), *View.Location.ToString(),
					*GetPathNameSafe(Fixture.Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset()),
					*GetPathNameSafe(WeaponMesh->GetSkeletalMeshAsset()), *GetPathNameSafe(Montage)));
			}
			WeaponMesh->UpdateComponentToWorld();
			const FVector Start = WeaponMesh->GetSocketLocation(TEXT("MeleeTraceStart"));
			const FVector End = WeaponMesh->GetSocketLocation(TEXT("MeleeTraceEnd"));
			FProjectedMeleeSocket FirstStart, FirstEnd;
			for (int32 SizeIndex = 0; SizeIndex < Sizes.Num(); ++SizeIndex)
			{
				const FProjectedMeleeSocket PStart = ProjectMeleeSocket(View, AxisConstraint, Sizes[SizeIndex], Start);
				const FProjectedMeleeSocket PEnd = ProjectMeleeSocket(View, AxisConstraint, Sizes[SizeIndex], End);
				if (SizeIndex == 0) { FirstStart = PStart; FirstEnd = PEnd; }
				if (SizeIndex == 1)
				{
					TestEqual(TEXT("Same-aspect resolution preserves FP start visibility"), PStart.bInFront, FirstStart.bInFront);
					TestEqual(TEXT("Same-aspect resolution preserves FP end visibility"), PEnd.bInFront, FirstEnd.bInFront);
					if (PStart.bInFront && PEnd.bInFront)
					{
						++ProjectedFrames;
						TestTrue(TEXT("Same-aspect resolution preserves normalized FP trace points"),
							PStart.Normalized.Equals(FirstStart.Normalized, 0.00001)
							&& PEnd.Normalized.Equals(FirstEnd.Normalized, 0.00001));
					}
				}
				Rows.Add(FString::Printf(TEXT("%d,%d,%d,%.6f,%d,%d,%d,%.6f,%.6f,%d,%.6f,%.6f"),
					bSuit, Frame, static_cast<int32>(Fixture.Hammer->GetAttackPhase()), ArmsAnim->Montage_GetPosition(Montage),
					Sizes[SizeIndex].X, Sizes[SizeIndex].Y, PStart.bInFront, PStart.Normalized.X, PStart.Normalized.Y,
					PEnd.bInFront, PEnd.Normalized.X, PEnd.Normalized.Y));
			}
			if (!ArmsAnim->Montage_IsPlaying(Montage)) break;
		}
		TestTrue(TEXT("Real FP swing supplies visible points for resolution comparison"), ProjectedFrames > 0);
		AddInfo(FString::Printf(TEXT("[MeleeViewport] Suit=%d SameAspectComparedFrames=%d"), bSuit, ProjectedFrames));
	}
	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("MeleeViewportProjection");
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("ViewportProjection.csv");
	TestTrue(TEXT("Viewport diagnostic CSV is written"), FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")), *OutputPath));
	AddInfo(FString::Printf(TEXT("[MeleeViewport] Samples=%s (normalized origin is screen center; positive X is right)"), *OutputPath));
	return !HasAnyErrors();
}

#endif
