#include "Environment/LightControlVolume.h"

#include "Components/BrushComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Editor.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "TimerManager.h"
#include "UObject/ObjectSaveContext.h"
#endif

ALightControlVolume::ALightControlVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	// EncompassesPoint는 Brush의 Query 콜리전으로 거리를 계산하므로 Query만 남기고 Overlap은 끈다.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BrushComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		BrushComp->SetGenerateOverlapEvents(false);
	}
}

void ALightControlVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALightControlVolume, bLightsTriggered);
}

void ALightControlVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	// BP를 콘텐츠 브라우저에서 드래그하면 Volume 팩토리를 거치지 않아 Brush가 비어 있다.
	// 생성 도중 PostEditChange가 Construction을 다시 돌리지 않도록 다음 에디터 틱에 Box Brush를 만든다.
	const UWorld* World = GetWorld();
	if (!GIsEditor || !GEditor || IsTemplate() || !World || World->IsGameWorld() || HasBrushGeometry()) return;
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!HasBrushGeometry()) CreateDefaultBoxBrush();
	}));
#endif
}

#if WITH_EDITOR
bool ALightControlVolume::HasBrushGeometry() const
{
	return Brush && Brush->Polys && Brush->Polys->Element.Num() > 0;
}

void ALightControlVolume::CreateDefaultBoxBrush()
{
	if (!IsValid(this) || !GetWorld()) return;
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = DefaultBrushSize.X;
	Builder->Y = DefaultBrushSize.Y;
	Builder->Z = DefaultBrushSize.Z;
	UActorFactory::CreateBrushForVolumeActor(this, Builder);
}
#endif

void ALightControlVolume::BeginPlay()
{
	Super::BeginPlay();
	// Sub-level이 스트리밍되어 BeginPlay가 불리는 시점에는 Persistent Level이 이미 World에 올라가 있다.
	CacheLights();
	if (bLightsTriggered) ApplyLightState();
}

void ALightControlVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Sub-level만 언로드되는 경우 Persistent Level의 Light가 바뀐 채로 남지 않도록 복원한다.
	RestoreOriginalLights();
	CachedLights.Reset();
	Super::EndPlay(EndPlayReason);
}

void ALightControlVolume::SetLightsTriggered(bool bTriggered)
{
	if (bLightsTriggered == bTriggered) return;
	bLightsTriggered = bTriggered;
	ApplyLightState();
	OnLightsTriggerChanged(bTriggered);
}

void ALightControlVolume::OnRep_LightsTriggered()
{
	ApplyLightState();
	OnLightsTriggerChanged(bLightsTriggered);
}

void ALightControlVolume::RefreshCachedLights()
{
	RestoreOriginalLights();
	CacheLights();
	if (bLightsTriggered) ApplyLightState();
}

void ALightControlVolume::ApplyLightState()
{
	if (!bLightsTriggered)
	{
		RestoreOriginalLights();
		return;
	}
	ApplyTargetToLights(CachedLights);
}

TArray<ULightComponent*> ALightControlVolume::GetCachedLights() const
{
	TArray<ULightComponent*> Result;
	Result.Reserve(CachedLights.Num());
	for (const FLightControlCachedState& State : CachedLights)
	{
		if (ULightComponent* Light = State.Light.Get()) Result.Add(Light);
	}
	return Result;
}

void ALightControlVolume::CacheLights()
{
	CollectLights(CachedLights);
	UE_LOG(LogOutlier, Log, TEXT("[%s] Cached %d light(s)."), *GetName(), CachedLights.Num());
}

void ALightControlVolume::RestoreOriginalLights()
{
	RestoreLights(CachedLights);
}

void ALightControlVolume::CollectLights(TArray<FLightControlCachedState>& OutLights) const
{
	OutLights.Reset();
	const UWorld* World = GetWorld();
	if (!World) return;

	auto CollectFromLevel = [this, &OutLights](const ULevel* Level)
	{
		if (!Level || !Level->bIsVisible) return;
		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor) || Actor == this) continue;
			TInlineComponentArray<ULightComponent*> Lights(Actor);
			for (ULightComponent* Light : Lights)
			{
				if (!Light || !EncompassesPoint(Light->GetComponentLocation(), BoundsTolerance)) continue;
				if (Light->Mobility == EComponentMobility::Static)
				{
					UE_LOG(LogOutlier, Warning, TEXT("[%s] Static light %s cannot change at runtime. Set it to Movable or Stationary."),
						*GetName(), *GetNameSafe(Actor));
					continue;
				}
				FLightControlCachedState& State = OutLights.AddDefaulted_GetRef();
				State.Light = Light;
				State.OriginalIntensity = Light->Intensity;
				State.OriginalColor = Light->GetLightColor();
			}
		}
	};

	if (bSearchAllLoadedLevels)
	{
		for (const ULevel* Level : World->GetLevels()) CollectFromLevel(Level);
	}
	else
	{
		CollectFromLevel(World->PersistentLevel);
	}
}

void ALightControlVolume::ApplyTargetToLights(const TArray<FLightControlCachedState>& Lights) const
{
	for (const FLightControlCachedState& State : Lights)
	{
		ULightComponent* Light = State.Light.Get();
		if (!Light) continue;
		if (bApplyIntensity) Light->SetIntensity(TargetIntensity);
		if (bApplyColor) Light->SetLightColor(TargetColor);
	}
}

void ALightControlVolume::RestoreLights(const TArray<FLightControlCachedState>& Lights)
{
	for (const FLightControlCachedState& State : Lights)
	{
		ULightComponent* Light = State.Light.Get();
		if (!Light) continue;
		Light->SetIntensity(State.OriginalIntensity);
		Light->SetLightColor(State.OriginalColor);
	}
}

#if WITH_EDITOR
void ALightControlVolume::PreviewApplyLights()
{
	// 미리보기 중이면 원래 값은 유지하고 Target만 다시 적용한다.
	if (!bPreviewActive)
	{
		CollectLights(PreviewLights);
		bPreviewActive = true;
		// 실수로 저장하거나 PIE를 시작해도 미리보기 값이 맵/PIE로 넘어가지 않도록 먼저 복원한다.
		PreSaveHandle = FEditorDelegates::PreSaveWorldWithContext.AddWeakLambda(this, [this](UWorld*, FObjectPreSaveContext) { PreviewResetLights(); });
		PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddWeakLambda(this, [this](bool) { PreviewResetLights(); });
	}
	ApplyTargetToLights(PreviewLights);
	UE_LOG(LogOutlier, Log, TEXT("[%s] Preview applied to %d light(s)."), *GetName(), PreviewLights.Num());
}

void ALightControlVolume::PreviewResetLights()
{
	if (!bPreviewActive) return;
	RestoreLights(PreviewLights);
	PreviewLights.Reset();
	bPreviewActive = false;
	FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveHandle);
	FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
	PreSaveHandle.Reset();
	PreBeginPIEHandle.Reset();
}

void ALightControlVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// 미리보기 중에는 Target 값을 바꾸는 즉시 반영한다.
	if (!bPreviewActive) return;
	RestoreLights(PreviewLights);
	ApplyTargetToLights(PreviewLights);
}

void ALightControlVolume::Destroyed()
{
	PreviewResetLights();
	Super::Destroyed();
}
#endif
