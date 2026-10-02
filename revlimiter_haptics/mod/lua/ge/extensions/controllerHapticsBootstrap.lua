local M = {}
local categoryDefinition = {
  order = 3.5, icon = 'vibration', title = 'Xbox Haptics Controller', simplemenu = false,
}

local function registerCategory()
  if not core_input_categories then return end
  if not core_input_categories.controllerHaptics then
    core_input_categories.controllerHaptics = categoryDefinition
  end
end

function M.onExtensionLoaded()
  registerCategory()
end

function M.onExtensionUnloaded()
  if core_input_categories and core_input_categories.controllerHaptics == categoryDefinition then
    core_input_categories.controllerHaptics = nil
  end
end

return M
