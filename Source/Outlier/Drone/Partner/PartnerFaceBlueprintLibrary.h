#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Drone/Partner/PartnerSpriteAnimationComponent.h"
#include "PartnerFaceBlueprintLibrary.generated.h"

class APartnerCharacter;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USkeletalMeshComponent;

UCLASS()
class OUTLIER_API UPartnerFaceBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 시퀀스 이벤트 등에서 파트너 메시의 얼굴 스프라이트를 직접 교체한다. 0번 프레임부터 재생된다. */
	UFUNCTION(BlueprintCallable, Category = "Partner|Face")
	static bool SetPartnerFaceSprite(
		USkeletalMeshComponent* Mesh,
		const FPartnerSpriteAnimationData& Sprite,
		FName MaterialSlotName = TEXT("Eye"),
		FName TextureParameterName = TEXT("SpriteTexture"));

	/**
	 * PartnerClass(BP_Partner 등)의 기본값에 설정된 표정 시트로 얼굴을 교체한다.
	 * 슬롯 이름과 텍스처 파라미터 이름도 그 클래스의 설정을 따른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Partner|Face")
	static bool SetPartnerFaceEmotion(
		USkeletalMeshComponent* Mesh,
		EPartnerEmotion Emotion,
		TSubclassOf<APartnerCharacter> PartnerClass);

	/** 정확한 슬롯 이름을 우선하고, 없으면 접두사가 일치하는 첫 슬롯을 반환한다. */
	static int32 FindFaceMaterialIndex(const UPrimitiveComponent* Mesh, FName MaterialSlotName);

	/** 스프라이트 시트와 격자, 재생 시작 시간을 MID에 넣는다. */
	static bool ApplyFaceSprite(
		UMaterialInstanceDynamic* MID,
		const FPartnerSpriteAnimationData& Sprite,
		FName TextureParameterName,
		float AnimationStartTime);
};
