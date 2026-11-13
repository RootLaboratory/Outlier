#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterCharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterFallingGravityTest,
	"Outlier.Shooter.Jump.FallingGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterFallingGravityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Shooter Blueprint"), ShooterClass)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Gravity test world"), World)) return false;
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	UShooterCharacterMovementComponent* Movement = Shooter
		? Cast<UShooterCharacterMovementComponent>(Shooter->GetCharacterMovement()) : nullptr;
	if (TestNotNull(TEXT("Shooter movement"), Movement))
	{
		AddInfo(FString::Printf(TEXT("Blueprint jump settings: GravityScale=%g JumpZVelocity=%g JumpMaxHoldTime=%g JumpMaxCount=%d"),
			Movement->GravityScale, Movement->JumpZVelocity, Shooter->JumpMaxHoldTime, Shooter->JumpMaxCount));
		Movement->SetMovementMode(MOVE_Falling);
		const FVector Gravity(0.0, 0.0, -980.0);
		const FVector Falling(120.0, -30.0, -100.0);
		const FVector Rising(120.0, -30.0, 400.0);
		const auto BaseVelocity = [Movement](const FVector& Initial, const FVector& Acceleration, float Delta)
		{
			return Movement->UCharacterMovementComponent::NewFallVelocity(Initial, Acceleration, Delta);
		};
		TestTrue(TEXT("Default descent applies 1.5 times gravity"),
			Movement->NewFallVelocity(Falling, Gravity, 0.1f).Equals(BaseVelocity(Falling, Gravity * 1.5, 0.1f)));
		TestTrue(TEXT("Ascent preserves original jump velocity integration"),
			Movement->NewFallVelocity(Rising, Gravity, 0.1f).Equals(BaseVelocity(Rising, Gravity, 0.1f)));
		TestTrue(TEXT("Apex immediately starts stronger descent"),
			Movement->NewFallVelocity(FVector::ZeroVector, Gravity, 0.1f).Equals(BaseVelocity(FVector::ZeroVector, Gravity * 1.5, 0.1f)));
		TestTrue(TEXT("No gravity still means no acceleration"),
			Movement->NewFallVelocity(Falling, FVector::ZeroVector, 0.1f).Equals(Falling));
		TestTrue(TEXT("Zero delta does not change velocity"),
			Movement->NewFallVelocity(Falling, Gravity, 0.0f).Equals(Falling));
		Movement->SetMovementMode(MOVE_Flying);
		TestTrue(TEXT("Non-falling modes retain base integration"),
			Movement->NewFallVelocity(Falling, Gravity, 0.1f).Equals(BaseVelocity(Falling, Gravity, 0.1f)));
		Movement->SetMovementMode(MOVE_Falling);
		FFloatProperty* Multiplier = FindFProperty<FFloatProperty>(Movement->GetClass(), TEXT("FallingGravityMultiplier"));
		if (TestNotNull(TEXT("Editable falling gravity multiplier"), Multiplier))
		{
			TestTrue(TEXT("Multiplier is editable in Details"), Multiplier->HasAnyPropertyFlags(CPF_Edit));
			for (float Value : {1.0f, 2.0f, 0.0f, -1.0f})
			{
				Multiplier->SetPropertyValue_InContainer(Movement, Value);
				TestTrue(FString::Printf(TEXT("Configured multiplier %g is applied with a minimum of one"), Value),
					Movement->NewFallVelocity(Falling, Gravity, 0.1f).Equals(BaseVelocity(Falling, Gravity * FMath::Max(1.0f, Value), 0.1f)));
			}
			Multiplier->SetPropertyValue_InContainer(Movement, 1.5f);
			TestTrue(TEXT("Second jump upward velocity immediately uses base gravity again"),
				Movement->NewFallVelocity(Rising, Gravity, 0.1f).Equals(BaseVelocity(Rising, Gravity, 0.1f)));
			const FVector SideGravity(-980.0, 0.0, 0.0);
			TestTrue(TEXT("Descent follows the supplied gravity direction"),
				Movement->NewFallVelocity(FVector(-100.0, 20.0, 0.0), SideGravity, 0.1f)
				.Equals(BaseVelocity(FVector(-100.0, 20.0, 0.0), SideGravity * 1.5, 0.1f)));
			const double Terminal = FMath::Abs(Movement->GetPhysicsVolume()->TerminalVelocity);
			TestTrue(TEXT("Stronger gravity respects terminal fall speed"),
				FMath::IsNearlyEqual(Movement->NewFallVelocity(FVector(0.0, 0.0, -Terminal), Gravity, 1.0f).Z, -Terminal));

			// 실제 PhysFalling의 정점 substep까지 통과시켜 상승 높이와 하강 시간의 변화를 비교한다.
			// 바닥 충돌 대신 출발 높이로 돌아오는 시간을 측정해 착지 애니메이션/지형과 분리한다.
			Movement->bRunPhysicsWithNoController = true;
			const auto SimulateJump = [&](float GravityMultiplier, float Step)
			{
				Multiplier->SetPropertyValue_InContainer(Movement, GravityMultiplier);
				Shooter->StopJumping();
				Shooter->SetActorLocation(FVector(0.0, 0.0, 5000.0));
				Movement->SetMovementMode(MOVE_Falling);
				Movement->Velocity = FVector(0.0, 0.0, Movement->JumpZVelocity);
				double MaxHeight = 0.0;
				float Elapsed = 0.0f;
				for (int32 Index = 0; Index < 600; ++Index)
				{
					Movement->PerformMovement(Step);
					Elapsed += Step;
					const double Height = Shooter->GetActorLocation().Z - 5000.0;
					MaxHeight = FMath::Max(MaxHeight, Height);
					if (Height <= 0.0 && Movement->Velocity.Z < 0.0) break;
				}
				return FVector2D(MaxHeight, Elapsed);
			};
			for (float Step : {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f})
			{
				const FVector2D Base = SimulateJump(1.0f, Step);
				const FVector2D FasterFall = SimulateJump(1.5f, Step);
				TestTrue(TEXT("Physics jump has a measurable ascent"), Base.X > 50.0);
				TestTrue(TEXT("Stronger descent preserves apex height within 1 cm"), FMath::Abs(Base.X - FasterFall.X) < 1.0);
				TestTrue(TEXT("Stronger descent returns to launch height sooner"), FasterFall.Y < Base.Y);
			}
		}
	}
	GEngine->ShutdownWorldNetDriver(World);
	World->DestroyWorld(true);
	World->SetPhysicsScene(nullptr);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

#endif
