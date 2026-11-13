# The trigger orbit camera and a tipped car (aircam branch)

Two pieces for the lead to fold in.

## 1. docs/pc-version/README.md: a new item in the code differences

* **The trigger orbit camera** (`camera_set_targets` with an orbit, camera mode 28 of
  `Camera_UpdatePosition`: GC `0x8001208C`, PC `0x40C520`, PS2 `0x00258940`). After
  `Camera_Orbit`, the GameCube (`0x80012DBC`–`0x80012E0C`) and the PS2 (`0x0025907C`–`0x002590DC`)
  check the player's car: if it is a `CarA`, tipped past 60° (`worldMatrix.v[1].y < 0.5`, at
  `+0x24`) and either stuck (`stuckCounter` `+0x464` > 0) or with its wheels down (`airTime`
  `+0x460` ≤ 0), the camera's mode goes back to `g_triggerAction_params_default` and the camera
  goes back to the car, ending the event's orbit. The PC's mode-28 code (`0x40C93B`–`0x40C9CD`)
  calls the orbit (`0x40BD20`) and jumps straight to the end of the function. It has no check,
  so its orbit keeps going until the trigger script changes the camera. The PC exe writes the
  default mode (`0x47A2E4`) only in `CameraManager_GetNewCamera` (`0x40A5A2`) and
  `CameraManager_ReturnAllToPlayer` (`0x40CA81`). Its `Camera_TriggerActionGetLocation`
  (`0x40C350`) also leaves out the GameCube/PS2 hand-back when a car location's car has been
  freed or released (`flags & 0x201000`).
  HWVX-SDL plays the PC's orbit by default. `--gc-air-camera` / `HWVX_GC_AIR_CAMERA=1` turns
  the GameCube/PS2 check back on.
  Players see the GameCube hand-back mostly after jumps and crashes. `stuckCounter` goes up
  after the tipped body hits the ground, and it is cleared only once the car is upright again
  (`CarA.c`, `tiltState`). So when an event starts its orbit while the car is coming down or
  rolling, the orbit ends at once.

## 2. HWVX-SDL README.md: options

In the usage list, after `--reset-save`:

- `--gc-air-camera` (or `HWVX_GC_AIR_CAMERA=1`): the GameCube/PS2 trigger orbit camera. An
  event's orbit around an object ends, and the camera goes back to the car, as soon as the
  player's car is tipped past 60° with its wheels down or stuck (after a jump or a roll).
  Default: the PC's orbit, which runs until the event's script moves the camera on.
