#include "Drone/Partner/EmpTestActor.h"
#include "Drone/Partner/EMPableComponent.h"
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

void AEmpTestActor::HandleEmp(FGameplayTag EffectTag)
{
	UE_LOG(LogTemp, Warning, TEXT("EMPed: %s Effect=%s"), *GetNameSafe(this), *EffectTag.ToString());
}
