#include "FirstPerson/PlayerRelocationEQS.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr float FloorGap = 2.0f;
constexpr float FloorProbeDistance = 6.0f;
constexpr float DistanceTieTolerance = 0.01f;

FCollisionQueryParams MakeParams(const FPlayerRelocationRequest& Request)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerRelocation), false, Request.MovingCharacter.Get());
	Params.AddIgnoredActor(Request.Anchor.Get());
	return Params;
}
}

UWorld* UPlayerRelocationQuery::GetWorld() const
{
	return QueryWorld.Get();
}

bool UPlayerRelocationQuery::Start(const FPlayerRelocationRequest& InRequest,
	TFunction<void(const FPlayerRelocationResult&)> InCompletion)
{
	if (Result.Status != EPlayerRelocationStatus::Idle) return false;
	Request = InRequest;
	Completion = MoveTemp(InCompletion);
	QueryWorld = Request.MovingCharacter.IsValid() ? Request.MovingCharacter->GetWorld() : nullptr;
	Result.Status = EPlayerRelocationStatus::Running;
	Deadline = FPlatformTime::Seconds() + Request.TimeoutSeconds;
	// 데이터 오류가 후보 폭증/무기한 대기로 이어지지 않도록 요청 진입에서 한도를 검증한다.
	// 대기 시간은 호출자가 소유한다. 슈트 연출의 남은 timeout도 허용하되 TimerManager의 float 범위를 넘기지 않는다.
	if (!IsContextValid() || !FMath::IsFinite(Request.TimeoutSeconds) || Request.TimeoutSeconds <= 0
		|| Request.TimeoutSeconds > TNumericLimits<float>::Max()
		|| !FMath::IsFinite(Request.GridSpacing) || Request.GridSpacing < 10
		|| !FMath::IsFinite(Request.InitialRadius) || !FMath::IsFinite(Request.ExpandedRadius)
		|| Request.InitialRadius < 0 || Request.ExpandedRadius < Request.InitialRadius || Request.ExpandedRadius > 10000
		|| !FMath::IsFinite(Request.InitialDepth) || !FMath::IsFinite(Request.ExpandedDepth)
		|| Request.InitialDepth <= 0 || Request.ExpandedDepth < Request.InitialDepth || Request.ExpandedDepth > 10000
		|| Request.MaxCandidatesPerStage <= 0 || Request.MaxCandidatesPerStage > 1024
		|| Request.MaxCollisionChecks <= 0 || Request.MaxCollisionChecks > 8192
		|| Request.PreferredLocalOffset.ContainsNaN()
		|| Request.ExplicitCandidates.Num() > Request.MaxCandidatesPerStage
		|| Request.ExplicitCandidates.ContainsByPredicate([](const FVector& Candidate) { return Candidate.ContainsNaN(); }))
	{
		Finish(EPlayerRelocationStatus::InvalidContext);
		return false;
	}

	// Cooked 실행의 native CDO는 GC 순회에서 제외된다. 런타임 객체를 CDO에만
	// 보관하면 요청 사이의 GC에서 회수되므로, 공유 불변 Template을 명시적으로 유지한다.
	// 요청마다 새로 만들지 않아 EQS 매니저의 Template 캐시도 증가하지 않는다.
	static TStrongObjectPtr<UEnvQuery> SharedTemplate;
	if (!SharedTemplate.IsValid())
	{
		SharedTemplate.Reset(NewObject<UEnvQuery>(GetTransientPackage()));
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(SharedTemplate.Get());
		Option->Generator = NewObject<UPlayerRelocationGenerator>(Option);
		Option->Tests.Add(NewObject<UPlayerRelocationSafetyTest>(Option));
		SharedTemplate->GetOptionsMutable().Add(Option);
	}
	QueryTemplate = SharedTemplate.Get();
	GetWorld()->GetTimerManager().SetTimer(TimeoutHandle, this, &ThisClass::OnTimeout,
		Request.TimeoutSeconds, false);
	// 0 최초 서기 → 1 최초 앉기 → 2 확장 서기 → 3 확장 앉기.
	// 앉기 의도는 1→3, 비행은 자세 구분 없이 0→2만 사용한다.
	Stage = Request.ExplicitCandidates.IsEmpty() && Request.bExpandedOnly ? 2 : 0;
	if (Request.Mode == EPlayerRelocationMode::Ground && Request.bWantsCrouch) ++Stage;
	BeginStage();
	return Result.Status == EPlayerRelocationStatus::Running;
}

bool UPlayerRelocationQuery::IsContextValid() const
{
	const ACharacter* Character = Request.MovingCharacter.Get();
	const AActor* Anchor = Request.Anchor.Get();
	return IsValid(GetWorld()) && IsValid(Character) && IsValid(Anchor) && Character != Anchor
		&& Character->HasAuthority() && !Character->IsActorBeingDestroyed() && !Anchor->IsActorBeingDestroyed()
		&& Character->GetWorld() == GetWorld() && Anchor->GetWorld() == GetWorld()
		&& Character->GetCapsuleComponent() && Character->GetCharacterMovement()
		&& Character->GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector)
		&& Request.Reference.IsValid() && (!Request.IsContextCurrent || Request.IsContextCurrent());
}

void UPlayerRelocationQuery::GetShape(float& Radius, float& HalfHeight) const
{
	const ACharacter* Character = Request.MovingCharacter.Get();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	Radius = Capsule->GetScaledCapsuleRadius();
	if (Request.Mode == EPlayerRelocationMode::Flight)
	{
		HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		return;
	}
	const float Scale = Capsule->GetShapeScale();
	const ACharacter* Defaults = Character->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeight = Result.Posture == EPlayerRelocationPosture::Crouched
		? Character->GetCharacterMovement()->GetCrouchedHalfHeight() * Scale
		: Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * Scale;
	HalfHeight = FMath::Max(Radius, HalfHeight);
}

bool UPlayerRelocationQuery::ConsumeCheck()
{
	if (FPlatformTime::Seconds() >= Deadline || Result.CollisionChecks >= Request.MaxCollisionChecks)
	{
		bBudgetExceeded = true;
		return false;
	}
	++Result.CollisionChecks;
	return true;
}

bool UPlayerRelocationQuery::IsWithinRange(const FVector& Location) const
{
	// 지정 fallback만 주변 반경을 벗어날 수 있다. 바닥/공간/가림/Room 검증을 생략하는 뜻은 아니다.
	if (!Request.ExplicitCandidates.IsEmpty()) return true;
	const FVector Reference = Request.Reference.GetLocation();
	const float Radius = Result.bExpanded ? Request.ExpandedRadius : Request.InitialRadius;
	if (FVector::DistSquared2D(Reference, Location) > FMath::Square(Radius + UE_KINDA_SMALL_NUMBER)) return false;
	return true;
}

bool UPlayerRelocationQuery::ProjectGround(FVector& Location)
{
	if (!ConsumeCheck()) return false;
	const ACharacter* Character = Request.MovingCharacter.Get();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Depth = Result.bExpanded || !Request.ExplicitCandidates.IsEmpty() ? Request.ExpandedDepth : Request.InitialDepth;
	FHitResult Hit;
	const FVector Start(Location.X, Location.Y, Request.Reference.GetLocation().Z);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, Depth),
		Capsule->GetCollisionObjectType(), MakeParams(Request), FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	if (!bHit || Hit.bStartPenetrating || !Character->GetCharacterMovement()->IsWalkable(Hit)) return false;
	float Radius, HalfHeight;
	GetShape(Radius, HalfHeight);
	// 경사면에서는 캡슐 밑 반구가 먼저 닿는다. 중심선 바닥 높이만 사용하면 정상 경사에도 파고든다.
	const float SlopeLift = Radius * (1.0f / Hit.ImpactNormal.Z - 1.0f);
	Location.Z = Hit.ImpactPoint.Z + HalfHeight + SlopeLift + FloorGap;
	return IsWithinRange(Location);
}

bool UPlayerRelocationQuery::Validate(const FVector& Location)
{
	if (!IsContextValid() || Location.ContainsNaN() || !IsWithinRange(Location)) return false;
	const ACharacter* Character = Request.MovingCharacter.Get();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	float Radius, HalfHeight;
	GetShape(Radius, HalfHeight);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FCollisionQueryParams Params = MakeParams(Request);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	const FQuat Rotation = Capsule->GetComponentQuat();
	const ACharacter* AnchorCharacter = Cast<ACharacter>(Request.Anchor.Get());
	if (AnchorCharacter)
	{
		const UCapsuleComponent* AnchorCapsule = AnchorCharacter->GetCapsuleComponent();
		// Pawn끼리 Ignore하더라도 서로 포개진 목적지는 허용하지 않는다.
		if (FVector::DistSquared2D(Location, AnchorCapsule->GetComponentLocation())
			< FMath::Square(Radius + AnchorCapsule->GetScaledCapsuleRadius())
			&& FMath::Abs(Location.Z - AnchorCapsule->GetComponentLocation().Z)
			< HalfHeight + AnchorCapsule->GetScaledCapsuleHalfHeight()) return false;
	}
	if (Request.AdditionalFilter && !Request.AdditionalFilter(Location, Radius, HalfHeight)) return false;
	if (!ConsumeCheck() || GetWorld()->OverlapBlockingTestByChannel(Location, Rotation, Channel, Shape, Params, Responses)) return false;
	FHitResult SightHit;
	if (!ConsumeCheck() || GetWorld()->LineTraceSingleByChannel(SightHit, Request.Reference.GetLocation(), Location,
		Channel, Params, Responses)) return false;
	if (Request.bRequireAnchorSweep)
	{
		if (!ConsumeCheck() || GetWorld()->OverlapBlockingTestByChannel(Request.Reference.GetLocation(), Rotation,
			Channel, Shape, Params, Responses)) return false;
		if (!ConsumeCheck() || GetWorld()->SweepTestByChannel(Request.Reference.GetLocation(), Location, Rotation,
			Channel, Shape, Params, Responses)) return false;
	}
	if (Request.Mode == EPlayerRelocationMode::Ground)
	{
		// 높이 제한은 캡슐 중심이 아닌 기준 XY의 실제 바닥으로 판정한다.
		FVector Projected = Location;
		if (!ProjectGround(Projected) || FMath::Abs(Projected.Z - Location.Z) > FloorGap) return false;
		// 점 Trace는 높이 투영만 담당한다. 실제 지지는 착지 자세의 캡슐로 다시 검사한다.
		FHitResult FloorHit;
		if (!ConsumeCheck() || !GetWorld()->SweepSingleByChannel(FloorHit, Location,
			Location - FVector(0, 0, FloorProbeDistance), Rotation, Channel, Shape, Params, Responses)
			|| FloorHit.bStartPenetrating || !Character->GetCharacterMovement()->IsWalkable(FloorHit)) return false;
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const float SupportRadius = FMath::Min(Radius - 0.15f, Movement->GetValidPerchRadius());
		if (SupportRadius <= 0 || FVector::DistSquared2D(FloorHit.Location, FloorHit.ImpactPoint)
			>= FMath::Square(SupportRadius)) return false;
	}
	return true;
}

void UPlayerRelocationQuery::Generate(FEnvQueryInstance& Query)
{
	if (!IsContextValid()) return;
	const FVector Reference = Request.Reference.GetLocation();
	const float Radius = Result.bExpanded ? Request.ExpandedRadius : Request.InitialRadius;
	const FRotator Yaw(0, Request.Reference.Rotator().Yaw, 0);
	int32 Count = 0;
	auto Add = [&](FVector Location)
	{
		if (!IsWithinRange(Location)) return;
		if (++Count > Request.MaxCandidatesPerStage || FPlatformTime::Seconds() >= Deadline)
		{
			bBudgetExceeded = true;
			return;
		}
		++Result.GeneratedCandidates;
		if (Request.Mode == EPlayerRelocationMode::Ground && !ProjectGround(Location)) return;
		Query.AddItemData<UEnvQueryItemType_Point>(Location);
	};
	if (!Request.ExplicitCandidates.IsEmpty())
	{
		// 출입구 순서를 보존한다. 가까운 입구의 서기/앉기를 끝낸 뒤 다음 지정 입구로 넘어간다.
		Add(Request.ExplicitCandidates[ExplicitCandidateIndex]);
		return;
	}
	if (Request.Mode == EPlayerRelocationMode::Flight && Request.bPreferOffset)
		Add(Reference + Yaw.RotateVector(Request.PreferredLocalOffset));
	const int32 Steps = FMath::CeilToInt(Radius / Request.GridSpacing);
	// 전체 후보를 유한하게 생성한 뒤 EQS 필터를 통과한 후보끼리만 순위를 비교한다.
	for (int32 X = -Steps; X <= Steps && !bBudgetExceeded; ++X)
	{
		for (int32 Y = -Steps; Y <= Steps && !bBudgetExceeded; ++Y)
		{
			const float Height = Request.Mode == EPlayerRelocationMode::Flight ? Request.PreferredLocalOffset.Z : 0;
			Add(Reference + Yaw.RotateVector(FVector(X * Request.GridSpacing, Y * Request.GridSpacing, Height)));
		}
	}
}

void UPlayerRelocationQuery::BeginStage()
{
	if (!IsContextValid()) { Finish(EPlayerRelocationStatus::InvalidContext); return; }
	if (FPlatformTime::Seconds() >= Deadline) { Finish(EPlayerRelocationStatus::TimedOut); return; }
	Result.bExpanded = Stage >= 2;
	Result.Posture = Request.Mode == EPlayerRelocationMode::Ground && Stage % 2 == 1
		? EPlayerRelocationPosture::Crouched : EPlayerRelocationPosture::Standing;
	// 모든 안전 후보를 받아 직접 동률을 해소한다. EQS의 무작위 단일 선택은 사용하지 않는다.
	QueryId = FEnvQueryRequest(QueryTemplate, this).Execute(EEnvQueryRunMode::AllMatching,
		FQueryFinishedSignature::CreateUObject(this, &ThisClass::OnQueryFinished));
	if (QueryId == INDEX_NONE) Finish(EPlayerRelocationStatus::InvalidContext);
}

bool UPlayerRelocationQuery::IsBetter(const FVector& Candidate, const FVector& Best) const
{
	const FVector Reference = Request.Reference.GetLocation();
	if (Request.Mode == EPlayerRelocationMode::Flight && Request.bPreferOffset)
	{
		const FVector Preferred = Reference + FRotator(0, Request.Reference.Rotator().Yaw, 0).RotateVector(Request.PreferredLocalOffset);
		const bool bCandidatePreferred = Candidate.Equals(Preferred, DistanceTieTolerance);
		const bool bBestPreferred = Best.Equals(Preferred, DistanceTieTolerance);
		if (bCandidatePreferred != bBestPreferred) return bCandidatePreferred;
	}
	const double Difference = FVector::Distance(Candidate, Reference) - FVector::Distance(Best, Reference);
	if (FMath::Abs(Difference) > DistanceTieTolerance) return Difference < 0;
	const FVector Forward = FRotator(0, Request.Reference.Rotator().Yaw, 0).Vector();
	const double RearDifference = FVector::DotProduct((Candidate - Reference).GetSafeNormal2D(), Forward)
		- FVector::DotProduct((Best - Reference).GetSafeNormal2D(), Forward);
	if (!FMath::IsNearlyZero(RearDifference)) return RearDifference < 0;
	if (Candidate.X != Best.X) return Candidate.X < Best.X;
	if (Candidate.Y != Best.Y) return Candidate.Y < Best.Y;
	return Candidate.Z < Best.Z;
}

void UPlayerRelocationQuery::OnQueryFinished(TSharedPtr<FEnvQueryResult> QueryResult)
{
	if (Result.Status != EPlayerRelocationStatus::Running || !QueryResult || QueryResult->QueryID != QueryId) return;
	QueryId = INDEX_NONE;
	if (!IsContextValid()) { Finish(EPlayerRelocationStatus::InvalidContext); return; }
	if (FPlatformTime::Seconds() >= Deadline) { Finish(EPlayerRelocationStatus::TimedOut); return; }
	if (bBudgetExceeded) { Finish(EPlayerRelocationStatus::BudgetExceeded); return; }
	if (QueryResult->IsAborted()) { Finish(EPlayerRelocationStatus::Cancelled); return; }
	bool bFound = false;
	for (int32 Index = 0; QueryResult->IsSuccessful() && Index < QueryResult->Items.Num(); ++Index)
	{
		const FVector Candidate = QueryResult->GetItemAsLocation(Index);
		if (!bFound || IsBetter(Candidate, Result.Location)) Result.Location = Candidate;
		bFound = true;
	}
	if (bFound)
	{
		// EQS가 여러 프레임에 걸쳐 실행된 동안 바뀐 장애물/바닥도 완료 직전에 거부한다.
		if (Validate(Result.Location)) Finish(EPlayerRelocationStatus::Success);
		else Finish(bBudgetExceeded ? EPlayerRelocationStatus::BudgetExceeded : EPlayerRelocationStatus::NoCandidate);
		return;
	}
	if (!Request.ExplicitCandidates.IsEmpty())
	{
		if (Request.Mode == EPlayerRelocationMode::Ground && Stage == 0 && !Request.bWantsCrouch)
		{
			Stage = 1;
		}
		else
		{
			++ExplicitCandidateIndex;
			Stage = Request.Mode == EPlayerRelocationMode::Ground && Request.bWantsCrouch ? 1 : 0;
		}
		if (ExplicitCandidateIndex < Request.ExplicitCandidates.Num()) BeginStage();
		else Finish(EPlayerRelocationStatus::NoCandidate);
		return;
	}
	Stage += Request.Mode == EPlayerRelocationMode::Flight || Request.bWantsCrouch ? 2 : 1;
	if (Stage < 4) BeginStage();
	else Finish(EPlayerRelocationStatus::NoCandidate);
}

bool UPlayerRelocationQuery::Revalidate()
{
	if (Result.Status != EPlayerRelocationStatus::Success) return false;
	// 시전 후 재검증은 원래 EQS 대기 기한과 별개인 한 번의 제한된 검사다.
	const double PreviousDeadline = Deadline;
	const int32 PreviousChecks = Result.CollisionChecks;
	Deadline = FPlatformTime::Seconds() + Request.TimeoutSeconds;
	Result.CollisionChecks = 0;
	const bool bValid = Validate(Result.Location);
	Result.CollisionChecks += PreviousChecks;
	Deadline = PreviousDeadline;
	return bValid;
}

bool UPlayerRelocationQuery::RevalidateForCrouchIntent(bool bWantsCrouch)
{
	if (Result.Status != EPlayerRelocationStatus::Success || !IsContextValid()) return false;
	Request.bWantsCrouch = bWantsCrouch;
	if (Request.Mode == EPlayerRelocationMode::Ground && bWantsCrouch
		&& Result.Posture == EPlayerRelocationPosture::Standing)
	{
		// 시전 중 새 앉기 입력은 같은 바닥에서 반영한다. 캡슐 중심만 낮추고 탐색 기준은 유지한다.
		float Radius, PreviousHeight, CrouchedHeight;
		GetShape(Radius, PreviousHeight);
		Result.Posture = EPlayerRelocationPosture::Crouched;
		GetShape(Radius, CrouchedHeight);
		Result.Location.Z += CrouchedHeight - PreviousHeight;
	}
	// 서기 의도로 바뀌었어도 낮은 천장용 앉기 후보는 유효하다. 착지 후 CMC가 안전하게 기립한다.
	return Revalidate();
}

void UPlayerRelocationQuery::Finish(EPlayerRelocationStatus Status)
{
	if (Result.Status != EPlayerRelocationStatus::Running) return;
	Result.Status = Status;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TimeoutHandle);
	const int32 PendingId = QueryId;
	QueryId = INDEX_NONE;
	// Abort가 완료 Delegate를 호출해도 이미 종료된 요청은 결과를 다시 발행하지 않는다.
	if (PendingId != INDEX_NONE)
		if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(GetWorld())) Manager->AbortQuery(PendingId);
	TFunction<void(const FPlayerRelocationResult&)> Finished = MoveTemp(Completion);
	if (Finished) Finished(Result);
}

void UPlayerRelocationQuery::Cancel()
{
	// 탐색 성공과 실제 이동 사이에도 취소할 수 있다. 완료 통보는 반복하지 않고 결과만 무효화한다.
	if (Result.Status == EPlayerRelocationStatus::Success) Result.Status = EPlayerRelocationStatus::Cancelled;
	else Finish(EPlayerRelocationStatus::Cancelled);
}
void UPlayerRelocationQuery::OnTimeout() { Finish(EPlayerRelocationStatus::TimedOut); }

UPlayerRelocationGenerator::UPlayerRelocationGenerator() { ItemType = UEnvQueryItemType_Point::StaticClass(); }
void UPlayerRelocationGenerator::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	if (UPlayerRelocationQuery* Owner = Cast<UPlayerRelocationQuery>(QueryInstance.Owner.Get())) Owner->Generate(QueryInstance);
}

UPlayerRelocationSafetyTest::UPlayerRelocationSafetyTest()
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_Point::StaticClass();
	SetWorkOnFloatValues(false);
	TestPurpose = EEnvTestPurpose::Filter;
	BoolValue.DefaultValue = true;
}

void UPlayerRelocationSafetyTest::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UPlayerRelocationQuery* Owner = Cast<UPlayerRelocationQuery>(QueryInstance.Owner.Get());
	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const bool bValid = Owner && Owner->Validate(GetItemLocation(QueryInstance, It.GetIndex()));
		It.SetScore(TestPurpose, EEnvTestFilterType::Match, bValid, true);
	}
}
