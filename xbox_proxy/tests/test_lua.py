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
    "controllerHapticsMixMode": 0, "controllerHapticsNativeGain": 100,
    "controllerHapticsCustomGain": 100,
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

    def test_bootstrap_waits_for_input_category_module(self):
        mod = self.load("lua/ge/extensions/controllerHapticsBootstrap.lua")
        mod.onExtensionLoaded()
        for _ in range(3):
            mod.onUpdate()
        self.lua.execute("core_input_categories={}")
        mod.onUpdate()
        self.assertEqual(self.lua.globals().core_input_categories.controllerHaptics.title,
                         "Xbox Haptics Controller")
        self.lua.execute("core_input_categories.controllerHaptics=nil")
        mod.onUpdate()
        self.assertIsNone(self.lua.globals().core_input_categories.controllerHaptics)
        # Registration stops after success; the callback stays stable for BeamNG.
        mod.onExtensionUnloaded()
        self.assertIsNone(self.lua.globals().core_input_categories.controllerHaptics)
        self.lua.execute("core_input_categories=nil")
        mod.onExtensionLoaded()
        mod.onExtensionUnloaded()
        self.lua.execute("core_input_categories={}")
        mod.onUpdate()
        self.assertIsNone(self.lua.globals().core_input_categories.controllerHaptics)

    def test_modscript_has_flat_loads(self):
        self.lua.execute("""
          events={}
          extensions={load=function(name)
            events[#events+1]='load:'..name
          end}
        """)
        self.lua.execute((MOD / "scripts/beamng_controller_haptics/modScript.lua").read_text())
        events = self.lua.globals().events
        self.assertEqual(events[1], "load:controllerHapticsBootstrap")
        self.assertEqual(events[2], "load:controllerHapticsDiagnostics")
        self.assertIsNone(events[3])

    def test_lock_uses_braked_loaded_wheels_and_forward_speed(self):
        self.lua.execute('''
          forwardSpeed=20
          obj.getVelocity=function() return {dot=function() return forwardSpeed end} end
          obj.getDirectionVector=function() return {} end
          electrics.values.brake_input=1
          wheel={radius=.3,angularVelocity=0,downForceRaw=1000,brakeTorque=1000,brakingTorque=500}
          wheels.wheels={[0]=wheel}
        ''')
        mod=self.protocol(); out=self.lua.table()
        mod.fillStruct(out,1/60)
        self.assertEqual(out.lockRatio,1)
        for field,value in [('brakingTorque',0),('brakeTorque',0),('downForceRaw',0),('isBroken',True)]:
            wheel=self.lua.globals().wheel
            old=wheel[field]; wheel[field]=value
            mod.fillStruct(out,1/60)
            self.assertEqual(out.lockRatio,0,field)
            wheel[field]=old
        self.lua.globals().forwardSpeed=0
        mod.fillStruct(out,1/60)
        self.assertEqual(out.lockRatio,0)  # Sideways sliding, airspeed still 20.
        self.lua.globals().forwardSpeed=-20
        mod.fillStruct(out,1/60)
        self.assertEqual(out.lockRatio,1)
        self.lua.globals().wheel.angularVelocityBrakeCouple=20/.3
        mod.fillStruct(out,1/60)
        self.assertAlmostEqual(out.lockRatio,0)

    def test_visuals_only_follow_confirmed_feedback_and_expire(self):
        mod=self.protocol(); out=self.lua.table()
        self.lua.globals().electrics['values'].gearIndex=4
        mod.fillStruct(out,1/60)
        self.assertEqual(self.lua.globals().electrics['values'].controllerHapticsVisualTriggerRight,0)
        self.lua.execute('electrics.values.controllerHapticsVisualTriggerRight=.7; electrics.values.controllerHapticsFeedbackTTL=.1; electrics.values.controllerHapticsOutputValid=1')
        mod.fillStruct(out,.02)
        self.assertEqual(self.lua.globals().electrics['values'].controllerHapticsVisualTriggerRight,.7)
        mod.fillStruct(out,.2)
        self.assertEqual(self.lua.globals().electrics['values'].controllerHapticsVisualTriggerRight,0)
        self.assertEqual(self.lua.globals().electrics['values'].controllerHapticsOutputValid,0)

    def test_diagnostics_validates_replies_and_clears_stale_outputs(self):
        self.lua.execute('''
          packets={}; sent={}; commands={}
          veh={getID=function() return 42 end,queueLuaCommand=function(_,s) commands[#commands+1]=s end}
          getPlayerVehicle=function() return veh end
          be={getObjectByID=function(_,id) if id==42 then return veh end end}
          sock={settimeout=function() end,setsockname=function() return true end,
            setpeername=function() return true end,close=function() end,
            send=function(_,s) sent[#sent+1]=s; return #s end,
            receive=function() return table.remove(packets,1) end}
          package.preload.socket=function() return {udp=function() return sock end} end
          reply={version='1.0.26.0',seq=1,vehicle=42,telemetry=1,paired=1,enabled=1,applied=1,backend=2,
            error=0,left=.2,right=.4,lt=.6,rt=.8,initMs=0,initPeakMs=0,scanMs=0,scanPeakMs=0,
            outputMs=0,outputPeakMs=0,uninitPeakMs=0,slowCalls=0}
          jsonDecode=function() return reply end
        ''')
        mod=self.load('lua/ge/extensions/controllerHapticsDiagnostics.lua')
        mod.onUpdate(.04)
        self.assertTrue(self.lua.globals().sent[1].startswith('BCQ1 1 42 0 '))
        self.lua.execute("packets[1]='BCD1 {}'; reply.rt=0/0")
        mod.onUpdate(.04)
        self.assertFalse(mod.getStatus().available)
        self.lua.execute("packets[1]='BCD1 {}'; reply.rt=.8; reply.seq=2")
        mod.onUpdate(.04)
        self.assertTrue(mod.getStatus().available)
        self.assertIn('controllerHapticsVisualTriggerRight=0.80000',self.lua.globals().commands[1])
        # A duplicate reply cannot refresh the timeout.
        self.lua.execute("packets[1]='BCD1 {}'")
        mod.onUpdate(.6)
        self.assertFalse(mod.getStatus().available)
        self.assertIn('controllerHapticsOutputValid=0',self.lua.globals().commands[2])
        mod.onExtensionUnloaded()

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
