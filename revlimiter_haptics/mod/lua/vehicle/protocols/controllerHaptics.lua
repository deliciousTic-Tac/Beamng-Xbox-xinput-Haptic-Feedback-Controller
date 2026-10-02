-- Telemetry and optional visual indicators in electrics.values; physics is unchanged.
-- BeamNG custom protocol API (0.32+). Packet layout: 96 bytes.
--
-- BeamNG 0.39 warns about custom mod keys in settings.json, but still exposes
-- those values through settings.getValue(). Keeping them allows the in-game
-- haptic tuner to persist its values between sessions.
local M = {}
local avToRPM = 60 / (2 * math.pi)
local visualTime = 0
local visualLastGear = 0
local visualShiftUntil = 0
local visualShiftDirection = 1
local settingsRefreshRemaining = 0
local cachedSettings = {}
local settingsRefreshInterval = 0.1 -- Simulation seconds; also invalidated on reset.
local visualShiftDuration = 0.18
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
  visualTime = 0
  visualLastGear = 0
  visualShiftUntil = 0
  visualShiftDirection = 1
  settingsRefreshRemaining = 0
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
  -- The host already selects the first player's vehicle. Also guard explicitly.
  if not e or not playerInfo or not playerInfo.firstPlayerSeated or number(dtSim) <= 0 then
    publishVisuals(e, 0, 0, 0, 0)
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

  -- Optional lock heuristic: braked, loaded wheels moving much slower than car.
  -- Not a precise tire slip estimator (drifts can still cause false positives).
  if o.brake > 0.1 and o.speed > 5 and wheels and wheels.wheels then
    local worst = 0
    for _, w in pairs(wheels.wheels) do
      if not w.isBroken and number(w.downForceRaw) > 50 then
        local surfaceSpeed = math.abs(number(w.angularVelocity)) * number(w.radius)
        worst = math.max(worst, 1 - surfaceSpeed / o.speed)
      end
    end
    o.lockRatio = math.min(1, math.max(0, worst))
  end

  -- Approximate visual indicators, NOT actuator feedback: the DLL applies its
  -- own pulse timing, scaling and connection state. BCH1 remains one-way.
  visualTime = visualTime + number(dtSim)
  local bodyLeft, bodyRight, triggerLeft, triggerRight = 0, 0, 0, 0
  if o.throttle > 0.08 and o.limitRPM > 0 and o.rpm / o.limitRPM >= o.rtStart then
    triggerRight = o.rtIntensity
  end
  local lockThreshold = math.max(0.05, 0.65 * (1 - o.absSensitivity))
  if o.brake > 0.1 and o.speed > 2 and (o.absActive > 0 or o.lockRatio > lockThreshold) then
    triggerLeft = o.hasABS <= 0 and o.lockRatio > lockThreshold and o.brakeLockIntensity or o.absIntensity
  end
  if o.starterActive > 0 then
    bodyLeft, bodyRight = o.startupIntensity, o.startupIntensity
  end

  -- Preserve the last non-neutral gear: sequential/race gearboxes often pass
  -- through neutral for one frame while changing ratio.
  if o.gearIndex ~= 0 then
    if visualLastGear ~= 0 and o.gearIndex ~= visualLastGear then
      visualShiftDirection = o.gearIndex > visualLastGear and 1 or -1
      visualShiftUntil = visualTime + visualShiftDuration
    end
    visualLastGear = o.gearIndex
  end
  if visualTime < visualShiftUntil then
    local shiftMode = math.floor(o.shiftMode * 2 + 0.5)
    if shiftMode == 0 or shiftMode == 2 then
      if visualShiftDirection > 0 then bodyRight = math.max(bodyRight, o.shiftIntensity)
      else bodyLeft = math.max(bodyLeft, o.shiftIntensity) end
    end
    if shiftMode == 1 or shiftMode == 2 then
      if visualShiftDirection > 0 then triggerRight = math.max(triggerRight, o.shiftIntensity)
      else triggerLeft = math.max(triggerLeft, o.shiftIntensity) end
    end
  end
  publishVisuals(e, bodyLeft, bodyRight, triggerLeft, triggerRight)
end

return M
