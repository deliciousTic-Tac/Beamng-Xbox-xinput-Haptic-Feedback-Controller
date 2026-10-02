-- BeamNG wraps extensions.load while running modScript.lua and automatically
-- keeps the requested extension loaded. Keep this entry point flat: nested
-- extension loads can re-enter the mod manager during its mount pass.
extensions.load('controllerHapticsBootstrap')
