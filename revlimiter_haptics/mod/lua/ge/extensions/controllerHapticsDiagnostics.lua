-- Loopback diagnostics and mixer control. Loaded flat by modScript, socket opened
-- later during onUpdate. No native work or nested extension load during mounting.
local M = {}
local udp, socketLib
local elapsed, sinceReply, retry = 0, 1, 0
local sequence, accepted, vehicleId = 0, 0, -1
local status = {available=false}
local lastPublishedVehicle = -1
local function finite(v) return type(v)=='number' and v==v and math.abs(v)<math.huge end
local function clamp(v, lo, hi, default)
  v = tonumber(v)
  if not finite(v) then return default end
  return math.min(hi, math.max(lo, v))
end
local function close()
  if udp then udp:close() end
  udp = nil
end
local function clearVehicle(id)
  local veh = be and be:getObjectByID(id)
  if veh then veh:queueLuaCommand('if electrics then local e=electrics.values; e.controllerHapticsVisualBodyLeft=0; e.controllerHapticsVisualBodyRight=0; e.controllerHapticsVisualTriggerLeft=0; e.controllerHapticsVisualTriggerRight=0; e.controllerHapticsOutputValid=0; e.controllerHapticsFeedbackTTL=0 end') end
end
local function publish(s)
  if s.available and s.vehicle==vehicleId and s.applied==1 and s.telemetry==1 then
    local veh = getPlayerVehicle(0)
    if veh then
      veh:queueLuaCommand(string.format('if electrics then local e=electrics.values; e.controllerHapticsVisualBodyLeft=%.5f; e.controllerHapticsVisualBodyRight=%.5f; e.controllerHapticsVisualTriggerLeft=%.5f; e.controllerHapticsVisualTriggerRight=%.5f; e.controllerHapticsOutputValid=1; e.controllerHapticsFeedbackTTL=0.5 end', s.left,s.right,s.lt,s.rt))
    end
  else
    clearVehicle(vehicleId)
  end
  lastPublishedVehicle = vehicleId
  if guihooks then guihooks.trigger('ControllerHapticsStatus', s) end
end
local function valid(s)
  if type(s)~='table' or type(s.version)~='string' or not s.version:match('^%d+%.%d+%.%d+%.%d+$') then return false end
  for _,key in ipairs({'seq','vehicle','telemetry','paired','enabled','applied','backend','error','left','right','lt','rt','initMs','initPeakMs','scanMs','scanPeakMs','outputMs','outputPeakMs','uninitPeakMs','slowCalls'}) do
    if not finite(s[key]) then return false end
  end
  if s.backend%1~=0 or s.backend<0 or s.backend>2 then return false end
  if s.vehicle%1~=0 or s.vehicle<0 or s.vehicle>4294967295 then return false end
  if s.seq%1~=0 or s.seq<=accepted or s.seq>sequence or s.seq<sequence-16 then return false end
  for _,key in ipairs({'left','right','lt','rt','telemetry','paired','enabled','applied'}) do
    if s[key]<0 or s[key]>1 then return false end
  end
  for _,key in ipairs({'telemetry','paired','enabled','applied'}) do
    if s[key]%1~=0 then return false end
  end
  for _,key in ipairs({'initMs','initPeakMs','scanMs','scanPeakMs','outputMs','outputPeakMs','uninitPeakMs','slowCalls'}) do
    if s[key]<0 or s[key]>1e12 then return false end
  end
  return true
end
function M.getStatus() return status end
function M.onUpdate(dtReal)
  local dt = clamp(dtReal,0,1,0)
  elapsed, sinceReply, retry = elapsed+dt, sinceReply+dt, retry-dt
  local veh = getPlayerVehicle and getPlayerVehicle(0)
  local current = veh and veh:getID() or -1
  if current~=vehicleId then
    if lastPublishedVehicle>=0 then clearVehicle(lastPublishedVehicle) end
    vehicleId, status, sinceReply = current, {available=false}, 1
  end
  if sinceReply>0.5 and status.available then status={available=false}; publish(status) end
  if not udp and retry<=0 then
    retry=2
    if not socketLib then
      local ok,lib=pcall(require,'socket')
      if ok then socketLib=lib end
    end
    if socketLib then
      udp=socketLib.udp()
      if udp then
        udp:settimeout(0)
        if not udp:setsockname('127.0.0.1',0) or not udp:setpeername('127.0.0.1',26780) then close() end
      end
    end
  end
  if not udp then return end
  -- Bound work even if another local process sends malformed replies.
  for _=1,16 do
    local data=udp:receive(1400)
    if not data then break end
    if data:sub(1,5)=='BCD1 ' then
      local ok,s=pcall(jsonDecode,data:sub(6))
      if ok and valid(s) then
        accepted=s.seq
        s.available=true; s.vehicleMatches=s.vehicle==vehicleId
        status=s; sinceReply=0; publish(s)
      end
    end
  end
  if elapsed>=1/30 then
    elapsed=0
    sequence=sequence+1
    if sequence>=2147483647 then sequence=1; accepted=0 end
    local mode=math.floor(clamp(settings.getValue('controllerHapticsMixMode',0),0,3,0)+0.5)
    local a=clamp(settings.getValue('controllerHapticsNativeGain',100),0,100,100)/100
    local b=clamp(settings.getValue('controllerHapticsCustomGain',100),0,100,100)/100
    local sent=udp:send(string.format('BCQ1 %d %d %d %.4f %.4f',sequence,math.max(0,vehicleId),mode,a,b))
    if not sent then close() end
  end
end
function M.onExtensionUnloaded()
  if lastPublishedVehicle>=0 then clearVehicle(lastPublishedVehicle) end
  close(); status={available=false}
end
return M
