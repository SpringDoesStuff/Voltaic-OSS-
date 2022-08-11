#pragma once

inline std::vector<UFunction*> toHook;
inline std::vector<std::function<bool(UObject*, void*)>> toCall;
#define HandlePEFunction(ufunctionName, func)                           \
            toHook.push_back(UObject::FindObject<UFunction>(ufunctionName)); \
            toCall.push_back([](UObject * Object, void* Parameters) -> bool func);


#include "game.h"
#include "ue4.h"

#include "FortniteGame/Abilities/Inventory.h"
#include "FortniteGame/Abilities/Build.h"
#include "FortniteGame/Abilities/Edit.h"
#include "FortniteGame/Abilities/DecoTool.h"
#include "FortniteGame/Abilities/Interactions.h"
#include "FortniteGame/Abilities/PlayerManager.h"
#include "FortniteGame/Abilities/Emote.h"
#include "FortniteGame/Abilities/Aircraft.h"
#include "FortniteGame/Abilities/Abilities.h"

#include <fstream>
#include <thread>
#include <ostream>

namespace Hooks
{
    UKismetStringLibrary* USL;
    static int teamIdx = 2;
    std::vector<const wchar_t*> PlayersJoined;

    bool LocalPlayer
    (ULocalPlayer* Player, const FString& URL, FString& OutError, UWorld* World)
    {
        if (bTraveled)
            return true;
        else
            return Functions::LocalPlayer::SpawnPlayActor(Player, URL, OutError, World);
    }

    uint64 GetNetMode(UWorld* World)
    {
        return ENetMode::NM_ListenServer;
    }

    void TickFlush(UNetDriver* _thisNetDriver, float DeltaSeconds)
    {
        printf("\nTickFlush has been called\n");
        auto NetDriver = GetWorld()->NetDriver;
        if (!NetDriver) return;

        if (NetDriver->IsA(UIpNetDriver::StaticClass()) && NetDriver->ClientConnections.Num() > 0 && NetDriver->ClientConnections[0]->InternalAck == false)
            if (NetDriver->ReplicationDriver)
                Functions::ReplicationDriver::ServerReplicateActors(NetDriver->ReplicationDriver);

        Functions::NetDriver::TickFlush(NetDriver, DeltaSeconds);
    }
    /*
    void WelcomePlayer(UWorld* World, UNetConnection* IncomingConnection)
    {
        Functions::World::WelcomePlayer(GetWorld(), IncomingConnection);
    }
    */
    char KickPlayer(__int64 a1, __int64 a2, __int64 a3)
    {
        return 1;
    }

    void World_NotifyControlMessage(UWorld* World, UNetConnection* Connection, uint8_t MessageType, int64* Bunch)
    {
        //MessageBoxA(0, "World_NotifyControlMessage", std::to_string(MessageType).c_str(), 0);
        if (MessageType == 4) //NMT_Netspeed
        {
            //MessageBoxA(0, "NMT_Netspeed", std::to_string(MessageType).c_str(), 0);
            Connection->CurrentNetSpeed = 30000;
        }
        else if (MessageType == 5) //NMT_Login
        {
            //MessageBoxA(0, "NMT_Login", std::to_string(MessageType).c_str(), 0);
            Bunch[7] += (16 * 1024 * 1024);

            auto OnlinePlatformName = FString(L"");

            Functions::NetConnection::ReceiveFString(Bunch, Connection->ClientResponse);
            Functions::NetConnection::ReceiveFString(Bunch, Connection->RequestURL);
            Functions::NetConnection::ReceiveUniqueIdRepl(Bunch, Connection->PlayerID);
            Functions::NetConnection::ReceiveFString(Bunch, OnlinePlatformName);
            //MessageBoxW(0, L"OnlinePlatformName", OnlinePlatformName.c_str(), 0);

            Bunch[7] -= (16 * 1024 * 1024);

            Functions::World::WelcomePlayer(GetWorld(), Connection);
        }
        else
            Functions::World::NotifyControlMessage(GetWorld(), Connection, MessageType, (void*)Bunch);
    }

    __int64 ReplicateMoveToServer_Hook(UCharacterMovementComponent* Component, float DeltaTime, FVector& NewAcceleration)
    {
        //AFortPlayerControllerAthena* PC;
        for (int i = 0; i < GetWorld()->NetDriver->ClientConnections.Num(); i++) {

            auto PlayerController = reinterpret_cast<AFortPlayerControllerAthena*>(GetWorld()->NetDriver->ClientConnections[i]->PlayerController);
            auto Pawn = (APlayerPawn_Athena_C*)PlayerController->Pawn;

            Pawn->CharacterMovement = Component;
            Pawn->Tick_Delta_Seconds = DeltaTime = 0.1;
            Pawn->K2_GetActorLocation() = NewAcceleration;

            return Functions::PlayerController::ReplicateMoveToServer(Component, DeltaTime, NewAcceleration);
        }
        
    }

    void* ServerAcknowledgePossession_Hook(APlayerController* Controller, APlayerPawn_Athena_C* Pawn) {

        Controller->AcknowledgedPawn = Pawn;
        Pawn = (APlayerPawn_Athena_C*)Controller->AcknowledgedPawn;

        return Functions::PlayerController::ServerAcknowledgePosession(Controller, Pawn);
    }

    APlayerController* SpawnPlayActor(UWorld* World, UPlayer* NewPlayer, ENetRole RemoteRole, FURL& URL, void* UniqueId, SDK::FString& Error, uint8_t NetPlayerIndex)
    {
        NewPlayer->CurrentNetSpeed = 30000;
        auto PlayerController = (AFortPlayerControllerAthena*)Functions::World::SpawnPlayActor(GetWorld(), NewPlayer, RemoteRole, URL, UniqueId, Error, NetPlayerIndex);
        NewPlayer->PlayerController = PlayerController;

        if (((AFortGameStateAthena*)(GetWorld()->GameState))->GamePhase >= EAthenaGamePhase::Aircraft)
        {
            KickController(PlayerController, L"You can't join while the match is running.");
            return 0;
        }

        auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;
        PlayerState->SetOwner(PlayerController);

        auto NewPlayerIP = PlayerState->SavedNetworkAddress.c_str();
        std::wcout << L"Spawning Player with IP: " << NewPlayerIP << L"\n";

        for (int i = 0; i < PlayersJoined.size(); i++)
        {
            if (PlayersJoined[i] == NewPlayerIP)
            {
                KickController(PlayerController, L"You can't rejoin in the same match.");
                return 0;
            }

            if (i == PlayersJoined.size() - 1)
                PlayersJoined.push_back(NewPlayerIP);
        }

        InitializePawn(PlayerController);

        PlayerController->QuickBars = SpawnActor<AFortQuickBars>({ -280, 400, 3000 }, PlayerController);
        PlayerController->QuickBars->SetOwner(PlayerController->QuickBars);
        auto QuickBars = PlayerController->QuickBars;
        PlayerController->OnRep_QuickBar();

        static auto Wall = UObject::FindObject<UFortBuildingItemDefinition>("FortBuildingItemDefinition BuildingItemData_Wall.BuildingItemData_Wall");
        static auto Floor = UObject::FindObject<UFortBuildingItemDefinition>("FortBuildingItemDefinition BuildingItemData_Floor.BuildingItemData_Floor");
        static auto Stair = UObject::FindObject<UFortBuildingItemDefinition>("FortBuildingItemDefinition BuildingItemData_Stair_W.BuildingItemData_Stair_W");
        static auto Cone = UObject::FindObject<UFortBuildingItemDefinition>("FortBuildingItemDefinition BuildingItemData_RoofS.BuildingItemData_RoofS");

        static auto Wood = ItemDefinitions::GetMaterial(false, ItemDefinitions::MaterialItemDefinitionNames::Wood);
        static auto Stone = ItemDefinitions::GetMaterial(false, ItemDefinitions::MaterialItemDefinitionNames::Stone);
        static auto Metal = ItemDefinitions::GetMaterial(false, ItemDefinitions::MaterialItemDefinitionNames::Metal);

        int Ammo_Count;
        static auto Rockets = ItemDefinitions::GetAmmo(false, &Ammo_Count, ItemDefinitions::AmmoItemDefinitionNames::Rockets);
        static auto Shells = ItemDefinitions::GetAmmo(false, &Ammo_Count, ItemDefinitions::AmmoItemDefinitionNames::Shells);
        static auto Medium = ItemDefinitions::GetAmmo(false, &Ammo_Count, ItemDefinitions::AmmoItemDefinitionNames::Medium);
        static auto Light = ItemDefinitions::GetAmmo(false, &Ammo_Count, ItemDefinitions::AmmoItemDefinitionNames::Light);
        static auto Heavy = ItemDefinitions::GetAmmo(false, &Ammo_Count, ItemDefinitions::AmmoItemDefinitionNames::Heavy);

        static auto EditTool = UObject::FindObject<UFortAmmoItemDefinition>("FortEditToolItemDefinition EditTool.EditTool");

        Abilities::Inventory::AddNewItem(PlayerController, Wall, 0, EFortQuickBars::Secondary, 1);
        Abilities::Inventory::AddNewItem(PlayerController, Floor, 1, EFortQuickBars::Secondary, 1);
        Abilities::Inventory::AddNewItem(PlayerController, Stair, 2, EFortQuickBars::Secondary, 1);
        Abilities::Inventory::AddNewItem(PlayerController, Cone, 3, EFortQuickBars::Secondary, 1);


        Abilities::Inventory::AddNewItem(PlayerController, Wood, 0, EFortQuickBars::Secondary, 5000);
        Abilities::Inventory::AddNewItem(PlayerController, Stone, 0, EFortQuickBars::Secondary, 5000);
        Abilities::Inventory::AddNewItem(PlayerController, Metal, 0, EFortQuickBars::Secondary, 5000);

        Abilities::Inventory::AddNewItem(PlayerController, Rockets, 0, EFortQuickBars::Secondary, 0);
        Abilities::Inventory::AddNewItem(PlayerController, Shells, 0, EFortQuickBars::Secondary, 0);
        Abilities::Inventory::AddNewItem(PlayerController, Medium, 0, EFortQuickBars::Secondary, 0);
        Abilities::Inventory::AddNewItem(PlayerController, Light, 0, EFortQuickBars::Secondary, 0);
        Abilities::Inventory::AddNewItem(PlayerController, Heavy, 0, EFortQuickBars::Secondary, 0);


        Abilities::Inventory::AddNewItem(PlayerController, EditTool, 0, EFortQuickBars::Primary, 1);

        QuickBars->ServerActivateSlotInternal(EFortQuickBars::Primary, 0, 0, true, true);

       /* Abilities::Inventory::InitInventory(PlayerController);

        auto entry = Abilities::Inventory::AddNewItem(PlayerController, ItemDefinitions::GetPickaxe(), 0);
        Abilities::Inventory::Update(PlayerController);
        EquipWeaponDefinition(PlayerController->Pawn, (UFortWeaponItemDefinition*)ItemDefinitions::GetPickaxe(), entry.ItemGuid, -1, true);


        int consumable_count = 0;
        auto consumable = ItemDefinitions::GetConsumable(false, &consumable_count, ItemDefinitions::ConsumableItemDefinitionNames::KnockGrenade);
        entry = Abilities::Inventory::AddNewItem(PlayerController, consumable, 5, EFortQuickBars::Primary, consumable_count);
        Abilities::Inventory::Update(PlayerController);
        EquipWeaponDefinition(PlayerController->Pawn, (UFortWeaponItemDefinition*)consumable, entry.ItemGuid, -1, true);*/


        PlayerState->TeamIndex = EFortTeam(teamIdx);

        PlayerState->PlayerTeam->TeamMembers.Add(PlayerController);
        PlayerState->PlayerTeam->Team = EFortTeam(teamIdx);

        PlayerState->SquadId = teamIdx - 1;
        PlayerState->OnRep_PlayerTeam();
        PlayerState->OnRep_SquadId();

        teamIdx++;

        PlayerController->OverriddenBackpackSize = 100;

        PlayerController->ServerReadyToStartMatch();
        

        //PlayerController->ForceNetUpdate();
        

        return PlayerController;
    }

    void* NetDebug(UObject* _this)
    {
        return nullptr;
    }

    __int64 CollectGarbage(__int64 a1)
    {
        return 0;
    }

    

    void OnReload(AFortWeapon* _this, unsigned int a2)
    {
        if (!_this->Owner) return;

        auto PC = ((APawn*)_this->Owner)->Controller;
        if (!PC) return;

        auto WeaponData = _this->WeaponData;
        if (!WeaponData) return;

        int AmmoToRemove = a2;

        Functions::FortWeapon::OnReload(_this, a2);

        if (ItemDefinitions::IsConsumable(WeaponData))
        {
            if (Abilities::Inventory::GetItemCount((AFortPlayerControllerAthena*)PC, WeaponData) <= 1)
            {
                auto Slots = ((AFortPlayerControllerAthena*)PC)->QuickBars->PrimaryQuickBar.Slots;
                for (int i = 0; i < Slots.Num(); i++)
                {
                    auto Slot = Slots[i];
                    if (!Slot.Items.Num()) continue;

                    auto ItemInstance = GetInstanceFromGuid(PC, Slot.Items[0]);
                    if (!ItemInstance) continue;

                    auto ItemDefinition = ItemInstance->ItemEntry.ItemDefinition;
                    if (ItemDefinition == WeaponData)
                    {
                        Abilities::Inventory::RemoveItemFromSlot((AFortPlayerControllerAthena*)PC, i, EFortQuickBars::Primary, 1);
                        return;
                    }
                }
            }
            else
            {
                Abilities::Inventory::DecreaseItemCount((AFortPlayerControllerAthena*)PC, WeaponData, 1);
                return;
            }
        }
        else
        {
            auto AmmoType = WeaponData->GetAmmoWorldItemDefinition_BP();
            if (!AmmoType) return;

            auto ItemInstances = ((AFortPlayerControllerAthena*)PC)->WorldInventory->Inventory.ItemInstances;
            for (int i = 0; i < ItemInstances.Num(); i++)
            {
                auto ItemInstance = ItemInstances[i];
                if (!ItemInstance) continue;

                auto Def = ItemInstance->ItemEntry.ItemDefinition;

                if (Def == AmmoType)
                {
                    Abilities::Inventory::AddItem((AFortPlayerControllerAthena*)PC, ItemInstance, 0, EFortQuickBars::Secondary, ItemInstance->ItemEntry.Count - AmmoToRemove);
                    return;
                }
            }
        }
    }

    void ProcessEventHook(UObject* Object, UFunction* Function, void* Parameters)
    {
        if (!Object || !Function)
            return oProcessEvent(Object, Function, Parameters);
        auto FuncName = Function->GetName();

        if (bTraveled)
        {
            if (bListening) {
                if (Function->GetName() == "ServerAbilityRPCBatch" || Function->GetName() == "Function GameplayAbilities.AbilitySystemComponent.ServerAbilityRPCBatch") {

                }
                if (Function->GetName() == "RecieveTick") {
                    
                }
                /*if ((Function->FunctionFlags & 0x00200000 || Function->FunctionFlags & 0x01000000))
                {
                  printf("RPC Called %s\n", FuncName.c_str());
                }*/
            }
            if (!bListening)
            {

                static auto ReadyToStartMatchFn = UObject::FindObject<UFunction>("Function Engine.GameMode.ReadyToStartMatch");
                
                if (Function == ReadyToStartMatchFn)
                {
                    Game::OnReadyToStartMatch();




                    MessageBoxA(0, "Creating NetDriver", 0, 0);
                    auto NewNetDriver = Functions::Engine::CreateNetDriver(Functions::GetEngine(), GetWorld(), FName(282));
                    MessageBoxA(0, "NewNetDriver", std::to_string((uintptr_t)NewNetDriver).c_str(), 0);





                    NewNetDriver->NetDriverName = FName(282);
                    NewNetDriver->World = GetWorld();


                    FString Error;
                    auto InURL = FURL();
                    InURL.Port = 7777;

                    MessageBoxA(0, "InitListen", std::to_string((uintptr_t)NewNetDriver).c_str(), 0);
                    Functions::NetDriver::InitListen(NewNetDriver, GetWorld(), InURL, true, Error);
                    MessageBoxA(0, "LISTENING", std::to_string((uintptr_t)NewNetDriver).c_str(), 0);

                    MessageBoxA(0, "NetDriver_SetWorld", 0, 0);
                    Functions::NetDriver::SetWorld(NewNetDriver, GetWorld());
                    MessageBoxA(0, "SETTEDSHITSSS", std::to_string((uintptr_t)NewNetDriver).c_str(), 0);
                    MessageBoxA(0, "PROOF", std::to_string((uintptr_t)NewNetDriver->World).c_str(), 0);


                    Functions::ReplicationDriver::ServerReplicateActors = decltype(Functions::ReplicationDriver::ServerReplicateActors)(NewNetDriver->ReplicationDriver->Vtable[/*3.5/4.1: 0x54*/ /*0x54*/0x56]);
                    MessageBoxA(0, "Initialized ServerReplicateActors", std::to_string((uintptr_t)Functions::ReplicationDriver::ServerReplicateActors).c_str(), 0);

                    //4C 8B DC 55 49 8D AB 78 FE FF FF 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 00 01 00 00 49 89 5B 18

                    //Functions::NetDriver::TickFlush = Utils::FindPattern<decltype(Functions::NetDriver::TickFlush)>("4C 8B DC 55 49 8D AB 78 FE FF FF 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 00 01 00 00 49 89 5B 18");
                    
                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());
                    DetourAttach(&(void*&)Functions::NetDriver::TickFlush, Hooks::TickFlush);
                    DetourTransactionCommit();

                    auto ClassRepNodePolicies = GetClassRepNodePolicies(NewNetDriver->ReplicationDriver);

                    for (auto&& Pair : ClassRepNodePolicies)
                    {
                        auto key = Pair.Key().ResolveObjectPtr();

                        if (key == AFortInventory::StaticClass())
                            Pair.Value() = EClassRepNodeMapping::RelevantAllConnections;
                        else if (key == AFortQuickBars::StaticClass())
                            Pair.Value() = EClassRepNodeMapping::RelevantAllConnections;
                    }


                    GetWorld()->NetDriver = NewNetDriver;
                    GetWorld()->LevelCollections[0].NetDriver = GetWorld()->NetDriver;
                    GetWorld()->LevelCollections[1].NetDriver = GetWorld()->NetDriver;
            
                    MessageBoxA(0, "stored shits in variables", std::to_string((uintptr_t)GetWorld()->NetDriver).c_str(), 0);

                    auto GameState = (AFortGameStateAthena*)GetWorld()->GameState;

                    ((AFortGameModeAthena*)GetWorld()->AuthorityGameMode)->GameSession->MaxPlayers = 100;

                    GameState->bReplicatedHasBegunPlay = true;
                    GameState->OnRep_ReplicatedHasBegunPlay();

                    GetWorld()->NetworkManager->NetCullDistanceSquared += (GetWorld()->NetworkManager->NetCullDistanceSquared * 2.5);

                    
                    Functions::PlayerController::ReplicateMoveToServer = Utils::FindPattern<decltype(Functions::PlayerController::ReplicateMoveToServer)>("4C 89 B4 24 ?? ?? ?? ?? 4D 8B B7 ?? ?? ?? ??");
                    MessageBoxA(0, "Defined ReplicateMoveToServer", std::to_string((uintptr_t)Functions::PlayerController::ReplicateMoveToServer).c_str(), 0);

                    Functions::PlayerController::ServerAcknowledgePosession = Utils::FindPattern<decltype(Functions::PlayerController::ServerAcknowledgePosession)>("48 89 5C 24 ? 57 48 83 EC 20 48 8B 19 48 8B F9 48 89 54 24 ? 48 8B 15 ? ? ? ? E8 ? ? ? ? 48 8B D0 4C 8D 44 24 ? 48 8B CF FF 93 ? ? ? ? 48 8B 5C 24 ? 48 83 C4 20 5F C3");
                    MessageBoxA(0, "Defined ServerAcknowledgePosession", std::to_string((uintptr_t)Functions::PlayerController::ServerAcknowledgePosession).c_str(), 0);

                    /*MessageBoxA(0, "Inited ReplicateMoveToServer & ServerAcknowledgePossession", "Hooked!", 0);
                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());
                    DetourAttach(&(void*&)Functions::PlayerController::ReplicateMoveToServer, ReplicateMoveToServer_Hook);
                    DetourAttach(&(void*&)Functions::PlayerController::ServerAcknowledgePosession, ServerAcknowledgePossession_Hook);
                    DetourTransactionCommit();*/

                

                    auto InProgress_Name = USL->STATIC_Conv_StringToName(L"InProgress");
                    ((AFortGameModeAthena*)GetWorld()->AuthorityGameMode)->MatchState = InProgress_Name;
                    ((AFortGameModeAthena*)GetWorld()->AuthorityGameMode)->K2_OnSetMatchState(InProgress_Name);
                    GameState->bReplicatedHasBegunPlay = true;
                    GameState->OnRep_ReplicatedHasBegunPlay();
                    ((AFortGameModeAthena*)GetWorld()->AuthorityGameMode)->StartMatch();
                    GetWorld()->NetworkManager->NetCullDistanceSquared += (GetWorld()->NetworkManager->NetCullDistanceSquared * 2.5);

                    /*
                    HostBeacon = SpawnActor<AFortOnlineBeaconHost>();
                    HostBeacon->ListenPort = 7776;
                    auto bInitBeacon = Functions::OnlineBeaconHost::InitHost(HostBeacon);

                    HostBeacon->NetDriverName = FName(282);
                    HostBeacon->NetDriver->NetDriverName = FName(282);
                    HostBeacon->NetDriver->World = GetWorld();

                    
                    FString Error;
                    auto InURL = FURL();
                    InURL.Port = 7777;

                    Functions::NetDriver::InitListen(HostBeacon->NetDriver, GetWorld(), InURL, true, Error);
                    Functions::ReplicationDriver::ServerReplicateActors = decltype(Functions::ReplicationDriver::ServerReplicateActors)(HostBeacon->NetDriver->ReplicationDriver->Vtable[0x53]);

                    auto ClassRepNodePolicies = GetClassRepNodePolicies(HostBeacon->NetDriver->ReplicationDriver);

                    for (auto&& Pair : ClassRepNodePolicies)
                    {
                        auto key = Pair.Key().ResolveObjectPtr();

                        if (key == AFortInventory::StaticClass())
                            Pair.Value() = EClassRepNodeMapping::RelevantAllConnections;
                        else if (key == AFortQuickBars::StaticClass())
                            Pair.Value() = EClassRepNodeMapping::RelevantAllConnections;
                    }
                    

                    GetWorld()->NetDriver = HostBeacon->NetDriver;
                    GetWorld()->LevelCollections[0].NetDriver = HostBeacon->NetDriver;
                    GetWorld()->LevelCollections[1].NetDriver = HostBeacon->NetDriver;

                    auto GameState = (AAthena_GameState_C*)GetWorld()->GameState;

                    ((AAthena_GameMode_C*)GetWorld()->AuthorityGameMode)->GameSession->MaxPlayers = 100;

                    Functions::OnlineBeacon::PauseBeaconRequests(HostBeacon, false);
                    */

                    bListening = true;
                    std::cout << "Server is listening, people can join\n";
                    return;
                }
            }

            for (int i = 0; i < toHook.size(); i++)
            {
                if (Function == toHook[i])
                {
                    if (toCall[i](Object, Parameters))
                    {
                        return;
                    }
                    break;
                }
            }
        }

        return oProcessEvent(Object, Function, Parameters);
    }
}
