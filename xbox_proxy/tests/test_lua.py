"""Behavioral regression tests on the real Lua files, using Lua 5.1 (lupa).

BeamNG APIs are mocked; this does not replace an in-game compatibility test.
Run: python xbox_proxy/tests/test_lua.py
Dependency: python -m pip install lupa
"""
from pathlib import Path
import json
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / ".test-deps"))
from lupa.lua51 import LuaRuntime

MOD = PROJECT.parent / "revlimiter_haptics" / "mod"
DEFAULTS = {
    "controllerHapticsRtIntensity": 85, "controllerHapticsAbsIntensity": 50,
    "controllerHapticsRtStart": 95, "controllerHapticsShiftIntensity": 100,
    "controllerHapticsShiftMode": 1, "controllerHapticsStartupIntensity": 100,
    "controllerHapticsStartupInterval": 300, "controllerHapticsAbsSensitivity": 69,
    "controllerHapticsBrakeLockIntensity": 85,
}


class LuaTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.globals()["values"] = self.lua.table_from(DEFAULTS)
        self.lua.execute("""
          reads, writes = 0, 0
          settings = {
            getValue = function(k, default) reads=reads+1; if values[k]==nil then return default end; return values[k] end,
            setValue = function(k,v) writes=writes+1; values[k]=v end
          }
          electrics = {values={throttle_input=.9,brake_input=0,airspeed=20,gearIndex=3,engineRunning=true}}
          playerInfo = {firstPlayerSeated=true}
          obj = {getID=function() return 42 end}
          powertrain = {
            getDevice=function() return {type='combustionEngine',outputAV1=700,revLimiterRPM=7000} end,
            getDevicesByType=function() return {} end
          }
          wheels = {wheels={}}
        """)

    def load(self, relative):
        return self.lua.execute((MOD / relative).read_text(encoding="utf-8-sig"))

    def protocol(self):
        return self.load("lua/vehicle/protocols/controllerHaptics.lua")

    def test_protocol_settings_cache_and_reset(self):
        mod = self.protocol()
        output = self.lua.table()
        for _ in range(600):
            mod.fillStruct(output, 1/60)
        self.assertLessEqual(self.lua.globals().reads, 1000)  # Previously 5,400.
        self.assertAlmostEqual(output.rtIntensity, .85)
        self.lua.globals()["values"].controllerHapticsRtIntensity = 20
        for _ in range(8):
            mod.fillStruct(output, 1/60)
        self.assertAlmostEqual(output.rtIntensity, .2)
        self.lua.globals().electrics["values"].controllerHapticsVisualBodyLeft = .9
        mod.reset()
        self.assertEqual(self.lua.globals().electrics["values"].controllerHapticsVisualBodyLeft, 0)
        self.lua.globals()["values"].controllerHapticsRtIntensity = 40
        mod.fillStruct(output, 1/60)
        self.assertAlmostEqual(output.rtIntensity, .4)

    def test_protocol_invalid_numbers_and_missing_systems(self):
        self.lua.globals()["values"].controllerHapticsRtIntensity = float("nan")
        self.lua.globals()["values"].controllerHapticsShiftMode = 1.8
        mod = self.protocol()
        output = self.lua.table()
        mod.fillStruct(output, 1/60)
        self.assertAlmostEqual(output.rtIntensity, .85)
        self.assertEqual(output.shiftMode, 1)
        self.lua.globals().powertrain = None
        mod.fillStruct(output, 1/60)
        self.assertEqual(output.flags, 1)
        self.lua.globals().electrics = None
        mod.fillStruct(output, 1/60)
        self.assertEqual(output.flags, 0)

    def test_protocol_pause_and_wire_abi(self):
        mod = self.protocol()
        out = self.lua.table()
        mod.fillStruct(out, 1/60)
        self.assertEqual(out.magic, "BCH1")
        self.assertEqual(mod.getPort(), 26780)
        self.assertEqual(mod.getAddress(), "127.0.0.1")
        self.assertEqual(mod.getMaxUpdateRate(), 60)
        self.assertFalse(mod.isPhysicsStepUsed())
        # 24 four-byte slots; preserves the 96-byte BCH1 contract.
        self.assertEqual(mod.getStructDefinition().count(";"), 24)
        mod.fillStruct(out, 0)
        self.assertEqual(out.flags, 0)
        self.assertEqual(self.lua.globals().electrics["values"].controllerHapticsVisualTriggerRight, 0)

    def test_bootstrap_registers_only_category(self):
        self.lua.execute("""
          core_input_categories={}
          extensions={load=function() error('bootstrap must not load nested extensions') end}
        """)
        mod = self.load("lua/ge/extensions/controllerHapticsBootstrap.lua")
        mod.onExtensionLoaded()
        self.assertEqual(self.lua.globals().core_input_categories.controllerHaptics.title,
                         "Xbox Haptics Controller")
        self.assertIsNone(self.lua.globals().core_input_actions)
        self.assertIsNone(self.lua.globals().core_input_bindings)
        mod.onExtensionUnloaded()
        self.assertIsNone(self.lua.globals().core_input_categories.controllerHaptics)
        self.lua.execute("core_input_categories.controllerHaptics={foreign=true}")
        mod.onExtensionLoaded()
        mod.onExtensionUnloaded()
        self.assertTrue(self.lua.globals().core_input_categories.controllerHaptics.foreign)

    def test_modscript_has_single_flat_bootstrap_load(self):
        self.lua.execute("""
          events={}
          extensions={load=function(name)
            events[#events+1]='load:'..name
          end}
        """)
        self.lua.execute((MOD / "scripts/beamng_controller_haptics/modScript.lua").read_text())
        events = self.lua.globals().events
        self.assertEqual(events[1], "load:controllerHapticsBootstrap")
        self.assertIsNone(events[2])

    def test_ui_changes_only_write_modified_setting(self):
        self.lua.execute("""
          sliderChange=false
          ui_imgui = {
            BoolPtr=function(v) return {[0]=v} end,
            FloatPtr=function(v) return {[0]=v} end,
            ImVec2=function() return {} end,
            SetNextWindowSize=function() end,
            Begin=function() return true end, End=function() end,
            TextWrapped=function() end, Separator=function() end,
            SliderFloat=function(label,ptr)
              if sliderChange and string.find(label,'controllerHapticsRt',1,true) then
                sliderChange=false; ptr[0]=40; return true
              end
              return false
            end,
            BeginCombo=function() return false end,
            Button=function() return false end, SameLine=function() end
          }
        """)
        mod = self.load("lua/ge/extensions/controllerHapticsUI.lua")
        mod.toggle()
        self.lua.globals().sliderChange = True
        mod.onUpdate()
        self.assertEqual(self.lua.globals()["values"].controllerHapticsRtIntensity, 40)
        self.assertEqual(self.lua.globals().writes, 1)
        for _ in range(60):
            mod.onUpdate()
        self.assertEqual(self.lua.globals().writes, 1)

    def test_all_lua_parse_and_json_is_valid(self):
        loader = self.lua.eval("function(source) local f,err=loadstring(source); return f~=nil,err end")
        for file in MOD.rglob("*.lua"):
            ok, error = loader(file.read_text(encoding="utf-8-sig"))
            self.assertTrue(ok, f"{file}: {error}")
        for file in MOD.rglob("*.json"):
            json.loads(file.read_text(encoding="utf-8-sig"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
