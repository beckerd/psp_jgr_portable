-- Read original resident stage sound banks after the normal stage loader.
-- No audio, task, bank or channel writes. Base fixture supplies stage entry.
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
local out=assert(os.getenv('JGR_REFERENCE_OUT'));local m=flycast.memory
dofile(root..'tools/flycast_stage_reference.lua');local base=flycast_callbacks.vblank
local age=0
flycast_callbacks.vblank=function()
 base();if not JGR_REFERENCE_READY then return end;age=age+1;if age~=60 then return end
 local manager=m.read32(0x8c2ae010);assert(manager>=0x8c000000 and manager<0x8d000000)
 for _,r in ipairs({{'seq',2,0xa4},{'patch',2,0x124},{'osb',6,0x1a4}})do
  local at=manager+r[3]+r[2]*8;local a,n=m.read32(at),m.read32(at+4)
  print('WORLD_BANK '..r[1]..' '..a..' '..n);assert(a+n<=0x200000)
  local f=assert(io.open(out..'world_'..r[1]..'.bin','wb'));local bytes={}
  for k=0,n-1,4 do bytes[#bytes+1]=string.pack('<I4',m.read32(0xa0800000+a+k))end
  f:write(table.concat(bytes):sub(1,n));f:close()
 end
 print('WORLD_BANK complete');io.stdout:flush();os.exit(0)
end
