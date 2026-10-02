-- In-game tuner. The Controls action loads it on demand.
local M = {}

function M.onInit()
  setExtensionUnloadMode(M, "manual")
end

local im = ui_imgui

local visible = im.BoolPtr(false)
local rtIntensity = im.FloatPtr(85)
local absIntensity = im.FloatPtr(50)
local rtStart = im.FloatPtr(95)
local shiftIntensity = im.FloatPtr(100)
local shiftMode = im.FloatPtr(1)
local startupIntensity = im.FloatPtr(100)
local startupInterval = im.FloatPtr(300)
local absSensitivity = im.FloatPtr(69)
local brakeLockIntensity = im.FloatPtr(85)
local initialized = false
local savedSettings = {}
local shiftModeNames = {'Body motors (right up / left down)', 'Triggers (RT up / LT down)', 'Both'}

local function clamp(value, minimum, maximum)
  if value ~= value then return minimum end
  return math.min(maximum, math.max(minimum, value))
end

local function initialize()
  if initialized then return end
  initialized = true
  rtIntensity[0] = clamp(tonumber(settings.getValue('controllerHapticsRtIntensity', 85)) or 85, 0, 100)
  absIntensity[0] = clamp(tonumber(settings.getValue('controllerHapticsAbsIntensity', 50)) or 50, 0, 100)
  rtStart[0] = clamp(tonumber(settings.getValue('controllerHapticsRtStart', 95)) or 95, 75, 98)
  shiftIntensity[0] = clamp(tonumber(settings.getValue('controllerHapticsShiftIntensity', 100)) or 100, 0, 100)
  shiftMode[0] = clamp(tonumber(settings.getValue('controllerHapticsShiftMode', 1)) or 1, 0, 2)
  startupIntensity[0] = clamp(tonumber(settings.getValue('controllerHapticsStartupIntensity', 100)) or 100, 0, 100)
  startupInterval[0] = clamp(tonumber(settings.getValue('controllerHapticsStartupInterval', 300)) or 300, 50, 300)
  absSensitivity[0] = clamp(tonumber(settings.getValue('controllerHapticsAbsSensitivity', 69)) or 69, 0, 100)
  brakeLockIntensity[0] = clamp(tonumber(settings.getValue('controllerHapticsBrakeLockIntensity', 85)) or 85, 0, 100)
end

local function saveValue(key, value)
  local previous = savedSettings[key]
  if previous == nil then previous = tonumber(settings.getValue(key)) end
  if previous ~= value then settings.setValue(key, value) end
  savedSettings[key] = value
end

local function saveSettings()
  -- Keep immediate feedback while avoiding eight unrelated writes per slider frame.
  saveValue('controllerHapticsRtIntensity', rtIntensity[0])
  saveValue('controllerHapticsAbsIntensity', absIntensity[0])
  saveValue('controllerHapticsRtStart', rtStart[0])
  saveValue('controllerHapticsShiftIntensity', shiftIntensity[0])
  saveValue('controllerHapticsShiftMode', math.floor(shiftMode[0] + 0.5))
  saveValue('controllerHapticsStartupIntensity', startupIntensity[0])
  saveValue('controllerHapticsStartupInterval', startupInterval[0])
  saveValue('controllerHapticsAbsSensitivity', absSensitivity[0])
  saveValue('controllerHapticsBrakeLockIntensity', brakeLockIntensity[0])
end

local function drawWindow()
  if not visible[0] then return end
  im.SetNextWindowSize(im.ImVec2(390, 0), im.Cond_FirstUseEver)
  if im.Begin('Xbox Haptic Feedback Controller', visible, im.WindowFlags_AlwaysAutoResize) then
    im.TextWrapped('Changes apply within 0.1 s of simulation and remain active after restarting BeamNG.')
    im.Separator()
    local changed = false
    changed = im.SliderFloat('RT intensity (rev limiter)##controllerHapticsRt', rtIntensity, 0, 100, '%.0f%%') or changed
    changed = im.SliderFloat('LT intensity (ABS)##controllerHapticsAbs', absIntensity, 0, 100, '%.0f%%') or changed
    changed = im.SliderFloat('ABS sensitivity (early warning)##controllerHapticsAbsSensitivity', absSensitivity, 0, 100, '%.0f%%') or changed
    changed = im.SliderFloat('Brake lock intensity (no ABS)##controllerHapticsLock', brakeLockIntensity, 0, 100, '%.0f%%') or changed
    changed = im.SliderFloat('RT start before limiter##controllerHapticsStart', rtStart, 75, 98, '%.0f%%') or changed
    changed = im.SliderFloat('Gear shift impact##controllerHapticsShift', shiftIntensity, 0, 100, '%.0f%%') or changed
    local modeIndex = math.floor(shiftMode[0] + 0.5) + 1
    local modeLabel = shiftModeNames[modeIndex] or shiftModeNames[2]
    if im.BeginCombo('Shifter output##controllerHapticsShiftMode', modeLabel) then
      for i, name in ipairs(shiftModeNames) do
        local selected = i == modeIndex
        if im.Selectable1(name, selected) then
          shiftMode[0] = i - 1
          changed = true
        end
        if selected then im.SetItemDefaultFocus() end
      end
      im.EndCombo()
    end
    changed = im.SliderFloat('Startup vibration##controllerHapticsStartup', startupIntensity, 0, 100, '%.0f%%') or changed
    changed = im.SliderFloat('Startup left/right interval##controllerHapticsStartupInterval', startupInterval, 50, 300, '%.0f ms') or changed
    im.Separator()
    if im.Button('Reset defaults') then
      rtIntensity[0], absIntensity[0], rtStart[0] = 85, 50, 95
      shiftIntensity[0], shiftMode[0] = 100, 1
      startupIntensity[0], startupInterval[0] = 100, 300
      absSensitivity[0], brakeLockIntensity[0] = 69, 85
      changed = true
    end
    im.SameLine()
    if im.Button('Hide window') then visible[0] = false end
    if changed then saveSettings() end
  end
  im.End()
end

function M.onUpdate()
  initialize()
  if core_gamestate and core_gamestate.loading and core_gamestate.loading() then
    visible[0] = false
    return
  end
  drawWindow()
end

function M.toggle()
  initialize()
  if core_gamestate and core_gamestate.loading and core_gamestate.loading() then return end
  visible[0] = not visible[0]
end

function M.onExtensionUnloaded()
  visible[0] = false
end

return M
