# Multiplayer client execution audit

Date: 2026-11-12. The findings below describe the initial source audit; no live gameplay reproduction was performed.

## Implementation status

Updated 2026-11-12 against the current source and working-tree changes. The implementation summary in the earlier version was too broad: it described party inventory controls as unavailable, while the current changes have begun restoring them. The old numbered findings remain below as the initial audit record; the status is:

| Findings | Current status |
| --- | --- |
| 1-4, 6-21 | Source changes address the reported routing, authorization, inventory replication, and state-lifecycle gaps. They have not all been verified in live multiplayer. |
| 5 (F11 attacks on doors) | Source change present, not gameplay-verified: the multiplayer first-person attack handler now falls back from critter picking to the door picker. Verify multiplayer and singleplayer F11 parity. |
| 22 (party loot pane transfers) | In progress and unverified: the current changes send the displayed party actor with transfers and add server-side selection/authorization, but the full party inventory/gear interaction still needs review and end-to-end testing. |

The current working changes add party actor access checks, actor-aware transfer/equipment commands, server-side party target selection, and reconciliation hooks. The access policy has regression coverage for self, companion, other-player, inactive, elevation, and range cases. Gear context-menu actions still need authoritative routing, and multiplayer barter party-pane switching is not implemented; do not treat either surface as complete. See [`multiplayer-party-inventory-handoff.md`](multiplayer-party-inventory-handoff.md) for the precise unfinished work.

Singleplayer behavior is intended to remain unchanged; shared inventory changes must stay on the viewer/server paths. The current source builds both `fallout2-ce` and `f2_server`, and all 7 CTest tests pass, including the new party access-policy test. These checks do not establish live two-client behavior, visual mouse capture, delayed-turn behavior, or full party inventory correctness.

The executable has two separate input dispatchers. Singleplayer uses `gameHandleKey` / `_gmouse_handle_event` (`game_ui.cc`, `game_mouse.cc`). Multiplayer uses the viewer loop in `main.cc`, which intentionally does not call `gameHandleKey`. Shared screens such as inventory need their own `clientViewerActive()` forks. The existence of a singleplayer fix or a server handler therefore does not establish that multiplayer reaches it.

References below are relative to the source tree at the time of this audit. “Confirmed” means the source establishes the routing/state omission; visible symptoms still need gameplay verification. Priority describes likely user impact, not a measured reproduction frequency.

## Findings

### 1. Stealing and planting run locally instead of calling the server

**High priority; confirmed routing gap.** Server `stake` / `splant` handlers exist in `src/server_control.cc:2511` and enqueue the authoritative steal intents at lines 2553–2556. However, the only gameplay call to `clientViewerStealVerb` is `sdone` in `src/inventory_ui.cc:3950`.

The drag handlers call `skillsPerformStealing` locally at lines 4451 and 4488. Their network transfer forks explicitly require `!_gIsSteal` at lines 4462 and 4499, leaving the stealing path to local `itemMove` at lines 4470 and 4507. Those are appropriate singleplayer operations but do not execute the multiplayer server's steal rolls or transfers.

Repro: steal or plant an item in multiplayer, then compare the server inventory and reconnect. Expected repair: send `stake` / `splant` from the viewer drag leaves and wait for authoritative results; preserve the local path for singleplayer.

### 2. First-person skill clicks become ordinary interactions outside combat

**High priority; confirmed dispatch mismatch.** `src/main.cc:1849` routes first-person peacetime clicks into `viewerHandleFirstPersonClick` at line 1289. That function does not inspect the selected skill cursor. It sends ordinary `talk`, `get`, or `use` commands and consumes the click before the skill dispatcher at line 2461.

Repro: F11, select Lockpick, click a locked door. The server can receive `use` instead of `skill <netId> <skill>`. First Aid, Doctor, Science, Repair, Traps and Steal share this defect. Expected repair: honor the selected interaction mode before primary-action dispatch.

### 3. First-person equipped use-on clicks also lose their intended command

**High priority; confirmed dispatch mismatch.** The same peacetime handler ignores `GAME_MOUSE_MODE_USE_CROSSHAIR`. An equipped shovel, key/tool, or healing item intended for another actor can trigger the target's ordinary action rather than `useitemon`. The correct sender exists at `src/main.cc:2436`, and the server handler exists at `src/server_control.cc:2697`.

Repro: equip a use-on item, arm its target cursor, and click a first-person target outside combat. Repair alongside finding 2, with a separate regression case for use-on mode.

### 4. First-person combat interactions use isometric target picking

**High priority; confirmed picker mismatch.** The first-person interaction handler is gated to outside combat at `src/main.cc:1849`. Combat action menus, equipped use-on, skill targeting and primary actions use `gameMouseGetObjectUnderCursor` at lines 2383, 2436, 2461 and 2476. That picker uses isometric sprite hit testing (`src/game_mouse.cc:1732`). Only the attack branch switches to first-person picking at `src/main.cc:2403`.

Repro: in F11 combat, select a skill or primary interaction and click a visible door/actor. The command can have no target or the wrong target. Expected repair: use the first-person pickers for every interaction mode, preserving the isometric pickers when F11 is disabled.

### 5. First-person attacks cannot select scenery doors

**Coverage gap from the initial audit; source change present, not gameplay-verified.** The initial F11 attack branch only picked critters. The current multiplayer click handler (`viewerHandleFirstPersonClick` in `src/main.cc`) falls back to `tileFirstPersonPreviewPickDoorAt` and passes the result through `_combat_attack_this`; the server supports door attack targets (`src/combat_drain.cc`). Verify the multiplayer behavior and check singleplayer F11 separately, since its input dispatcher is independent.

Regression check: attempt to shoot a visible door in multiplayer F11 combat and confirm the server validates/applies the attack. Also test singleplayer F11 independently.

### 6. Combat hand self-use sends an inventory-only command

**High priority; confirmed client/server contract mismatch.** `src/main.cc:2199` sends `useitem <pid>` when clicking a usable nonweapon in hand. The comment says the server queues it and charges AP. In fact, `src/server_control.cc:3244` treats it as an inventory-screen operation; line 3260 requires an already-open paid inventory session, and line 3544 executes it inline.

Singleplayer's hand self-use charges its own AP (`src/interface.cc:1408`). Normal multiplayer hand use therefore gets “Open your inventory first,” despite not being an inventory-screen action. Expected repair: a distinct priced hand-use execution path, with own-turn checks, separate from free manipulation within a paid pack session.

### 7. Combat backpack/use-item-on menu entry is disabled despite server support

**Medium priority; confirmed stale gate.** `src/main.cc:1223` only opens the inventory-item picker outside combat. Its comment claims the server drops the command in combat. The server now supports it: `src/server_control.cc:2726` arms `kInteractUseItemOn`, and line 606 executes `actionUseItemOnObjectWithApCost`.

Repro: in combat, select the backpack action on another actor/door. Nothing opens. Expected repair: enable the picker with the appropriate combat modal lifetime handling and retain server AP validation.

### 8. Combat loot opening works, but transfers are refused

**High priority; confirmed end-to-end disagreement.** `kInteractLoot` charges opening AP and sends a grant (`src/server_control.cc:540` and following). `src/main.cc:1125` deliberately keeps the loot modal open in combat. However, `take`, `put` and `takeall` are unconditionally rejected in combat at `src/server_control.cc:3777`.

Repro: loot a corpse during your turn, then transfer an item. Opening can cost AP while every transfer fails. Expected repair: consistently support authorized combat loot sessions, or refuse the opening before charging until that support exists.

### 9. Loot UI can open before the server grants it

**Medium priority; confirmed authorization ordering gap.** `viewerArmPendingLoot` records a pending request immediately (`src/main.cc:1067`). The poll opens the screen on grant **or local adjacency** at line 1104, even if the server has not accepted the request. A stale mirror, insufficient AP, or another-player turn can therefore produce a screen for a refused action.

Repro: attempt adjacent looting with insufficient AP or delayed network updates. Expected repair: use the authoritative grant to open requested sessions; do not interpret local adjacency as acceptance. This is separate from finding 8's transfer policy.

### 10. Carried containers cannot be opened through the viewer inventory

**High priority; confirmed missing action.** `src/inventory_ui.cc:3342` intercepts viewer context actions and returns. Its USE branch at line 3358 explicitly skips `ITEM_TYPE_CONTAINER`. Singleplayer reaches `_container_enter` at line 3452; this is its only call site.

The server supports `invput`, but ordinary viewer inventory cannot enter the bag to recover its contents. Subsequent item commands also search top-level inventory (`src/server_control.cc:3484`). Expected repair: implement safe container navigation plus authoritative operations against its contents; do not merely remove the gate and expose local mutations.

### 11. Item use and explosive arming lose instance identity

**Medium priority; confirmed ambiguous addressing.** Equip/drop/unload use netIds, but useitem, useitemon and explosive arming send PIDs (`src/inventory_ui.cc:3356`, `src/client_net.h:236`). The server chooses the first top-level PID match (`src/server_control.cc:2704`, line 3484).

Repro: hold two same-PID instances with different charge/armed state and select the second. The server can act on the first; for explosives it can refuse because the first is already armed (`src/server_control.cc:3573`). Expected repair: address the selected instance by netId and re-resolve that instance at deferred execution time.

### 12. Remote live inventories do not reconcile additions/removals

**High priority; confirmed replication omission.** `src/client_net.cc:2655` handles ordinary nonlocal living critters by updating existing items only. It neither adds newly acquired items nor removes discarded ones. The local-player branch performs a fuller reconciliation.

Repro: another player picks up a weapon absent from the join snapshot and equips/fires it. Observers can have the authoritative actor art but no matching weapon object for presentation. Expected repair: identity-preserving remote reconciliation with deferred frees for items still referenced by replay.

### 13. Remote same-PID weapons overwrite one mirror item

**Medium priority; confirmed matching defect.** Each incoming row in `src/client_net.cc:2681` searches from the beginning by PID and stops at the first match. Wire netIds do not distinguish the instances in this branch.

Repro: a remote player carries two identical guns with different ammo, one in each hand. Both rows update the first gun's equipment/ammo state. Expected repair: use netId matching as the local-player reconciliation does.

### 14. Newly spawned NPC/container inventory can remain absent indefinitely

**High priority; confirmed missing initial payload.** SPAWN sends no inventory (`src/presenter_network.cc:374`). The full first-beat delta also omits `OBJECT_DELTA_INVENTORY` (`src/object_delta.cc:299`), but stores the current inventory hash as its future comparison baseline at line 310.

Repro: a script creates and fills an NPC/container in one beat. With no later inventory mutation, the client never receives its contents. Singleplayer owns the actual populated object. Expected repair: include inventory in initial convergence for new objects.

### 15. Ground item ammo/charges do not have incremental replication

**Medium priority; confirmed payload omission, visible impact needs reproduction.** World `ObjectShadow` omits item-instance fields (`src/object_delta.cc:29`). SPAWN/CONNECT omit ammo/charge data (`src/presenter_network.cc:374`, line 436). Unknown-item healing uses prototype defaults (`src/client_net.cc:2170`). Inventory payloads carry more state, but only after an owner has the item.

Repro: spawn an on-ground weapon with custom loaded ammo or a partially charged tool after a viewer joins. Inspection/presentation can show prototype values until pickup or rebaseline. Existing known dropped items may retain correct values, so test newly created/unknown objects explicitly.

### 16. Door/container lock and jam changes are not incrementally replicated

**Medium priority; confirmed state omission, functional impact conditional.** Lock/jam operations change the object union fields (`src/proto_instance.cc:2169`, line 2186, line 2350, line 2372). Those fields are absent from object deltas. The earlier door fix synchronizes OPEN from generic flags; it preserves the map-loaded lock/jam bits.

Repro: lockpick/unlock/jam a target and inspect client state without reloading. Server-authoritative operations may succeed normally; stale inspection and any local checks remain suspect. Expected repair: replicate the relevant door/container state explicitly, including baseline and delta handling.

### 17. Elevator offers outlive the interaction that authorized them

**Medium priority; confirmed lifecycle omission, ordinary-client trigger needs reproduction.** Offers are stored by slot in `src/server_control.cc:1713`. The only caller sets an elevator offer (`src/script_request_handler_server.cc:135`); no caller clears it with -1. Escaping the client panel sends nothing (`src/main.cc:2567`). Disconnect/map changes do not invalidate this offer. The `elev` handler at `src/server_control.cc:3111` checks the stored offer and floor, but does not recheck the offered map/location or combat state.

Repro: cancel a panel, move/change map or reconnect, then submit a delayed elevator response. Expected repair: tie offers to a session/world generation and origin context, expire/cancel them, and revalidate before riding. A normal canceled UI does not itself send the delayed response; this is a stale-request risk, not proof that cancel instantly teleports anyone.

### 18. Combat hand switching can park a stale action for a later turn

**Medium priority; confirmed inconsistent turn gate, delayed-network reproduction needed.** `src/server_control.cc:2331` queues hand switching without checking turn ownership. Other interactions reject off-turn requests at line 902. The viewer trusts cached combat state and flips its hand immediately (`src/main.cc:2118`); refusal rollback is tracked only outside combat.

Repro: delay turn updates, switch hands just after the authoritative turn ends. The HUD changes immediately while the server queues the switch for a later turn. Expected repair: apply the same receipt-time ownership policy and authoritative correction used by other combat actions.

### 19. Server approach cancellation has no multiplayer client sender

**Lower priority; confirmed unexposed handler.** `src/server_control.cc:2305` implements `cancel` for the pending walk-then-act operation. No gameplay client sender exists. Right-click only changes local cursor mode (`src/main.cc:2353`); Escape opens quit confirmation.

Repro: start approaching a door/item/skill target and try to cancel without issuing a replacement move/action. Expected repair: give explicit abort input a `cancel` route and clear corresponding pending client UI state. Cursor-mode cancellation alone is not necessarily intended to abort an already-issued action, so choose the binding deliberately.

### 20. First-person multiplayer modal openings omit singleplayer mouse release

**Medium priority; source divergence confirmed, visual reproduction needed.** Singleplayer wraps character/skill/Pipboy/preferences/quit UI in `FirstPersonPreviewMouseReleaseScope` (`src/game_ui.cc:1051`, line 1125, line 1190). The viewer directly opens those screens (`src/main.cc:2257`, line 2299, line 2314 and earlier branches). `ScopedGameMode` does not release relative mouse capture.

Repro: open each screen from captured F11 mode and try mouse navigation, then return to gameplay. Inventory has its own guard and should be tested as a control. Expected repair: share the mouse-release/restore lifecycle for modal entry across both dispatchers.

### 21. Barter withdrawal loses which offer table was selected

**Medium priority; confirmed ambiguous command.** Both player-table and merchant-table withdrawal send identical `bunoffer <pid> <quantity>` requests (`src/inventory_ui.cc:4646`, line 4663). The server-side barter executor searches the player's table first, then the merchant's (`src/inventory_ui.cc:5058`). Singleplayer passes the actual source table to `itemMoveForce`.

Repro: put the same PID, especially caps, on both offer tables, then withdraw the merchant's row. The player's offer is withdrawn instead. Expected repair: transmit and validate the selected table/side, ideally together with instance identity.

### 22. Party loot pane selection is ignored by authoritative transfers

**Medium priority; initial mismatch confirmed, repair in progress and unverified.** The initial audit found that the loot UI could switch its left pane to an NPC while viewer transfer requests omitted the selected actor. The current changes add explicit actor IDs to party transfer/loot commands, server-side party target selection, and eligibility checks. These are intended to keep displayed and mutated inventories aligned, but the path has not had a full review or live multiplayer test.

Do not consider party inventory switching complete yet. Gear context-menu actions can still be intercepted before authoritative routing, and the separate multiplayer barter viewer does not implement party-pane switching. Verify selection acceptance/refusal, same-target selection, spectator behavior, disconnect cleanup, inventory reconciliation, and transfers in both directions. Preserve the server dialog/steal driver as the authority; the selected NPC is only the inventory owner.

## Additional candidates and parity gaps

- **Party equipment controls are only partially restored.** The current changes add viewer gear commands and server authorization, but drag/ctrl-click helpers do not establish that the generic gear-slot context menu routes every action authoritatively. Trace weapon and armor slot actions, including unload/drop/unequip, and ensure rejected requests never fall through to local mutation. The multiplayer barter viewer (`inventoryOpenTradeViewer`) still has no party-owner switching; do not enable an ineffective path by removing only a guard in the singleplayer barter UI.
- **Missing viewer shortcuts:** Tab automap, J first-person action menu, Z rest, M cursor cycling, Home centering and comma/dot rotation have singleplayer handlers but no equivalent viewer dispatch. Some functions remain reachable through other UI. These are input parity gaps, not automatically missing server implementations. Save/load and global pause require separate multiplayer policy and are not counted as defects here.
- **Combat input busy gates vary by command.** Attack/move use `combatBusy`; primary interactions, item self-use and reload do not consistently use the same presentation gate. Server validation limits some consequences, but repeated commands during outstanding replay should be tested for duplicate spending and misleading refusals.

## Server/client routing map

| Action family | Singleplayer execution | Multiplayer authority | Audit result |
| --- | --- | --- | --- |
| Movement | Local animation movement | `mv`, `cmove` | Both routes exist; F11 WASD forwards to authority. |
| Attack | `_combat_attack_this` and local combat | `cattack` through commit fork | Shared selection/remote commit and multiplayer F11 door picking are implemented; verify live behavior and singleplayer parity. |
| End turn/combat | Local combat loop | `cendturn`, `cendcombat`, `cstart` | Explicit viewer routes exist. |
| Door/scenery use | `_action_use_an_object` | `use`, `usedoor` | First-person mode/picker paths are implemented; live target-selection behavior remains to verify. |
| Pickup | `actionPickUp` | `get`, `gethere` | Explicit viewer routes exist. |
| Examine/talk/push/rotate | Local object/action functions | `look`, `talk`, `push`, `rot` | Viewer primary/action-menu routes exist; verify first-person target selection live. |
| Skills | `actionUseSkill`, local sneak state | `skill`, `sneak` | First-person skill dispatch and targeting are implemented; live behavior remains to verify. |
| Use item on target | `_action_use_an_item_on_object` | `useitemon` | Combat picker lifetime and first-person dispatch are implemented; live behavior remains to verify. |
| Use item on self | `objectUseItem`, inventory item functions | `useitem` | Pack and priced combat hand-use paths exist; live AP behavior remains to verify. |
| Equip/drop/unload | Local inventory mutations | `invwield`, `invunwield`, `invdrop`, `unload` | Viewer forks and instance-aware remote inventory reconciliation are implemented; party gear remains partial. |
| Container/ammo manipulation | Local `itemMove` / reload operations | `invput`, `invload`, `reload` | Carried-container navigation and authoritative item routes are implemented; live behavior remains to verify. |
| Loot | Local loot screen/transfers | `loot`, `take`, `put`, `takeall` | Combat grants and transfers have an authoritative path; party-owner transfers remain under review. |
| Steal/plant | Local skill roll and transfer | `stake`, `splant`, `sdone` | Viewer intent path is implemented; live server/client outcome remains to verify. |
| Character advancement | Local editor mutations | `sheetopen/close`, `skillup/down`, `perkpick`, `tagpick`, `mutpick` | Viewer helpers and server handlers exist; not evidence of a missing route by itself. |
| Dialog/barter | Local dialog/trade engine | `dsay`, `dend`, `dbarter`, `boffer`, `btake`, `bunoffer`, `bcommit`, `bdone`, `bcancel` | Withdrawal identifies its offer table; party inventory-owner switching is not implemented in the multiplayer trade viewer. |
| Rest | Local rest engine | `rest`, `restopt` | Explicit route exists; advances shared time by current design. |
| Elevator | Local panel then ride | Elevator prompt then `elev` | Offer context/expiry and cancellation handling are implemented; delayed-response behavior remains to verify. |
| Worldmap | Local worldmap input | `wmmove`, `wmenter`, `wmesc`, encounter answers | Explicit routes exist. |
| Approach abort | Cancel local animation | `cancel` | Backspace sends cancellation outside chat/dialog; live pending-action cleanup remains to verify. |

## Suggested verification order

1. Finish party inventory/gear routing and explicitly keep party-pane switching unavailable in multiplayer barter until its separate viewer path is implemented.
2. Verify F11 door attack target selection in both the multiplayer viewer and the singleplayer F11 path.
3. Run two-client checks for the previously repaired high-impact paths: steal/plant, hand self-use, combat loot, carried containers, and remote inventory convergence.
4. Exercise first-person skill/use-on targeting in combat and peacetime, then verify rejected requests do not mutate inventory, AP, or the displayed target.
5. Test delayed elevator/turn responses and mouse-capture transitions across modal screens.

Use two connected clients, including a non-host actor. Repeat relevant actions in four combinations: isometric/first-person and peacetime/combat. Compare server outcomes, acting-client state, observing-client state and post-reconnect state. For rejected requests verify that neither client inventory nor AP changes locally. Headless server tests alone cannot verify target picking or mouse capture.
