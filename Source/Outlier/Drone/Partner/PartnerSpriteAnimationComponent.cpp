// Fill out your copyright notice in the Description page of Project Settings.

#include "Drone/Partner/PartnerSpriteAnimationComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UPartnerSpriteAnimationComponent::UPartnerSpriteAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPartnerSpriteAnimationComponent::BeginPlay()
{
	Super::BeginPlay();

	// A dedicated server owns the replicated state but does not render a material.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Partner ? Partner->GetMesh() : nullptr;
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Partner sprite animation could not find the owner's mesh."));
		return;
	}

	const int32 MaterialIndex = Mesh->GetMaterialIndex(MaterialSlotName);
	if (MaterialIndex == INDEX_NONE)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Material slot '%s' was not found on '%s'."),
			*MaterialSlotName.ToString(),
			*GetNameSafe(Mesh)
		);
		return;
	}

	SpriteMID = Mesh->CreateDynamicMaterialInstance(MaterialIndex);
	if (!SpriteMID)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Failed to create a dynamic material for slot '%s'."),
			*MaterialSlotName.ToString()
		);
		return;
	}
	ApplyEmotionTexture(CurrentEmotion);
}

void UPartnerSpriteAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EmotionTransitionTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UPartnerSpriteAnimationComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPartnerSpriteAnimationComponent, CurrentEmotion);
}

void UPartnerSpriteAnimationComponent::SetEmotion(EPartnerEmotion InPartnerEmotion)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		SetEmotionAuthority(InPartnerEmotion);
		return;
	}

	ServerSetEmotion(InPartnerEmotion);
}

void UPartnerSpriteAnimationComponent::ServerSetEmotion_Implementation(
	EPartnerEmotion InPartnerEmotion)
{
	SetEmotionAuthority(InPartnerEmotion);
}

void UPartnerSpriteAnimationComponent::SetEmotionAuthority(EPartnerEmotion InPartnerEmotion)
{
	UE_LOG(LogTemp, Error, TEXT("SetEmotionAuthority"));

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (CurrentEmotion == InPartnerEmotion)
	{
		return;
	}

	CurrentEmotion = InPartnerEmotion;

	if (GetNetMode() != NM_DedicatedServer)
	{
		StartEmotionTransition();
	}

	//Owner->ForceNetUpdate();
}

void UPartnerSpriteAnimationComponent::OnRep_CurrentEmotion()
{
	StartEmotionTransition();
}

void UPartnerSpriteAnimationComponent::StartEmotionTransition()
{
	if (!SpriteMID || !GetWorld())
	{
			UE_LOG(LogTemp, Error, TEXT("SpriteMID"));

		return;
	}

	const FPartnerSpriteAnimationData* TargetData = EmotionSprites.Find(CurrentEmotion);
	if (!TargetData || !TargetData->Texture)
	{
		UE_LOG(LogTemp, Error, TEXT("argetData->Texture"));

		return;
	}

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(EmotionTransitionTimerHandle);
	bTransitioning = false;

	if (CurrentEmotion == EPartnerEmotion::Closed || ClosedTime <= 0.0f)
	{
		ApplyEmotionTexture(CurrentEmotion);
		return;
	}

	const FPartnerSpriteAnimationData* ClosedData = EmotionSprites.Find(EPartnerEmotion::Closed);
	if (!ClosedData || !ClosedData->Texture || !ApplyEmotionTexture(EPartnerEmotion::Closed))
	{
		ApplyEmotionTexture(CurrentEmotion);
		return;
	}

	bTransitioning = true;
	TimerManager.SetTimer(
		EmotionTransitionTimerHandle,
		this,
		&ThisClass::FinishEmotionTransition,
		ClosedTime,
		false
	);
}

void UPartnerSpriteAnimationComponent::FinishEmotionTransition()
{
	bTransitioning = false;
	ApplyEmotionTexture(CurrentEmotion);
}

bool UPartnerSpriteAnimationComponent::ApplyEmotionTexture(EPartnerEmotion Emotion)
{
	if (GetNetMode() == NM_DedicatedServer || !SpriteMID)
	{
		return false;
	}

	const FPartnerSpriteAnimationData* Data =
		EmotionSprites.Find(Emotion);

	if (!Data || !Data->Texture)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Local sprite data is missing. Emotion=%d Owner=%s Class=%s"),
			static_cast<uint8>(Emotion),
			*GetNameSafe(GetOwner()),
			GetOwner() ? *GetNameSafe(GetOwner()->GetClass()) : TEXT("None")
		);

		return false;
	}

	SpriteMID->SetTextureParameterValue(TextureParameterName, Data->Texture.Get());
	SpriteMID->SetScalarParameterValue(TEXT("Rows"), static_cast<float>(Data->Rows));
	SpriteMID->SetScalarParameterValue(TEXT("Columns"), static_cast<float>(Data->Columns));

	SpriteMID->SetScalarParameterValue(
		TEXT("AnimationStartTime"),
		GetWorld()->GetTimeSeconds()
	);

	//UE_LOG(LogTemp, Error, TEXT("Done->Texture"));


	DisplayedEmotion = Emotion;
	return true;
}
