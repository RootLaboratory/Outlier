#include "Impact/ImpactSmokeSubscriberComponent.h"

#include "Impact/ImpactFieldSubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UImpactSmokeSubscriberComponent::UImpactSmokeSubscriberComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UImpactSmokeSubscriberComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TargetNiagara && GetOwner())
	{
		TargetNiagara = GetOwner()->FindComponentByClass<UNiagaraComponent>();
	}
}

void UImpactSmokeSubscriberComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetNiagara)
	{
		return;
	}

	UWorld* W = GetWorld();
	UImpactFieldSubsystem* Impact = W ? W->GetSubsystem<UImpactFieldSubsystem>() : nullptr;
	if (!Impact)
	{
		return;
	}

	const int32 Count = Impact->GetActiveCount();
	// 임팩트가 계속 0이면 매 프레임 빈 배열 set 할 필요 없음. 0으로 한 번은 비워줌.
	if (Count == 0 && LastPushedCount == 0)
	{
		return;
	}

	TArray<FVector> Positions, ScatterDirs, Normals;
	TArray<float> Radii, Strengths, Ages;
	Impact->BuildArrays(Positions, ScatterDirs, Normals, Radii, Strengths, Ages);

	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(TargetNiagara, PositionsParam, Positions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(TargetNiagara, ScatterDirsParam, ScatterDirs);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(TargetNiagara, NormalsParam, Normals);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(TargetNiagara, RadiiParam, Radii);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(TargetNiagara, StrengthsParam, Strengths);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(TargetNiagara, AgesParam, Ages);

	LastPushedCount = Count;
}
