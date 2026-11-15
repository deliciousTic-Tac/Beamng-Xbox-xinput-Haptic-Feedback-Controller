local M = {}
local registrationPending = false
local categoryDefinition = {
  order = 3.5, icon = 'vibration', title = 'Xbox Haptics Controller', simplemenu = false,
}

local function registerCategory()
  if not core_input_categories then return false end
  if not core_input_categories.controllerHaptics then
    core_input_categories.controllerHaptics = categoryDefinition
  end
  return true
end

function M.onUpdate()
  if registrationPending and registerCategory() then registrationPending = false end
end

function M.onExtensionLoaded()
  -- Retry only while the input module is unavailable. No action-cache mutation,
  -- nested extension loads or recursive input-binding refreshes.
  registrationPending = true
  M.onUpdate()
end

function M.onExtensionUnloaded()
  registrationPending = false
  if core_input_categories and core_input_categories.controllerHaptics == categoryDefinition then
    core_input_categories.controllerHaptics = nil
  end
end

return M
