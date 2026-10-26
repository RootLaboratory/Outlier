// Fill out your copyright notice in the Description page of Project Settings.

#include "Drone/Partner/PartnerSpriteAnimationComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPartnerEmotion, Log, All);

UPartnerSpriteAnimationComponent::UPartnerSpriteAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPartnerSpriteAnimationComponent::BeginPlay()
{
	Super::BeginPlay();

	// 눈 깜빡임은 대기 여부와 무관하며, 렌더링하지 않는 전용 서버에서도 타이머를 시작한다.
	SetBlinkingActive(true);

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

	int32 MaterialIndex = Mesh->GetMaterialIndex(MaterialSlotName);
	// 정확한 슬롯 이름을 우선하고, 없으면 Eye_001 같은 접두사 일치 슬롯을 사용한다.
	if (MaterialIndex == INDEX_NONE && !MaterialSlotName.IsNone())
	{
		const FString SlotPrefix = MaterialSlotName.ToString();
		const TArray<FName> SlotNames = Mesh->GetMaterialSlotNames();
		for (int32 Index = 0; Index < SlotNames.Num(); ++Index)
		{
			if (SlotNames[Index].ToString().StartsWith(SlotPrefix, ESearchCase::IgnoreCase))
			{
				MaterialIndex = Index;
				break;
			}
		}
	}
	if (MaterialIndex == INDEX_NONE)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Material slot name or prefix '%s' was not found on '%s'."),
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
	SpriteMaterialIndex = MaterialIndex;
	UE_LOG(LogPartnerEmotion, Log,
		TEXT("[PartnerEmotion] Init Owner=%s NetMode=%d Emotion=%s Mesh=%s Slot=%s Index=%d MID=%s Parameter=%s"),
		*GetNameSafe(GetOwner()), static_cast<int32>(GetNetMode()),
		*UEnum::GetValueAsString(CurrentEmotion), *GetNameSafe(Mesh),
		*MaterialSlotName.ToString(), MaterialIndex, *GetNameSafe(SpriteMID), *TextureParameterName.ToString());
	ApplyEmotionTexture(CurrentEmotion);
}

void UPartnerSpriteAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EmotionTransitionTimerHandle);
		World->GetTimerManager().ClearTimer(TemporaryEmotionTimerHandle);
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
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
	RequestEmotion(InPartnerEmotion, false);
}

void UPartnerSpriteAnimationComponent::SetTemporaryEmotion(EPartnerEmotion InPartnerEmotion)
{
	RequestEmotion(InPartnerEmotion, true);
}

void UPartnerSpriteAnimationComponent::RequestEmotion(EPartnerEmotion InPartnerEmotion, bool bTemporary)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		ServerSetEmotion_Implementation(InPartnerEmotion, bTemporary);
		return;
	}

	ServerSetEmotion(InPartnerEmotion, bTemporary);
}

void UPartnerSpriteAnimationComponent::ServerSetEmotion_Implementation(
	EPartnerEmotion InPartnerEmotion, bool bTemporary)
{
	SetEmotionAuthority(InPartnerEmotion);
	if (GetOwner() && GetOwner()->HasAuthority() && GetWorld()
		&& !bDeathEmotionActive && bTemporary && InPartnerEmotion != EPartnerEmotion::Default)
	{
		const float Duration = FMath::Max3(TemporaryEmotionDuration, ClosedTime + 0.25f, 0.1f);
		GetWorld()->GetTimerManager().SetTimer(TemporaryEmotionTimerHandle, this,
			&ThisClass::ResetTemporaryEmotion, Duration, false);
	}
}

void UPartnerSpriteAnimationComponent::ResetTemporaryEmotion()
{
	SetEmotionAuthority(EPartnerEmotion::Default);
}

void UPartnerSpriteAnimationComponent::SetEmotionAuthority(EPartnerEmotion InPartnerEmotion, bool bFromBlink)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()
		|| (bDeathEmotionActive && InPartnerEmotion != EPartnerEmotion::Closed))
	{
		return;
	}

	if (!bFromBlink)
	{
		bOwnsBlink = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BlinkTimerHandle);
			World->GetTimerManager().ClearTimer(TemporaryEmotionTimerHandle);
		}
		if (InPartnerEmotion == EPartnerEmotion::Default)
		{
			// Default를 명시적으로 요청하면 스킬 등 외부 표정의 우선권을 해제한다.
			ScheduleBlink();
		}
	}

	if (CurrentEmotion == InPartnerEmotion)
	{
		return;
	}

	UE_LOG(LogPartnerEmotion, Log,
		TEXT("[PartnerEmotion] Authority Owner=%s NetMode=%d Previous=%s Current=%s FromBlink=%d"),
		*GetNameSafe(Owner), static_cast<int32>(GetNetMode()),
		*UEnum::GetValueAsString(CurrentEmotion), *UEnum::GetValueAsString(InPartnerEmotion), bFromBlink ? 1 : 0);
	CurrentEmotion = InPartnerEmotion;

	if (GetNetMode() != NM_DedicatedServer)
	{
		StartEmotionTransition();
	}

	Owner->ForceNetUpdate();
}

void UPartnerSpriteAnimationComponent::SetBlinkingActive(bool bActive)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		return;
	}
	bBlinkingActive = bActive;
	GetWorld()->GetTimerManager().ClearTimer(BlinkTimerHandle);
	if (!bActive)
	{
		if (bOwnsBlink)
		{
			bOwnsBlink = false;
			SetEmotionAuthority(EPartnerEmotion::Default, true);
		}
		return;
	}
	if (CurrentEmotion == EPartnerEmotion::Default || bOwnsBlink)
	{
		ScheduleBlink();
	}
}

void UPartnerSpriteAnimationComponent::SetDeathEmotionActive(bool bActive)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDeathEmotionActive == bActive)
	{
		return;
	}
	bDeathEmotionActive = bActive;
	if (bActive)
	{
		SetBlinkingActive(false);
	}
	else
	{
		SetBlinkingActive(true);
	}
	SetEmotionAuthority(bActive ? EPartnerEmotion::Closed : EPartnerEmotion::Default);
}

void UPartnerSpriteAnimationComponent::ScheduleBlink()
{
	if (!bBlinkingActive || !GetWorld())
	{
		return;
	}
	const float MinInterval = FMath::Max(StandbyBlinkIntervalMin, 0.1f);
	const float MaxInterval = FMath::Max(MinInterval, StandbyBlinkIntervalMax);
	GetWorld()->GetTimerManager().SetTimer(BlinkTimerHandle, this,
		&ThisClass::PlayBlink, FMath::FRandRange(MinInterval, MaxInterval), false);
}

void UPartnerSpriteAnimationComponent::PlayBlink()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bBlinkingActive
		|| bDeathEmotionActive || CurrentEmotion != EPartnerEmotion::Default)
	{
		return;
	}
	const FPartnerSpriteAnimationData* DefaultData = EmotionSprites.Find(EPartnerEmotion::Default);
	const FPartnerSpriteAnimationData* ClosedData = EmotionSprites.Find(EPartnerEmotion::Closed);
	if (!DefaultData || !DefaultData->Texture || !ClosedData || !ClosedData->Texture)
	{
		ScheduleBlink();
		return;
	}
	bOwnsBlink = true;
	SetEmotionAuthority(EPartnerEmotion::Closed, true);
	GetWorld()->GetTimerManager().SetTimer(BlinkTimerHandle, this,
		&ThisClass::FinishBlink, FMath::Max(StandbyBlinkClosedDuration, 0.05f), false);
}

void UPartnerSpriteAnimationComponent::FinishBlink()
{
	if (!bOwnsBlink || bDeathEmotionActive)
	{
		return;
	}
	bOwnsBlink = false;
	SetEmotionAuthority(EPartnerEmotion::Default, true);
	ScheduleBlink();
}

void UPartnerSpriteAnimationComponent::OnRep_CurrentEmotion()
{
	UE_LOG(LogPartnerEmotion, Log,
		TEXT("[PartnerEmotion] OnRep Owner=%s NetMode=%d Current=%s Displayed=%s"),
		*GetNameSafe(GetOwner()), static_cast<int32>(GetNetMode()),
		*UEnum::GetValueAsString(CurrentEmotion), *UEnum::GetValueAsString(DisplayedEmotion));
	StartEmotionTransition();
}

void UPartnerSpriteAnimationComponent::StartEmotionTransition()
{
	if (!SpriteMID || !GetWorld())
	{
		UE_LOG(LogPartnerEmotion, Warning,
			TEXT("[PartnerEmotion] Transition skipped Owner=%s Current=%s MID=%s World=%s"),
			*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(CurrentEmotion),
			*GetNameSafe(SpriteMID), *GetNameSafe(GetWorld()));
		return;
	}

	const FPartnerSpriteAnimationData* TargetData = EmotionSprites.Find(CurrentEmotion);
	if (!TargetData || !TargetData->Texture)
	{
		UE_LOG(LogPartnerEmotion, Warning,
			TEXT("[PartnerEmotion] Missing target texture Owner=%s Current=%s"),
			*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(CurrentEmotion));

		return;
	}

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(EmotionTransitionTimerHandle);
	bTransitioning = false;

	// 깜빡임에서 눈을 뜰 때는 일반 표정 전환의 눈 감기 시간을 다시 적용하지 않는다.
	if (CurrentEmotion == EPartnerEmotion::Closed || ClosedTime <= 0.0f
		|| (CurrentEmotion == EPartnerEmotion::Default && DisplayedEmotion == EPartnerEmotion::Closed))
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

	// 적용 대상이 다른 머티리얼로 교체됐는지 표정 변경 시에만 확인한다.
	const APartnerCharacter* Partner = Cast<APartnerCharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Partner ? Partner->GetMesh() : nullptr;
	const int32 MaterialIndex = SpriteMaterialIndex;
	const UMaterialInterface* SlotMaterial = Mesh && MaterialIndex != INDEX_NONE
		? Mesh->GetMaterial(MaterialIndex) : nullptr;
	UE_LOG(LogPartnerEmotion, Log,
		TEXT("[PartnerEmotion] Apply Owner=%s NetMode=%d Current=%s Applied=%s Texture=%s Rows=%d Columns=%d Parameter=%s Mesh=%s Slot=%s MID=%s SlotMaterial=%s MIDMatchesSlot=%d"),
		*GetNameSafe(GetOwner()), static_cast<int32>(GetNetMode()),
		*UEnum::GetValueAsString(CurrentEmotion), *UEnum::GetValueAsString(Emotion),
		*GetNameSafe(Data->Texture.Get()), Data->Rows, Data->Columns, *TextureParameterName.ToString(),
		*GetNameSafe(Mesh), *MaterialSlotName.ToString(), *GetNameSafe(SpriteMID),
		*GetNameSafe(SlotMaterial), SlotMaterial == SpriteMID.Get() ? 1 : 0);
	DisplayedEmotion = Emotion;
	return true;
}
