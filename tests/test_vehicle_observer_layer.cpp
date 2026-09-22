/*
 * test_vehicle_observer_layer.cpp -- production wiring guards for
 * vehicle and observer scene-layer migration.
 *
 * @SYNC src/game/bondbike.c
 * @SYNC src/game/forgemode.c
 * @SYNC port/src/spectator.c
 * @SYNC port/src/inputlayer.c
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string functionBlock(const std::string &text, const std::string &name)
{
    const std::string needle = name + "(";
    size_t start = text.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    size_t brace = text.find('{', start);
    if (brace == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (size_t i = brace; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

} /* namespace */

TEST_CASE("vehicle layer wiring: hoverbike routes through scene layer", "[input][vehicle][static]")
{
    const std::string inputlayer = readTextFile("port/src/inputlayer.c");
    const std::string bondbike = readTextFile("src/game/bondbike.c");

    REQUIRE_FALSE(inputlayer.empty());
    REQUIRE_FALSE(bondbike.empty());

    REQUIRE(inputlayer.find("s_VehicleDriverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_VEHICLE_ACCELERATE") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_VEHICLE_EXIT") != std::string::npos);

    const std::string vehiclePush = functionBlock(inputlayer, "onVehicleDriverPush");
    REQUIRE_FALSE(vehiclePush.empty());
    REQUIRE(vehiclePush.find("imcVehicleMount()") != std::string::npos);
    REQUIRE(vehiclePush.find("actionmapFlushGameplayState()") != std::string::npos);
    REQUIRE(vehiclePush.find("actionmapFlushActionSet(s_VehicleDriverActionSet") != std::string::npos);

    const std::string vehiclePop = functionBlock(inputlayer, "onVehicleDriverPop");
    REQUIRE_FALSE(vehiclePop.empty());
    REQUIRE(vehiclePop.find("imcVehicleDismount()") != std::string::npos);
    REQUIRE(vehiclePop.find("actionmapFlushActionSet(s_VehicleDriverActionSet") != std::string::npos);

    REQUIRE(inputlayer.find(".action_set            = s_VehicleDriverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find(".on_push               = onVehicleDriverPush") != std::string::npos);
    REQUIRE(inputlayer.find(".on_pop                = onVehicleDriverPop") != std::string::npos);
    REQUIRE(inputlayer.find(".on_abort              = onVehicleDriverAbort") != std::string::npos);
    REQUIRE(inputlayer.find(".imc                   = &g_ImcVehicle") != std::string::npos);

    const std::string init = functionBlock(bondbike, "bbikeInit");
    const std::string exit = functionBlock(bondbike, "bbikeExit");
    const std::string exitLocal = functionBlock(bondbike, "bbikeExitLocal");
    REQUIRE_FALSE(init.empty());
    REQUIRE_FALSE(exit.empty());
    REQUIRE_FALSE(exitLocal.empty());
    REQUIRE(init.find("sceneFire(SCENE_EVENT_VEHICLE_BOARD") != std::string::npos);
    REQUIRE(init.find("imcVehicleMount()") == std::string::npos);
    REQUIRE(exitLocal.find("sceneFire(SCENE_EVENT_VEHICLE_DISMOUNT") != std::string::npos);
    REQUIRE(exitLocal.find("imcVehicleDismount()") == std::string::npos);
}

TEST_CASE("hoverbike operability keeps one validated mount transaction", "[vehicle][input][network][static]")
{
    const std::string propobj = readTextFile("src/game/propobj.c");
    const std::string bondbike = readTextFile("src/game/bondbike.c");
    const std::string bondgrab = readTextFile("src/game/bondgrab.c");
    const std::string bondmove = readTextFile("src/game/bondmove.c");
    const std::string netmsg = readTextFile("port/src/net/netmsg.c");
    const std::string netmsgHeader = readTextFile("port/include/net/netmsg.h");
    const std::string net = readTextFile("port/src/net/net.c");
    const std::string netHeader = readTextFile("port/include/net/net.h");
    const std::string main = readTextFile("port/src/main.c");
    const std::string smoke = readTextFile("tools/smoke-verify/tests/vehicle_flow.json");

    REQUIRE_FALSE(propobj.empty());
    REQUIRE_FALSE(bondbike.empty());
    REQUIRE_FALSE(bondgrab.empty());
    REQUIRE_FALSE(bondmove.empty());
    REQUIRE_FALSE(netmsg.empty());
    REQUIRE_FALSE(netmsgHeader.empty());
    REQUIRE_FALSE(net.empty());
    REQUIRE_FALSE(netHeader.empty());
    REQUIRE_FALSE(main.empty());
    REQUIRE_FALSE(smoke.empty());

    const std::string mount = functionBlock(propobj, "currentPlayerTryMountHoverbike");
    const std::string committed = functionBlock(propobj, "currentPlayerApplyHoverbikeState");
    const std::string canApply = functionBlock(propobj, "currentPlayerCanApplyHoverbikeState");
    const std::string interact = functionBlock(propobj, "propobjInteract");
    const std::string apply = functionBlock(bondbike, "bbikeApplyMoveData");
    const std::string init = functionBlock(bondbike, "bbikeInit");
    const std::string exit = functionBlock(bondbike, "bbikeExit");
    const std::string exitLocal = functionBlock(bondbike, "bbikeExitLocal");
    const std::string canDismount = functionBlock(bondbike, "bbikeCanDismount");
    const std::string tryDismount = functionBlock(bondbike, "bbikeTryDismount");
    const std::string bikeActivate = functionBlock(bondbike, "bbikeHandleActivate");
    const std::string grabActivate = functionBlock(bondgrab, "bgrabHandleActivate");
    const std::string remoteInput = functionBlock(bondmove, "bmoveProcessRemoteInput");
    const std::string mode = functionBlock(bondmove, "bmoveSetMode");
    const std::string svcWrite = functionBlock(netmsg, "netmsgSvcPropUseWrite");
    const std::string svcRead = functionBlock(netmsg, "netmsgSvcPropUseRead");
    const std::string vehicleCanWrite = functionBlock(netmsg, "netmsgSvcVehicleStateCanWrite");
    const std::string vehiclePrepare = functionBlock(netmsg, "netmsgSvcVehicleStatePrepare");
    const std::string vehicleCommit = functionBlock(netmsg, "netmsgSvcVehicleStateCommit");
    const std::string vehicleAbort = functionBlock(netmsg, "netmsgSvcVehicleStateAbort");
    const std::string vehicleWrite = functionBlock(netmsg, "netmsgSvcVehicleStateWrite");
    const std::string vehicleRead = functionBlock(netmsg, "netmsgSvcVehicleStateRead");
    const std::string moveRead = functionBlock(netmsg, "netmsgClcMoveRead");
    const std::string sequenceOrder = functionBlock(netmsg, "netmsgVehicleSequenceIsNewer");
    REQUIRE_FALSE(mount.empty());
    REQUIRE_FALSE(committed.empty());
    REQUIRE_FALSE(canApply.empty());
    REQUIRE_FALSE(interact.empty());
    REQUIRE_FALSE(apply.empty());
    REQUIRE_FALSE(init.empty());
    REQUIRE_FALSE(exit.empty());
    REQUIRE_FALSE(exitLocal.empty());
    REQUIRE_FALSE(canDismount.empty());
    REQUIRE_FALSE(tryDismount.empty());
    REQUIRE_FALSE(bikeActivate.empty());
    REQUIRE_FALSE(grabActivate.empty());
    REQUIRE_FALSE(remoteInput.empty());
    REQUIRE_FALSE(mode.empty());
    REQUIRE_FALSE(svcWrite.empty());
    REQUIRE_FALSE(svcRead.empty());
    REQUIRE_FALSE(vehicleCanWrite.empty());
    REQUIRE_FALSE(vehiclePrepare.empty());
    REQUIRE_FALSE(vehicleCommit.empty());
    REQUIRE_FALSE(vehicleAbort.empty());
    REQUIRE_FALSE(vehicleWrite.empty());
    REQUIRE_FALSE(vehicleRead.empty());
    REQUIRE_FALSE(moveRead.empty());
    REQUIRE_FALSE(sequenceOrder.empty());

    REQUIRE(mount.find("prop->type != PROPTYPE_OBJ") != std::string::npos);
    REQUIRE(mount.find("currentPlayerCanApplyHoverbikeState(prop, true)") != std::string::npos);
    REQUIRE(mount.find("netmsgSvcVehicleStatePrepare") != std::string::npos);
    REQUIRE(mount.find("netmsgSvcVehicleStateCommit") != std::string::npos);
    REQUIRE(mount.find("netmsgSvcVehicleStateAbort") != std::string::npos);
    REQUIRE(mount.find("player->bondmovemode == MOVEMODE_BIKE") != std::string::npos);
    REQUIRE(mount.find("xdiff * xdiff + zdiff * zdiff >= range * range") != std::string::npos);
    REQUIRE(mount.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
    REQUIRE(mount.find("currentPlayerApplyHoverbikeState(prop, true)") != std::string::npos);
    REQUIRE(mount.find("currentPlayerApplyHoverbikeState(prop, false)") == std::string::npos);
    REQUIRE(committed.find("prop->pos") == std::string::npos);
    REQUIRE(committed.find("atan2f") == std::string::npos);
    REQUIRE(committed.find("cdTest") == std::string::npos);
    REQUIRE(canApply.find("assetRuntimeVehicleAllows(obj->modelnum, \"mount\")") != std::string::npos);
    REQUIRE(canApply.find("sceneVehicleDriverCanBoard()") != std::string::npos);
    REQUIRE(canApply.find("currentPlayerHoverbikeOwner") != std::string::npos);
    REQUIRE(canApply.find("currentPlayerHoverbikeOwnedByOther") != std::string::npos);
    REQUIRE(mount.find("player->pcinteractusekind == 2") != std::string::npos);
    REQUIRE(mount.find("player->pcinteractusekind == 0") != std::string::npos);
    REQUIRE(mount.find("player->isremote") != std::string::npos);
    REQUIRE(mount.find("bmoveIsLocalControllerVehicleIntent()") != std::string::npos);
    REQUIRE(mount.find("optionsGetControlMode(g_Vars.currentplayerstats->mpindex)") == std::string::npos);
    REQUIRE(committed.find("currentPlayerCanApplyHoverbikeState(prop, mounted)") != std::string::npos);
    REQUIRE(canApply.find("obj->prop != prop") != std::string::npos);
    REQUIRE(committed.find("player->hoverbike = previousbike") != std::string::npos);
    REQUIRE(propobj.find("currentPlayerHoverbikeOwner") != std::string::npos);
    REQUIRE(propobj.find("owner && owner != player") != std::string::npos);

    const size_t mountAttempt = interact.find("currentPlayerTryMountHoverbike(prop)");
    const size_t grabFallback = interact.find("bmoveGrabProp(prop)");
    REQUIRE(mountAttempt != std::string::npos);
    REQUIRE(grabFallback != std::string::npos);
    REQUIRE(mountAttempt < grabFallback);
    REQUIRE(interact.find("currentPlayerTryMountHoverbike(prop)", mountAttempt + 1) == std::string::npos);
    REQUIRE(interact.find("optionsGetControlMode(g_Vars.currentplayerstats->mpindex)") == std::string::npos);
    REQUIRE(interact.find("The candidate owns the explicit PC/controller intent invariant") != std::string::npos);

    REQUIRE(apply.find("actionValue(actionPlayer, ACTION_VEHICLE_ACCELERATE)") != std::string::npos);
    REQUIRE(apply.find("|| vHandbrake > 0.0f") != std::string::npos);
    REQUIRE(apply.find("player->isremote") != std::string::npos);
    REQUIRE(apply.find("goto apply_transported_vehicle_data") != std::string::npos);
    REQUIRE(apply.find("targetForward = player->speedforwards * (1.0f - vHandbrake)") != std::string::npos);
    REQUIRE(apply.find("targetForward = vAccel - vBrake") != std::string::npos);
    REQUIRE(apply.find("targetForward = -vBrake") == std::string::npos);
    REQUIRE(apply.find("ACTION_VEHICLE_EXIT") != std::string::npos);
    REQUIRE(apply.find("s_VehicleExitIntentPending") != std::string::npos);
    REQUIRE(apply.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
    REQUIRE(apply.find("if (g_Vars.currentplayer->walkinitmove)") != std::string::npos);
    REQUIRE(init.find("!player || !player->prop") != std::string::npos);
    REQUIRE(init.find("bikebase->prop != bikeprop") != std::string::npos);
    REQUIRE(init.find("sceneFire(SCENE_EVENT_VEHICLE_BOARD") != std::string::npos);
    REQUIRE(exitLocal.find("OBJHFLAG_MOUNTED") != std::string::npos);
    REQUIRE(exitLocal.find("sceneFire(SCENE_EVENT_VEHICLE_DISMOUNT") != std::string::npos);
    REQUIRE(bondbike.find("bool bbikeTryDismount") != std::string::npos);
    REQUIRE(canDismount.find("g_NetStageEpoch == 0") != std::string::npos);
    REQUIRE(canDismount.find("currentPlayerCanApplyHoverbikeState(bikeprop, false)") != std::string::npos);
    REQUIRE(tryDismount.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
    REQUIRE(tryDismount.find("bbikeExitLocal()") == std::string::npos);
    REQUIRE(tryDismount.find("bmoveSetMode(MOVEMODE_WALK)") != std::string::npos);
    REQUIRE(tryDismount.find("player->hoverbike = NULL") == std::string::npos);
    REQUIRE(tryDismount.find("netmsgSvcVehicleStateCommit") >
        tryDismount.find("bmoveSetMode(MOVEMODE_WALK)"));
    REQUIRE(bondbike.find("netmsgSvcVehicleStatePrepare") != std::string::npos);
    REQUIRE(bondbike.find("netmsgSvcVehicleStateCommit") != std::string::npos);
    REQUIRE(bondbike.find("netmsgSvcVehicleStateAbort") != std::string::npos);
    REQUIRE(exit.find("netmsgSvcVehicleState") == std::string::npos);
    REQUIRE(bikeActivate.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
    REQUIRE(bikeActivate.find("bool transportedintent") != std::string::npos);
    REQUIRE(bikeActivate.find("authoritativeTransportedExit") != std::string::npos);
    REQUIRE(bikeActivate.find("g_NetMode == NETMODE_SERVER") != std::string::npos);
    REQUIRE(bikeActivate.find("authoritativeTransportedExit") <
        bikeActivate.find("_bbContrMode == CONTROLMODE_PC"));
    REQUIRE(bikeActivate.find("clientLocalIntent") != std::string::npos);
    REQUIRE(bikeActivate.find("localIntentAccepted") != std::string::npos);
    REQUIRE(bikeActivate.find("bbikeTryDismountAngle") <
        bikeActivate.find("s_VehicleExitIntentPending"));
    REQUIRE(bikeActivate.find("s_VehicleExitIntentPending") <
        bikeActivate.find("bbikeTryDismount()"));
    REQUIRE(bikeActivate.find("s_VehicleExitIntentPending") != std::string::npos);
    REQUIRE(bikeActivate.find("return") != std::string::npos);
    REQUIRE(remoteInput.find("!handled && (inmove->ucmd & UCMD_ACTIVATE)") != std::string::npos);
    REQUIRE(remoteInput.find("UCMD_VEHICLE_PC_INTENT") != std::string::npos);
    REQUIRE(remoteInput.find("pcVehicleIntent") != std::string::npos);
    REQUIRE(remoteInput.find("pl->pcinteractusekind = pcVehicleIntent ? 2 : 0") != std::string::npos);
    REQUIRE(remoteInput.find("pl->activatetimelast = pl->activatetimethis") != std::string::npos);
    REQUIRE(remoteInput.find("bbikeHandleActivate(pcVehicleIntent)") != std::string::npos);
    REQUIRE(remoteInput.find("bbikeHandleActivate(true)") == std::string::npos);
    REQUIRE(remoteInput.find("pl->client->inmovetick = inmove->tick") != std::string::npos);
    REQUIRE(remoteInput.find("pl->pcinteractusekind = 0") != std::string::npos);
    REQUIRE(remoteInput.find("const bool handled") <
        remoteInput.find("!handled && (inmove->ucmd & UCMD_ACTIVATE)"));
    REQUIRE(remoteInput.find("bbikeHandleActivate(pcVehicleIntent)") <
        remoteInput.find("pl->client->inmovetick = inmove->tick"));
    REQUIRE(bondmove.find("bondactivateorreload &= ~JO_ACTION_ACTIVATE") != std::string::npos);
    REQUIRE(bondmove.find("if (vehicleExitIntent)") != std::string::npos);
    REQUIRE(bondmove.find("g_Vars.currentplayer->ucmd |= UCMD_ACTIVATE") != std::string::npos);
    REQUIRE(bondmove.find("g_Vars.currentplayer->ucmd |= UCMD_VEHICLE_PC_INTENT") != std::string::npos);
    REQUIRE(apply.find("player->pcinteractusekind = 2") != std::string::npos);
    REQUIRE(apply.find("bbikeTryDismountAngle") < apply.find("s_VehicleExitIntentPending"));
    REQUIRE(mode.find("sceneVehicleDriverCanBoard()") != std::string::npos);
    REQUIRE(mode.find("!player->isremote") != std::string::npos);
    REQUIRE(mode.find("bwalkInit()") != std::string::npos);
    REQUIRE(mode.find("leavingbike") != std::string::npos);
    REQUIRE(init.find("sceneVehicleDriverCanBoard()") != std::string::npos);
    REQUIRE(init.find("if (!player->isremote)") != std::string::npos);
    REQUIRE(grabActivate.find("g_NetMode == NETMODE_CLIENT") != std::string::npos);
    REQUIRE(grabActivate.find("OBJTYPE_HOVERBIKE") != std::string::npos);
    REQUIRE(init.find("sceneFire(SCENE_EVENT_VEHICLE_BOARD") < init.find("player->bondmovemode = MOVEMODE_BIKE"));
    REQUIRE(init.find("sceneFire(SCENE_EVENT_VEHICLE_BOARD") < init.find("objFreeEmbedmentOrProjectile"));
    REQUIRE(bondbike.find("s_LastVehicleInputProp[MAX_PLAYERS]") != std::string::npos);
    REQUIRE(bondbike.find("s_LastVehicleInputLogFrame[MAX_PLAYERS]") != std::string::npos);
    REQUIRE(bondbike.find("lastVehicleInputLogFrame") == std::string::npos);
    REQUIRE(bondbike.find("bbikeSetVehicleStatePublishSuppressed") == std::string::npos);

    REQUIRE(svcWrite.find("OBJTYPE_HOVERBIKE") != std::string::npos);
    REQUIRE(svcWrite.find("return 1") != std::string::npos);
    REQUIRE(svcRead.find("currentPlayerTryMountHoverbike") == std::string::npos);
    REQUIRE(svcRead.find("rejected raw SVC_PROP_USE for hoverbike") != std::string::npos);
    REQUIRE(svcWrite.find("prop->obj->type == OBJTYPE_HOVERBIKE") != std::string::npos);
    REQUIRE(vehicleCanWrite.find("g_NetMode != NETMODE_SERVER") != std::string::npos);
    REQUIRE(vehiclePrepare.find("SVC_PROP_VEHICLE_STATE") != std::string::npos);
    REQUIRE(vehiclePrepare.find("NET_VEHICLE_STATE_WIRE_VERSION") != std::string::npos);
    REQUIRE(vehiclePrepare.find("NET_VEHICLE_STATE_WIRE_BYTES") != std::string::npos);
    REQUIRE(vehiclePrepare.find("prop->syncid") != std::string::npos);
    REQUIRE(vehiclePrepare.find("g_NetStageEpoch") != std::string::npos);
    REQUIRE(vehiclePrepare.find("netmsgVehicleSequenceCandidate") != std::string::npos);
    REQUIRE(vehiclePrepare.find("dst->wp += encoded.wp") != std::string::npos);
    REQUIRE(vehicleCommit.find("memcpy") != std::string::npos);
    REQUIRE(vehicleCommit.find("netPropMarkDirty") != std::string::npos);
    REQUIRE(vehicleAbort.find("dst->wp = txn->write_offset") != std::string::npos);
    REQUIRE(vehicleWrite.find("netmsgSvcVehicleStatePrepare") != std::string::npos);
    REQUIRE(vehicleWrite.find("netmsgSvcVehicleStateCommit") != std::string::npos);
    REQUIRE(vehicleWrite.find("g_NetTick") == std::string::npos);
    REQUIRE(vehicleRead.find("src->error") != std::string::npos);
    REQUIRE(vehicleRead.find("netbufReadLeft(src)") != std::string::npos);
    REQUIRE(vehicleRead.find("if (src->error || g_NetMode != NETMODE_CLIENT") != std::string::npos);
    REQUIRE(vehicleRead.find("wire_version != NET_VEHICLE_STATE_WIRE_VERSION") != std::string::npos);
    REQUIRE(vehicleRead.find("actor_client_id == NET_NULL_CLIENT") != std::string::npos);
    REQUIRE(vehicleRead.find("playernum >= MAX_PLAYERS") != std::string::npos);
    REQUIRE(vehicleRead.find("srccl != g_NetLocalClient") != std::string::npos);
    REQUIRE(vehicleRead.find("actor->id != actor_client_id") != std::string::npos);
    REQUIRE(vehicleRead.find("actor->player != g_Vars.players[playernum]") != std::string::npos);
    REQUIRE(vehicleRead.find("actor->player->client != actor") != std::string::npos);
    REQUIRE(vehicleRead.find("netSyncIdLookup(prop_syncid)") != std::string::npos);
    REQUIRE(vehicleRead.find("prop->obj->type != OBJTYPE_HOVERBIKE") != std::string::npos);
    REQUIRE(vehicleRead.find("stage_epoch != g_NetStageEpoch") != std::string::npos);
    REQUIRE(vehicleRead.find("ignored stale vehicle state") != std::string::npos);
    REQUIRE(vehicleRead.find("Reliable duplicates are valid") != std::string::npos);
    REQUIRE(vehicleRead.find("sequence == seen->sequence") != std::string::npos);
    REQUIRE(vehicleRead.find("netmsgVehicleSequenceIsNewer") != std::string::npos);
    REQUIRE(vehicleRead.find("sequence == 0") != std::string::npos);
    REQUIRE(vehicleRead.find("g_NetTick") == std::string::npos);
    REQUIRE(vehicleRead.find("seen->state == state && seen->prop_syncid == prop_syncid") != std::string::npos);
    REQUIRE(vehicleRead.find("seen->actor_client_id != actor_client_id") != std::string::npos);
    REQUIRE(vehicleRead.find("currentPlayerApplyHoverbikeState(prop, mounted)") != std::string::npos);
    REQUIRE(moveRead.find("UCMD_VEHICLE_PC_INTENT") != std::string::npos);
    REQUIRE(moveRead.find("UCMD_ACTIVATE") != std::string::npos);
    REQUIRE(moveRead.find("rejected CLC_MOVE vehicle PC intent") != std::string::npos);
    REQUIRE(sequenceOrder.find("candidate != prior") != std::string::npos);
    REQUIRE(sequenceOrder.find("0x80000000u") != std::string::npos);
    REQUIRE(netmsg.find("independent of g_NetTick") != std::string::npos);
    REQUIRE(netmsgHeader.find("SVC_PROP_VEHICLE_STATE 0x39") != std::string::npos);
    /* Vehicle state entered the wire at v59; later compatible protocol
     * revisions must retain this contract without pinning the entire game. */
    const std::string protocolDefine = "#define NET_PROTOCOL_VER ";
    const size_t protocolPos = netHeader.find(protocolDefine);
    REQUIRE(protocolPos != std::string::npos);
    REQUIRE(std::stoi(netHeader.substr(protocolPos + protocolDefine.size())) >= 59);
    REQUIRE(netHeader.find("UCMD_VEHICLE_PC_INTENT") != std::string::npos);
    REQUIRE(netHeader.find("UCMD_VEHICLE_PC_INTENT)") != std::string::npos);
    REQUIRE(bondmove.find("g_Vars.currentplayer->pcinteractusekind == 2") != std::string::npos);
    REQUIRE(netmsg.find("0x80000000u") != std::string::npos);
    REQUIRE(netmsg.find("netmsgVehicleStateStageReset") != std::string::npos);
    REQUIRE(net.find("netmsgVehicleStateStageReset()") != std::string::npos);
    const size_t serverVehicleDispatch = net.find("case SVC_PROP_VEHICLE_STATE");
    const size_t clientVehicleDispatch = net.find("case SVC_PROP_VEHICLE_STATE", serverVehicleDispatch + 1);
    REQUIRE(serverVehicleDispatch != std::string::npos);
    REQUIRE(clientVehicleDispatch != std::string::npos);
    REQUIRE(main.find("debug_candidate_no_propfind_los") != std::string::npos);
    REQUIRE(main.find("g_Vars.players[0]->pcinteractusekind = 2") != std::string::npos);
    REQUIRE(main.find("g_Vars.lvframenum > 120") == std::string::npos);
    REQUIRE(main.find("g_BootMountBikeStage != g_Vars.stagenum") != std::string::npos);
    REQUIRE(main.find("target.x += sinf(turn + M_BADPI * 0.5f) * 150.0f") != std::string::npos);
    REQUIRE(main.find("chrMoveToPos(g_Vars.players[0]->prop->chr") != std::string::npos);
    REQUIRE(main.find("turn, false") != std::string::npos);
    REQUIRE(smoke.find("ACTION_VEHICLE_ACCELERATE") != std::string::npos);
    REQUIRE(smoke.find("VEHICLE: mounted") != std::string::npos);
    REQUIRE(smoke.find("VEHICLE: dismounted") != std::string::npos);
    REQUIRE(smoke.find("propFindForInteract LOS/collision") != std::string::npos);
    REQUIRE(smoke.find("0x19") == std::string::npos);
    REQUIRE(smoke.find("--launch-mission") != std::string::npos);
    REQUIRE(smoke.find("base:escape") != std::string::npos);
    REQUIRE(smoke.find("vehicle_driver_ready") != std::string::npos);
    REQUIRE(smoke.find("SVC_PROP_USE") == std::string::npos);
    REQUIRE(smoke.find("No listen-authority/two-client or V-009 proof is claimed.") != std::string::npos);
    REQUIRE(smoke.find("deterministic remount failure remains an explicit risk") != std::string::npos);
}

TEST_CASE("observer layer wiring: forge and spectator fire observer events", "[input][observer][static]")
{
    const std::string inputlayer = readTextFile("port/src/inputlayer.c");
    const std::string sceneHeader = readTextFile("port/include/scene.h");
    const std::string scene = readTextFile("port/src/scene.c");
    const std::string forge = readTextFile("src/game/forgemode.c");
    const std::string spectator = readTextFile("port/src/spectator.c");

    REQUIRE_FALSE(inputlayer.empty());
    REQUIRE_FALSE(sceneHeader.empty());
    REQUIRE_FALSE(scene.empty());
    REQUIRE_FALSE(forge.empty());
    REQUIRE_FALSE(spectator.empty());

    REQUIRE(sceneHeader.find("#define SCENE_OBSERVER_SOURCE_SPECTATOR  3") != std::string::npos);

    REQUIRE(inputlayer.find("s_ObserverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_FORGE_TOGGLE") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_FORGE_TAB_NEXT") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_OBSERVER_SUBSET_PREV") != std::string::npos);
    REQUIRE(inputlayer.find("ACTION_OBSERVER_STOP") != std::string::npos);
    REQUIRE(inputlayer.find(".action_set            = s_ObserverActionSet") != std::string::npos);
    REQUIRE(inputlayer.find(".on_push               = onObserverPush") != std::string::npos);
    REQUIRE(inputlayer.find(".on_pop                = onObserverPop") != std::string::npos);
    REQUIRE(inputlayer.find(".on_abort              = onObserverAbort") != std::string::npos);
    REQUIRE(inputlayer.find(".imc                   = &g_ImcObserver") != std::string::npos);

    const std::string observerPop = functionBlock(inputlayer, "onObserverPop");
    const std::string observerPush = functionBlock(inputlayer, "onObserverPush");
    REQUIRE_FALSE(observerPush.empty());
    REQUIRE_FALSE(observerPop.empty());
    REQUIRE(observerPush.find("SCENE_OBSERVER_SOURCE_SPECTATOR") != std::string::npos);
    REQUIRE(observerPush.find("imcActivate(&g_ImcObserver)") != std::string::npos);
    /* s036-06: push must stash the source so pop/abort can release symmetrically. */
    REQUIRE(observerPush.find("s_ObserverActiveSource") != std::string::npos);
    REQUIRE(observerPop.find("actionmapFlushActionSet(s_ObserverActionSet") != std::string::npos);
    /* s036-06: pop only deactivates ImcObserver for SPECTATOR source.
     * Forge IMCs are owned by forge transition code (forgeTransitionToInactive
     * in src/game/forgemode.c), not by observer pop. */
    REQUIRE(observerPop.find("SCENE_OBSERVER_SOURCE_SPECTATOR") != std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcObserver)") != std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcForge)") == std::string::npos);
    REQUIRE(observerPop.find("imcDeactivate(&g_ImcForgeSession)") == std::string::npos);
    REQUIRE(scene.find("s_ObserverPayload.source = s_ObserverSource") != std::string::npos);
    REQUIRE(scene.find("inputLayerPush(&g_LayerObserver, &s_ObserverPayload)") != std::string::npos);

    const std::string forgeEnter = functionBlock(forge, "forgeObserverLayerEnter");
    const std::string forgeExit = functionBlock(forge, "forgeObserverLayerExit");
    REQUIRE_FALSE(forgeEnter.empty());
    REQUIRE_FALSE(forgeExit.empty());
    REQUIRE(forgeEnter.find("SCENE_OBSERVER_SOURCE_FORGE") != std::string::npos);
    REQUIRE(forgeEnter.find("sceneFire(SCENE_EVENT_OBSERVER_ENTER") != std::string::npos);
    REQUIRE(forgeExit.find("sceneFire(SCENE_EVENT_OBSERVER_EXIT") != std::string::npos);
    REQUIRE(forge.find("forgeObserverLayerEnter(reason)") != std::string::npos);
    REQUIRE(forge.find("forgeObserverLayerExit(reason)") != std::string::npos);

    const std::string specEnter = functionBlock(spectator, "spectatorObserverLayerEnter");
    const std::string specExit = functionBlock(spectator, "spectatorObserverLayerExit");
    REQUIRE_FALSE(specEnter.empty());
    REQUIRE_FALSE(specExit.empty());
    REQUIRE(specEnter.find("SCENE_OBSERVER_SOURCE_SPECTATOR") != std::string::npos);
    REQUIRE(specEnter.find("sceneFire(SCENE_EVENT_OBSERVER_ENTER") != std::string::npos);
    REQUIRE(specExit.find("sceneFire(SCENE_EVENT_OBSERVER_EXIT") != std::string::npos);

    const std::string beginLive = functionBlock(spectator, "spectatorBeginLive");
    const std::string beginTheater = functionBlock(spectator, "spectatorBeginTheater");
    const std::string stop = functionBlock(spectator, "void spectatorStop");
    REQUIRE_FALSE(beginLive.empty());
    REQUIRE_FALSE(beginTheater.empty());
    REQUIRE_FALSE(stop.empty());
    REQUIRE(beginLive.find("spectatorObserverLayerEnter()") != std::string::npos);
    REQUIRE(beginTheater.find("spectatorObserverLayerEnter()") != std::string::npos);
    REQUIRE(stop.find("spectatorObserverLayerExit()") != std::string::npos);
}

TEST_CASE("observer action map: spectator overlay uses observer actions", "[input][observer][spectator][static]")
{
    const std::string actionHeader = readTextFile("port/include/actionmap.h");
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string spectator = readTextFile("port/fast3d/pdgui_spectator.cpp");
    const std::string glyphs = readTextFile("port/fast3d/pdgui_glyphs.cpp");
    const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");

    REQUIRE_FALSE(actionHeader.empty());
    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(spectator.empty());
    REQUIRE_FALSE(glyphs.empty());
    REQUIRE_FALSE(mainmenu.empty());

    REQUIRE(actionHeader.find("ACTION_OBSERVER_SUBSET_PREV") != std::string::npos);
    REQUIRE(actionHeader.find("ACTION_OBSERVER_ASCEND") != std::string::npos);
    REQUIRE(actionHeader.find("extern InputMappingContext g_ImcObserver") != std::string::npos);
    REQUIRE(actionmap.find("InputMappingContext g_ImcObserver") != std::string::npos);
    REQUIRE(actionmap.find("setupObserverDefaults") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_SUBSET_PREV,   VKL_PAGEUP)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_FREEFLY,       VKL_R)") != std::string::npos);
    REQUIRE(actionmap.find("addBind(imc, ACTION_OBSERVER_STOP,          VK_ESCAPE)") != std::string::npos);
    REQUIRE(actionmap.find("ctx == &g_ImcObserver") != std::string::npos);

    REQUIRE(spectator.find("#include \"actionmap.h\"") != std::string::npos);
    REQUIRE(spectator.find("actionPressed(0, ACTION_OBSERVER_SUBSET_PREV)") != std::string::npos);
    REQUIRE(spectator.find("actionPressed(0, ACTION_OBSERVER_CAMERA_TOGGLE)") != std::string::npos);
    REQUIRE(spectator.find("actionReleased(0, ACTION_OBSERVER_FREEFLY)") != std::string::npos);
    REQUIRE(spectator.find("actionAxis(0, ACTION_AXIS_MOVE_X") != std::string::npos);
    REQUIRE(spectator.find("actionHeld(0, ACTION_OBSERVER_ASCEND)") != std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyPressed") == std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyReleased") == std::string::npos);
    REQUIRE(spectator.find("ImGui::IsKeyDown") == std::string::npos);

    // Prompts now use the same active-context winner as dispatch, so Observer
    // participates without a second hard-coded glyph context roster.
    REQUIRE(glyphs.find("actionmapGetActiveBindingVkMatching(0, action, ACTIONMAP_DEVICE_GAMEPAD") != std::string::npos);
    REQUIRE(mainmenu.find("BG_OBSERVER") != std::string::npos);
    REQUIRE(mainmenu.find("&g_ImcObserver") != std::string::npos);
}
