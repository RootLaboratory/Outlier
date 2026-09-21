#include "Enemy/Anim/AnimNotify_TurretDeployFinished.h"

#include "Enemy/AutoTurret.h"

void UAnimNotify_TurretDeployFinished::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AAutoTurret* Turret = MeshComp ? Cast<AAutoTurret>(MeshComp->GetOwner()) : nullptr;
	if (Turret && Turret->HasAuthority())
	{
		// Reset은 이전 Montage를 정지하므로, Notify 시점의 토큰은 현재 전개 수명만 가리킨다.
		Turret->NotifyDeploySequenceFinished(
			Turret->GetRoomWaveGameplayGeneration(),
			Turret->GetRoomWaveActivationSerial());
	}
}

FString UAnimNotify_TurretDeployFinished::GetNotifyName_Implementation() const
{
	return TEXT("Turret Deploy Finished");
}
