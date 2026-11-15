-- Telemetry and optional visual indicators in electrics.values; physics is unchanged.
-- BeamNG custom protocol API (0.32+). Packet layout: 96 bytes.
--
-- BeamNG 0.39 warns about custom mod keys in settings.json, but still exposes
-- those values through settings.getValue(). Keeping them allows the in-game
-- haptic tuner to persist its values between sessions.
local M = {}
local avToRPM = 60 / (2 * math.pi)
local settingsRefreshRemaining = 0
local cachedSettings = {}
local settingsRefreshInterval = 0.1 -- Simulation seconds; also invalidated on reset.
local brakeLockScale = 0.85 -- Preserve the BCH1 receiver's historic tuning.

local function number(x, fallback)
  if type(x) == "number" and x == x and math.abs(x) < math.huge then return x end
  return fallback or 0
end
local function positive(x)
  x = number(x)
  return x > 0 and x or nil
end
local function flag(x) return x == true or (type(x) == "number" and x > 0) end
local function pedal(x) return math.min(1, math.max(0, number(x))) end
local function settingPercent(key, default)
  return pedal(number(tonumber(settings.getValue(key, default)), default) / 100)
end
local function settingRange(key, default, minimum, maximum)
  local value = number(tonumber(settings.getValue(key, default)), default)
  return math.min(maximum, math.max(minimum, value))
end

local function refreshSettings(dt)
  settingsRefreshRemaining = settingsRefreshRemaining - math.max(0, number(dt))
  if settingsRefreshRemaining > 1e-9 then return cachedSettings end
  settingsRefreshRemaining = settingsRefreshInterval
  local c = cachedSettings
  c.rtIntensity = settingPercent('controllerHapticsRtIntensity', 85)
  c.absIntensity = settingPercent('controllerHapticsAbsIntensity', 50)
  c.rtStart = settingRange('controllerHapticsRtStart', 95, 75, 98) / 100
  c.shiftIntensity = settingPercent('controllerHapticsShiftIntensity', 100)
  c.shiftMode = math.floor(settingRange('controllerHapticsShiftMode', 1, 0, 2) + 0.5) / 2
  c.startupIntensity = settingPercent('controllerHapticsStartupIntensity', 100)
  c.startupInterval = (settingRange('controllerHapticsStartupInterval', 300, 50, 300) - 50) / 250
  c.absSensitivity = settingPercent('controllerHapticsAbsSensitivity', 69)
  c.brakeLockIntensity = settingPercent('controllerHapticsBrakeLockIntensity', 85) * brakeLockScale
  return c
end
local function publishVisuals(e, bodyLeft, bodyRight, triggerLeft, triggerRight)
  if not e then return end
  e.controllerHapticsVisualBodyLeft = pedal(bodyLeft)
  e.controllerHapticsVisualBodyRight = pedal(bodyRight)
  e.controllerHapticsVisualTriggerLeft = pedal(triggerLeft)
  e.controllerHapticsVisualTriggerRight = pedal(triggerRight)
end

function M.getAddress() return "127.0.0.1" end
function M.getPort() return 26780 end
function M.getMaxUpdateRate() return 60 end
function M.isPhysicsStepUsed() return false end
-- No game-engine window is loaded by this protocol. Keeping this vehicle-side
-- avoids an overlay during loading screens and after returning to gameplay.
function M.init() end
function M.reset()
  settingsRefreshRemaining = 0
  if electrics and electrics.values then electrics.values.controllerHapticsFeedbackTTL = 0; electrics.values.controllerHapticsOutputValid = 0 end
  publishVisuals(electrics and electrics.values, 0, 0, 0, 0)
end
function M.getStructDefinition()
  return [[
    char magic[4];
    unsigned int vehicleId;
    float rpm;
    float limitRPM;
    float throttle;
    float brake;
    float absActive;
    float hasABS;
    float lockRatio;
    float speed;
    float engineRunning;
    float starterActive;
    float rtIntensity;
    float absIntensity;
    float rtStart;
    int gearIndex;
    float shiftIntensity;
    float isShifting;
    float shiftMode;
    float startupIntensity;
    float startupInterval;
    float absSensitivity;
    float brakeLockIntensity;
    unsigned int flags;
  ]]
end

function M.fillStruct(o, dtSim)
  o.magic = "BCH1"
  o.vehicleId = obj:getID()
  o.rpm, o.limitRPM, o.throttle, o.brake = 0, 0, 0, 0
  o.absActive, o.hasABS, o.lockRatio, o.speed, o.engineRunning, o.starterActive, o.flags = 0, 0, 0, 0, 0, 0, 0
  local c = refreshSettings(dtSim)
  o.rtIntensity, o.absIntensity, o.rtStart = c.rtIntensity, c.absIntensity, c.rtStart
  o.gearIndex = 0
  o.shiftIntensity = c.shiftIntensity
  o.isShifting = 0
  o.shiftMode, o.startupIntensity, o.startupInterval = c.shiftMode, c.startupIntensity, c.startupInterval
  o.absSensitivity, o.brakeLockIntensity = c.absSensitivity, c.brakeLockIntensity
  local e = electrics and electrics.values
  -- Overlay channels are now last successful DLL commands, sampled by the GE
  -- diagnostics extension. No duplicate Lua effect calculation.
  if e then
    e.controllerHapticsFeedbackTTL = math.max(0, number(e.controllerHapticsFeedbackTTL) - math.max(0, number(dtSim)))
    if e.controllerHapticsFeedbackTTL <= 0 then
      publishVisuals(e, 0, 0, 0, 0)
      e.controllerHapticsOutputValid = 0
    end
  end
  -- The host already selects the first player's vehicle. Also guard explicitly.
  if not e or not playerInfo or not playerInfo.firstPlayerSeated or number(dtSim) <= 0 then
    publishVisuals(e, 0, 0, 0, 0)
    if e then e.controllerHapticsOutputValid = 0; e.controllerHapticsFeedbackTTL = 0 end
    return
  end
  o.throttle = pedal(e.throttle_input or e.throttle)
  o.brake = pedal(e.brake_input or e.brake)
  o.absActive = flag(e.absActive) and 1 or 0 -- bool in current wheels.lua
  o.hasABS = flag(e.hasABS) and 1 or 0
  o.speed = math.abs(number(e.airspeed))
  o.engineRunning = flag(e.engineRunning) and 1 or 0
  o.gearIndex = math.floor(number(e.gearIndex))
  o.isShifting = flag(e.isShifting) and 1 or 0
  o.flags = 1 -- simulation advancing; zero packets must stop the bridge

  local engine = powertrain and powertrain.getDevice("mainEngine")
  if not engine or engine.type ~= "combustionEngine" then
    local engines = powertrain and powertrain.getDevicesByType("combustionEngine")
    engine = engines and engines[1]
  end
  if engine then
    o.rpm = math.abs(number(engine.outputAV1, number(e.rpm) / avToRPM)) * avToRPM
    local limit = positive(engine.revLimiterRPM) or positive(engine.maxRPM) or 0
    if flag(engine.isTempRevLimiterActive) and positive(engine.tempRevLimiterAV) then
      local temporary = engine.tempRevLimiterAV * avToRPM
      limit = limit > 0 and math.min(limit, temporary) or temporary
    end
    o.limitRPM = limit
    if flag(engine.revLimiterActive) then o.flags = o.flags + 2 end
    o.flags = o.flags + 4 -- combustion engine found
    o.starterActive = number(engine.starterEngagedCoef) > 0.01 and 1 or 0
  end

  -- Use forward speed rather than total airspeed: sideways motion is not
  -- longitudinal braking slip. abs() also supports reversing.
  local longitudinalSpeed = 0
  if obj.getVelocity and obj.getDirectionVector then
    longitudinalSpeed = math.abs(number(obj:getVelocity():dot(obj:getDirectionVector())))
  end
  if o.brake > 0.1 and longitudinalSpeed > 5 and wheels and wheels.wheels then
    local worst = 0
    for _, w in pairs(wheels.wheels) do
      local radius = positive(w.radius)
      -- brakeTorque is capacity; brakingTorque is the current applied request.
      if not w.isBroken and radius and number(w.downForceRaw) > 50
          and number(w.brakeTorque) > 0 and number(w.brakingTorque) > 1 then
        local av = number(w.angularVelocityBrakeCouple, number(w.angularVelocity))
        local surfaceSpeed = math.abs(av) * radius
        worst = math.max(worst, 1 - surfaceSpeed / longitudinalSpeed)
      end
    end
    o.lockRatio = pedal(worst)
  end
end

return M
