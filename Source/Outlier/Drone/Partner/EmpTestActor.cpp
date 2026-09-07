#include "Drone/Partner/EmpTestActor.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/EMPFrameBillboardActor.h"
#include "Drone/Partner/EMPGameplayTags.h"

AEmpTestActor::AEmpTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	EMPComponent = CreateDefaultSubobject<UEMPableComponent>(TEXT("EMPComponent"));
}

void AEmpTestActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && EMPComponent)
	{
		EMPComponent->AddEMPTag(EMPGameplayTags::Target::EMPable());
	}
}

UEMPableComponent* AEmpTestActor::GetEMPableComponent() const
{
	return EMPComponent;
}

void AEmpTestActor::HandleEMPStarted(FGameplayTag EffectTag)
{
	UE_LOG(LogTemp, Warning, TEXT("EMP started: %s Effect=%s"), *GetNameSafe(this), *EffectTag.ToString());
}

void AEmpTestActor::HandleEMPEnded(FGameplayTag EffectTag)
{
	UE_LOG(LogTemp, Warning, TEXT("EMP ended: %s Effect=%s"), *GetNameSafe(this), *EffectTag.ToString());
}
