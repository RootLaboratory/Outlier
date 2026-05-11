// Fill out your copyright notice in the Description page of Project Settings.


#include "OutlierPlayerState.h"
#include "Net/UnrealNetwork.h"

void AOutlierPlayerState::OnRep_CheckpointData()
{

}

void AOutlierPlayerState::SetCheckpointData(const FOutlierCheckpointData& NewData)
{
	if (!HasAuthority())
	{
		return;
	}

	CheckpointData = NewData;
}

void AOutlierPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOutlierPlayerState, CheckpointData);
}
