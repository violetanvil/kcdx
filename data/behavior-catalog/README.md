# Behavior catalog

The engine-shipped named-behavior pack. One `.lua` file per behavior; each is a
normal Lua source calling `kcdx.behavior.declare(...)` exactly as a plugin
would. The engine loads this directory as a builtin pack ahead of every user
plugin, stamping each declare under the reserved `kcdx.behavior.<bare>` root, so
every catalog name is available to every plugin regardless of load order.

Each file's header comment is a self-contained statement of the verified fact it
relies on. Promotion of a plugin behavior into the catalog is a file move — the
declare code is unchanged, only the stamping root differs.

## Entries

| Name | What it does |
|------|--------------|
| [`kcdx.behavior.log_texture_streaming`](log_texture_streaming.lua) | Reads the texture-streaming CVar (`r_TexturesStreaming`) and logs it when set; changes no game state. |
| [`kcdx.behavior.motion_blur`](motion_blur.lua) | Toggle camera motion blur (`r_MotionBlur`). |
| [`kcdx.behavior.depth_of_field`](depth_of_field.lua) | Toggle the depth-of-field effect (`r_DepthOfField`). |
| [`kcdx.behavior.display_info`](display_info.lua) | Toggle the on-screen display-info overlay (`r_DisplayInfo`). |
| [`kcdx.behavior.chromatic_aberration`](chromatic_aberration.lua) | Toggle the chromatic-aberration effect (`r_ChromaticAberration`). |
| [`kcdx.behavior.show_compass`](show_compass.lua) | Toggle the HUD compass (`wh_ui_showCompass`). |
| [`kcdx.behavior.ambient_occlusion`](ambient_occlusion.lua) | Toggle screen-space ambient occlusion (`r_ssdo`). |
