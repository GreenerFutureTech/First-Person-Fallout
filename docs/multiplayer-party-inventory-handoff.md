# Resume: multiplayer party inventory and equipment

Paused at the user's request on 2026-11-12. **Partial implementation, not validated or ready to claim fixed.** Preserve the current working tree; do not restart or discard it.

## User scope

Restore party-member inventory switching and gear controls in multiplayer. The user explicitly authorized necessary multiplayer server changes, while preserving singleplayer execution. The earlier broader multiplayer audit/fixes are already in the baseline; the current diff is this follow-up task.

## Current partial changes

- `src/party_inventory_access.h`: new pure access policy. Manage self or a friendly NPC companion; never another registered player actor. Requires active critter, same elevation, within 8 hexes. Review whether the range matches desired UX; no user requirement specified a range.
- `src/inventory.cc` / `.h`: `inventoryCanManagePartyActor`, `inventoryPartyEquip`, `inventoryPartyUnequip`. Server uses the requesting player as `gDude`; do not scope `gDude` to the NPC, because NPC armor/stat behavior depends on that distinction.
- `src/client_net.cc` / `.h`: client commands `partytransfer`, `partygear`, `partyungear`, `takeid`, `putid`, `takeallparty`. Actor and item instance netIds cross the wire. Added actor-aware inventory equipment reconciliation hooks instead of attaching UI gear to `gDude` unconditionally.
- `src/server_control.cc`: parses four integer arguments; validates friendly-session driver and controllable companions for gear/transfers. Loot transfers can name the selected left-pane actor. Added `spartytarget` session selection. These changes need review and tests.
- `src/inventory_ui.cc`: restores party selection and gear controls in loot/party sessions; filters other player actors; sends gear/transfer requests before local mutations; applies server target selection at a safe modal-loop point. Adds restore/detach hooks for equipment owned by the actual displayed left/right actors, plus a UI dirty flag.
- Friendly dedicated-server steal/party sessions now keep equipment attached authoritatively (`lootTargetDetach(..., false, ...)`); ordinary hostile stealing still hides equipped gear. New `STEAL_INTENT_PARTY_TARGET` switches the server session target, restores the old hidden inventory, and broadcasts `stealBegin` again.
- `src/server_stubs.cc`: matching client helper stubs for the server link.

## Critical design facts

- Left-pane gear is removed from the mirror inventory into `gInventoryLeftHandItem`, `gInventoryRightHandItem`, `gInventoryArmor`. `_stack[0]` owns those items, not necessarily `gDude` or `_inven_dude` (which may point into a bag).
- Right-side party gear is removed into `partyTargetEquipped`, owned by `partyBaseTarget`. Restore before reconciling an owner's inventory, clear the snapshot, then detach the reconciled equipment afterward. Repaint only at modal safe points.
- `critterRestoreEquipped` clears the supplied snapshot. `inventoryUnequipFunc` clears flags without removing/re-adding the item, so its pointer remains valid for a following transfer.
- Ordinary server steal sessions save removed equipment in local `item1/item2/armor` pointers until teardown. Mutating that gear without integrating with those locals would restore obsolete gear. The friendly-only attached-equipment change avoids that trap.
- Session ownership is the thief/dialog driver. NPC party membership must never authorize manipulating another human actor. Spectators may render but cannot send mutations or change the active party target.

## Remaining implementation/review work

1. **Build and inspect errors first.** A build was started, but its result/session ID was accidentally not retained by the orchestration call (only output text was printed). At pause, `mingw32-make` processes were still present (IDs 7396, 10540, 15280, 23852, 36604). Check whether they have completed before starting another build against that directory. Do not kill unrelated processes. No successful build or tests have been established for this diff.
2. Review `inventoryCanManagePartyActor`: reject non-critter subjects before calling `critterIsActive`; currently arguments to the pure policy are evaluated eagerly. Add regression tests for self, friendly NPC, other human (even if party member), nonparty NPC, dead/inactive, elevation/range mismatch.
3. **Gear context menus still need wiring.** Drag and ctrl-click gear helpers have viewer branches, but the generic `inventoryWindowOpenContextMenu` viewer interception can still swallow party-slot actions because owner is not `gDude`. Trace `PARTY_WEAPON_SLOT_KEY` / `PARTY_ARMOR_SLOT_KEY`, existing singleplayer behavior, and route supported actions authoritatively. Never fall through to local mutations. Check unload/drop/unequip behavior explicitly.
4. Review the new reconciliation hooks with switched NPC left pane, party right pane, hostile steal, normal inventory, nested player bag, and barter. The new hooks run during every inventory-family modal; ensure no synthetic barter list gets confused with its real subject actor. Recompute displayed body/FID/weight after gear changes as needed.
5. Review switching target within a friendly server session, including selecting the same target, invalid/refused selection, spectator clicks, disconnect, and hidden-box cleanup. Client `clientStealOnBegin` changes identity at decode; the UI intentionally waits until the next safe loop iteration to apply it.
6. **Multiplayer barter party-pane switching is not implemented yet.** Removing the old guard in `barterProcessUI` does not implement it: multiplayer actually uses the separate `inventoryOpenTradeViewer` loop (around line 5500). See design below. Decide based on scope to finish it rather than leave an enabled-but-ineffective path.
7. Add meaningful regression checks, build both targets, run CTest, review every shared-file edit for a multiplayer execution guard. Update the audit implementation status: it still says party controls are disabled. Do not claim live two-client verification without doing it.

## Barter work to finish

- `inventoryOpenTradeViewer` uses synthetic four-list snapshots (`client_barter.cc`); `_inven_dude` is a real critter for stats/art, while `_pud` points at `clientBarterDriverInv()`'s synthetic inventory. Preserve that distinction.
- Server `barterProcessUI` strips merchant equipment into locals. Do not permit gear changes to the merchant during this barrier.
- Keep the true dialog driver/`ServerActorScope` unchanged; selected party inventory owner is separate state.
- Maintain selected owner on the server. Add a driver-only selection intent and publish selected owner's netId in barter state (a trailing field is possible). Filter eligible NPCs and exclude merchant/other human actors. Client applies accepted owner changes with the pending snapshot at modal safe points and renders that actor's body/list.
- OFFER source, player-table UNOFFER destination, COMMIT participant/carry capacity, DONE, disconnect/bail, and cancellation cleanup must all use the selected owner consistently. Reject switching while player offer table is nonempty, or return offers to their original owner first; otherwise item provenance is lost.
- `barterEmitState` currently snapshots `_inven_dude`; it does not scan object deltas. If using real companion inventory/equipment updates, run `objectDeltaScan` before flushing the barter state. Do not replace driver identity with selected owner identity for permissions.
- Possible implementation: append an owner netId to `Presenter::BarterView` and the existing event, latch/apply it in `client_barter`, and add `BARTER_INTENT_SELECT_OWNER` carrying the chosen netId. Preserve existing enum numeric values by appending.

## Validation commands

```powershell
cmake --build out/build/scenery-game-gcc --target fallout2-ce f2_server --parallel 4
cmake -S tools/tests -B out/build/scenery-tests-gcc
cmake --build out/build/scenery-tests-gcc --parallel 4
ctest --test-dir out/build/scenery-tests-gcc --output-on-failure
git diff --check
```

Use UTF-8 explicitly when editing source/docs with Python on this Windows host (`read_text(encoding='utf-8')`, `write_text(..., encoding='utf-8', newline='\n')`). Default cp1252 caused earlier comment edits to miss Unicode punctuation. Temp filesystem permissions can be restrictive; in-memory fixtures worked for existing wire tests.

Existing read-only review agents: `inventory_audit` and `replication_audit`. Their prior review findings informed the notes above; they can be reused for another read-only pass. No commits or deployment were requested.
