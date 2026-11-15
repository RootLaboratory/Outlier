#include "Drone/Partner/PartnerFaceBlueprintLibrary.h"

#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

int32 UPartnerFaceBlueprintLibrary::FindFaceMaterialIndex(const UPrimitiveComponent* Mesh, FName MaterialSlotName)
{
	if (!Mesh || MaterialSlotName.IsNone())
	{
		return INDEX_NONE;
	}

	const int32 ExactIndex = Mesh->GetMaterialIndex(MaterialSlotName);
	if (ExactIndex != INDEX_NONE)
	{
		return ExactIndex;
	}

	// 정확한 슬롯 이름이 없으면 Eye_001 같은 접두사 일치 슬롯을 사용한다.
	const FString SlotPrefix = MaterialSlotName.ToString();
	const TArray<FName> SlotNames = Mesh->GetMaterialSlotNames();
	for (int32 Index = 0; Index < SlotNames.Num(); ++Index)
	{
		if (SlotNames[Index].ToString().StartsWith(SlotPrefix, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UPartnerFaceBlueprintLibrary::ApplyFaceSprite(
	UMaterialInstanceDynamic* MID,
	const FPartnerSpriteAnimationData& Sprite,
	FName TextureParameterName,
	float AnimationStartTime)
{
	if (!MID || !Sprite.Texture)
	{
		return false;
	}

	MID->SetTextureParameterValue(TextureParameterName, Sprite.Texture.Get());
	MID->SetScalarParameterValue(TEXT("Rows"), static_cast<float>(Sprite.Rows));
	MID->SetScalarParameterValue(TEXT("Columns"), static_cast<float>(Sprite.Columns));
	MID->SetScalarParameterValue(TEXT("AnimationStartTime"), AnimationStartTime);
	return true;
}

bool UPartnerFaceBlueprintLibrary::SetPartnerFaceEmotion(
	USkeletalMeshComponent* Mesh,
	EPartnerEmotion Emotion,
	TSubclassOf<APartnerCharacter> PartnerClass)
{
	APartnerCharacter* PartnerDefaults = PartnerClass ? PartnerClass->GetDefaultObject<APartnerCharacter>() : nullptr;
	const UPartnerSpriteAnimationComponent* SpriteDefaults = PartnerDefaults
		? PartnerDefaults->FindComponentByClass<UPartnerSpriteAnimationComponent>() : nullptr;
	if (!SpriteDefaults && PartnerDefaults)
	{
		// 기본 객체에서 OwnedComponents가 비어 있을 때를 대비해 기본 서브오브젝트에서 다시 찾는다.
		TArray<UObject*> Subobjects;
		PartnerDefaults->GetDefaultSubobjects(Subobjects);
		for (UObject* Subobject : Subobjects)
		{
			if (const UPartnerSpriteAnimationComponent* Found = Cast<UPartnerSpriteAnimationComponent>(Subobject))
			{
				SpriteDefaults = Found;
				break;
			}
		}
	}
	if (!SpriteDefaults)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPartnerFaceEmotion: '%s' has no PartnerSpriteAnimationComponent."),
			*GetNameSafe(PartnerClass.Get()));
		return false;
	}

	const FPartnerSpriteAnimationData* Sprite = SpriteDefaults->FindEmotionSprite(Emotion);
	if (!Sprite || !Sprite->Texture)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPartnerFaceEmotion: emotion '%s' has no sprite in '%s'."),
			*UEnum::GetValueAsString(Emotion), *GetNameSafe(PartnerClass.Get()));
		return false;
	}

	return SetPartnerFaceSprite(Mesh, *Sprite,
		SpriteDefaults->GetMaterialSlotName(), SpriteDefaults->GetTextureParameterName());
}

bool UPartnerFaceBlueprintLibrary::SetPartnerFaceSprite(
	USkeletalMeshComponent* Mesh,
	const FPartnerSpriteAnimationData& Sprite,
	FName MaterialSlotName,
	FName TextureParameterName)
{
	const int32 MaterialIndex = FindFaceMaterialIndex(Mesh, MaterialSlotName);
	UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	if (MaterialIndex == INDEX_NONE || !World)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPartnerFaceSprite: slot '%s' was not found on '%s'."),
			*MaterialSlotName.ToString(), *GetNameSafe(Mesh));
		return false;
	}

	// 슬롯이 이미 MID면 엔진이 재사용하므로 이벤트가 반복돼도 MID가 쌓이지 않는다.
	UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(MaterialIndex);
	if (!ApplyFaceSprite(MID, Sprite, TextureParameterName, World->GetTimeSeconds()))
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPartnerFaceSprite: failed to apply sprite on '%s' (MID=%s, Texture=%s)."),
			*GetNameSafe(Mesh), *GetNameSafe(MID), *GetNameSafe(Sprite.Texture.Get()));
		return false;
	}
	return true;
}
