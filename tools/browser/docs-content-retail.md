# Content tab: Retail changes

A balance or repaint mod: the game's own cars and weapons changed, with no new entries.
The Content tab's **Retail changes** section (below Cars and Weapons) lists the 33 retail
cars by name and the 12 item-select weapons; a changed one is marked `● … (N changed)`
and the header counts them. Opening a mod with no entry selected shows its first change.

Selecting one opens the same inspector a new car or weapon has (paint for a car, the numbers
by part, tick to change), but it writes the mod's top-level `patches` and `recipes` by base
object id (`schemas/Mod.schema.json`, `src/game/Port_Mods.c`):

| what | id |
|---|---|
| a car's numbers | `base://HW_Cars/car_N/CarAInfo/0`, `…/CarAInfo/0/engine`, `…/accel`, `…/stunt` |
| a car's paint | `base://HW_Cars/car_N/texture/T` |
| a weapon's numbers | `base://HW_Items/item_N/ItemRecord/0/weapon`, `…/ItemRecord/0` |

A retail weapon N is HW_Items pack N. A new weapon made from it loads a pack of its own
(`Port_ContentItemPack`), so a retail weapon's patch never reaches it, and the new weapon's
own patch never reaches the retail one.

**▶ Drive it / ▶ Try it** saves the mod and starts the game with it on, as for a new entry:
`HWVX_START=race:mode=challenge,car=N` or `race:mode=battle,item=N`. **↺ Reset** drops every
patch and recipe on that car or weapon (also ones the tab doesn't show).

Code: the model is `src/content_retail.rs` (`Target`: an entry's own patch or a retail
car/weapon; `ModProject::target_value`, `set_target_value`, `target_paint`,
`set_target_paint`, `retail_changes`, `retail_reset`); the UI is `src/content_retail_ui.rs`,
with `content_ui.rs`'s `content_numbers` and `content_paint` taking a `Target`.

Checking it in the game: `HWVX_MODS_LOG=1` logs each value
(`base://HW_Cars/car_6/CarAInfo/0 CarAInfo.maxRpm = 9000 (was 7700)`), and without it
`mods: <mod>: N values of base://…`.
